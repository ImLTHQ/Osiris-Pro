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
#include <cstdint>
#include <string>
#include <vector>

#include "resource.h"
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
    std::wprintf(L"\nPress any key to close...");
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

// Target process name (default cs2.exe; INJECTOR_TARGET_EXE overrides for testing).
const wchar_t* TargetExe() {
    static wchar_t buf[64] = {};
    if (buf[0] == L'\0') {
        if (GetEnvironmentVariableW(L"INJECTOR_TARGET_EXE", buf, 64) == 0 ||
            buf[0] == L'\0')
            wcscpy_s(buf, L"cs2.exe");
    }
    return buf;
}

// Dev hooks (invisible to normal users).
bool EnvFlag(const wchar_t* name) {
    wchar_t v[2] = {};
    return GetEnvironmentVariableW(name, v, 2) > 0 && v[0] == L'1';
}

// Returns the pid of the first running target process, or 0 if none.
DWORD FindTarget() {
    const auto procs = mm::FindProcessesByName(TargetExe());
    if (procs.size() > 1)
        std::wprintf(L"[!] multiple %ls processes; using the first (pid %lu).\n",
                     TargetExe(), procs.front().pid);
    return procs.empty() ? 0 : procs.front().pid;
}

// Launches the target. For cs2.exe this goes through Steam (steam://rungameid/730).
bool LaunchTarget() {
    if (EnvFlag(L"INJECTOR_NO_LAUNCH")) {
        std::wprintf(L"[*] launch skipped (INJECTOR_NO_LAUNCH=1); waiting for %ls...\n",
                     TargetExe());
        return true;
    }
    if (_wcsicmp(TargetExe(), L"cs2.exe") == 0) {
        std::wprintf(L"[*] %ls not found; launching CS2 via Steam (steam://rungameid/730)...\n",
                     TargetExe());
        const HINSTANCE r = ShellExecuteW(nullptr, L"open", L"steam://rungameid/730",
                                          nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<std::intptr_t>(r) > 32;
    }
    std::wprintf(L"[*] %ls not found; waiting for it to start...\n", TargetExe());
    return true;
}

// Refresh interval for the process/module polling loops below.
constexpr DWORD kPollIntervalMs = 3000;

// Maximum wait for the game client (client.dll) per attempt: 1 minute.
constexpr DWORD kClientWaitTimeoutMs = 60 * 1000;

// Polls for the target process until it appears (no timeout - waits forever).
DWORD WaitForProcess() {
    const DWORD t0 = GetTickCount();
    DWORD lastStatus = 0;
    for (;;) {
        if (const DWORD pid = FindTarget())
            return pid;
        const DWORD elapsed = GetTickCount() - t0;
        if (elapsed - lastStatus >= kPollIntervalMs * 2) {
            lastStatus = elapsed;
            std::wprintf(L"[*] waiting for %ls to start... (%u s)\n", TargetExe(),
                         elapsed / 1000);
        }
        Sleep(kPollIntervalMs);
    }
}

// True when the target process has loaded the real game code (client.dll).
// This is the phase discriminator: at the "international / China region"
// selection dialog engine2.dll is already loaded, but client.dll is NOT -
// it only appears once the real game starts after the region choice.
bool GameEngineLoaded(DWORD pid) {
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE)
        return true;  // cannot check -> do not block on it
    MODULEENTRY32W me = {};
    me.dwSize = sizeof(me);
    bool found = false;
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, L"client.dll") == 0) {
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

// Waits until the game client (client.dll) is loaded in the target. The
// status line is printed once per attempt and the loop then polls silently
// every kPollIntervalMs; after kClientWaitTimeoutMs it reports a timeout and
// offers an any-key retry. Returns true once the client is loaded, false when
// it gives up (console not interactive, so no retry key can arrive).
bool WaitForClientLoaded(DWORD pid) {
    for (;;) {
        std::wprintf(L"[*] waiting for the game client (client.dll)...\n");
        const DWORD t0 = GetTickCount();
        for (;;) {
            if (GameEngineLoaded(pid))
                return true;
            if (GetTickCount() - t0 >= kClientWaitTimeoutMs)
                break;
            Sleep(kPollIntervalMs);
        }
        std::wprintf(L"[!] timeout: client.dll not loaded within %u s "
                     L"(maximum wait per attempt: 1 minute).\n",
                     kClientWaitTimeoutMs / 1000);
        if (!ConsoleInteractive()) {
            std::wprintf(L"[!] no interactive console for a retry; aborting.\n");
            return false;
        }
        std::wprintf(L"Press any key to retry...");
        fflush(stdout);
        _getwch();
        fflush(stdout);
    }
}

}  // namespace

