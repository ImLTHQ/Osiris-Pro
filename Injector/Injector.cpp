// Injector - parameterless manual-map DLL injector for cs2.exe (x64).
//
// Novice flow: double-click -> UAC auto-elevate -> map the embedded Osiris.dll
// into cs2.exe. No arguments are required or accepted.
// The console output below is diagnostic output intended for the developer.

#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <conio.h>

#include <cstdio>
#include <string>
#include <vector>

#include "EmbeddedDll.h"
#include "ManualMapper.h"

namespace {

// True when the current process runs with a full administrator token.
bool IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elev = {};
    DWORD size = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &size);
    CloseHandle(token);
    return ok && elev.TokenIsElevated;
}

// True when stdin is an interactive console (false when output is redirected,
// e.g. from a script or CI - in that case we must not block on key input).
bool ConsoleInteractive() {
    const HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    if (in == INVALID_HANDLE_VALUE || in == nullptr)
        return false;
    DWORD mode = 0;
    return GetConsoleMode(in, &mode) != FALSE;
}

// Keeps the console window open after finishing (double-click scenario).
void PauseOnExit() {
    if (!ConsoleInteractive())
        return;
    std::wprintf(L"\nPress any key to continue...");
    fflush(stdout);
    _getwch();
    fflush(stdout);
}

// Elevation gate. Returns:
//   1  - already elevated, continue in this process
//   0  - elevation was granted; the elevated child process does the work,
//        this (parent) instance must exit without loading anything
//  -1  - elevation was denied; caller must abort WITHOUT loading the module
int EnsureElevated() {
    // Developer/testing hook: INJECTOR_NO_ELEVATE=1 skips elevation (no UAC prompt).
    // Invisible to normal users.
    wchar_t skip[2] = {};
    if (GetEnvironmentVariableW(L"INJECTOR_NO_ELEVATE", skip, 2) > 0 && skip[0] == L'1')
        return 1;

    if (IsElevated())
        return 1;

    std::wprintf(L"[*] not running as administrator; requesting elevation...\n");
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    const HINSTANCE result = ShellExecuteW(nullptr, L"runas", exePath, nullptr, nullptr,
                                           SW_SHOWNORMAL);
    if (reinterpret_cast<std::intptr_t>(result) <= 32) {
        std::wprintf(L"[!] elevation was denied; injection aborted (no module loaded).\n");
        return -1;
    }
    std::wprintf(L"[+] elevation accepted; the elevated instance continues in a new window.\n");
    return 0;
}

HANDLE OpenTarget(DWORD pid) {
    return OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                           PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                       FALSE, pid);
}

}  // namespace

int wmain() {
    // ---- elevation gate ----
    // Insufficient rights -> request elevation; if denied, abort WITHOUT loading.
    const int elev = EnsureElevated();
    if (elev <= 0) {
        const int rc = elev == 0 ? 0 : 3;
        if (elev != 0)
            PauseOnExit();  // keep the window open so the user can read the error
        return rc;
    }

    // ---- locate target ----
    const auto procs = mm::FindProcessesByName(L"cs2.exe");
    if (procs.empty()) {
        std::wprintf(L"[!] cs2.exe not running. Start the game and run again.\n");
        PauseOnExit();
        return 2;
    }
    const DWORD pid = procs.front().pid;
    if (procs.size() > 1)
        std::wprintf(L"[!] multiple cs2.exe processes; using the first (pid %lu).\n", pid);
    std::wprintf(L"[*] target: cs2.exe (pid %lu)\n", pid);

    // ---- open target with injection rights ----
    HANDLE hProc = OpenTarget(pid);
    if (!hProc) {
        std::wprintf(L"[!] OpenProcess failed (error %lu). The target denied access; "
                     L"nothing was loaded.\n",
                     GetLastError());
        PauseOnExit();
        return 3;
    }

    // ---- embedded module ----
    if (kEmbeddedDllSize == 0) {
        std::wprintf(L"[!] no embedded DLL (rebuild via build_injector.ps1).\n");
        CloseHandle(hProc);
        PauseOnExit();
        return 4;
    }
    std::wprintf(L"[*] module: embedded Osiris.dll (%u bytes)\n", kEmbeddedDllSize);

    // ---- map ----
    std::wstring error;
    const DWORD t0 = GetTickCount();
    const bool ok = mm::ManualMap(hProc, kEmbeddedDll, kEmbeddedDllSize, error);
    CloseHandle(hProc);

    if (ok) {
        std::wprintf(L"[+] success: module mapped into pid %lu (%lu ms)\n", pid,
                     GetTickCount() - t0);
        std::wprintf(L"[*] Osiris saves its config to %%APPDATA%%\\OsirisCS2\\configs\\default.cfg\n");
        PauseOnExit();
        return 0;
    }
    std::wprintf(L"[!] manual map failed: %ls\n", error.c_str());
    PauseOnExit();
    return 5;
}