int wmain() {
    // Live diagnostic output: never buffer stdout (the console log is the
    // developer's debugging tool and must appear in real time, even when
    // redirected to a file).
    setvbuf(stdout, nullptr, _IONBF, 0);

    // ---- elevation gate ----
    // Insufficient rights -> request elevation; if denied, abort WITHOUT loading.
    const int elev = EnsureElevated();
    if (elev <= 0) {
        const int rc = elev == 0 ? 0 : 3;
        if (elev != 0)
            PauseOnExit();  // keep the window open so the user can read the error
        return rc;
    }

    // ---- find the target; if missing, launch it via Steam and wait ----
    DWORD pid = FindTarget();
    if (pid == 0) {
        if (!LaunchTarget()) {
            std::wprintf(L"[!] failed to launch %ls (Steam not available?).\n", TargetExe());
            PauseOnExit();
            return 4;
        }
        pid = WaitForProcess();  // waits forever, 3 s refresh
        std::wprintf(L"[+] %ls started (pid %lu).\n", TargetExe(), pid);
    } else {
        std::wprintf(L"[*] target: %ls already running (pid %lu).\n", TargetExe(), pid);
    }

    // Both paths: wait until the game client (client.dll) is loaded, then
    // inject immediately - no window-focus gating. At the region-select
    // dialog engine2.dll is loaded but client.dll is not, so injection
    // naturally happens once the real game starts. The wait times out after
    // 1 minute and offers an any-key retry.
    if (!WaitForClientLoaded(pid))
        return 6;
    std::wprintf(L"[*] game client loaded; injecting immediately.\n");

    // ---- dev hook: dry run ----
    if (EnvFlag(L"INJECTOR_DRY_RUN")) {
        std::wprintf(L"[!] DRY RUN (INJECTOR_DRY_RUN=1): skipping injection.\n");
        PauseOnExit();
        return 0;
    }

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
    // Osiris.dll is embedded verbatim as an RCDATA resource in the .rsrc
    // section (see resource.h and the generated EmbeddedDll.rc). The bytes
    // stay in a read-only section and are only read by the manual mapper.
    const HRSRC dllRes = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_EMBEDDED_DLL),
                                       RT_RCDATA);
    if (dllRes == nullptr) {
        std::wprintf(L"[!] embedded DLL resource missing (broken build?).\n");
        CloseHandle(hProc);
        PauseOnExit();
        return 4;
    }
    const auto* dllBytes = static_cast<const std::uint8_t*>(
        LockResource(LoadResource(nullptr, dllRes)));
    const DWORD dllSize = SizeofResource(nullptr, dllRes);
    if (dllBytes == nullptr || dllSize == 0) {
        std::wprintf(L"[!] embedded DLL resource is empty (broken build?).\n");
        CloseHandle(hProc);
        PauseOnExit();
        return 4;
    }
    std::wprintf(L"[*] module: embedded Osiris.dll (%lu bytes)\n", dllSize);

    // ---- map ----
    std::wstring error;
    const DWORD t0 = GetTickCount();
    const bool ok = mm::ManualMap(hProc, dllBytes, dllSize, error);
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
