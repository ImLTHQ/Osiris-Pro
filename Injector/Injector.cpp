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

// Polls for the target process until it appears or the timeout expires.
DWORD WaitForProcess(DWORD timeoutMs) {
    const DWORD t0 = GetTickCount();
    DWORD lastStatus = 0;
    for (;;) {
        if (const DWORD pid = FindTarget())
            return pid;
        const DWORD elapsed = GetTickCount() - t0;
        if (elapsed >= timeoutMs)
            return 0;
        if (elapsed - lastStatus >= 5000) {
            lastStatus = elapsed;
            std::wprintf(L"[*] waiting for %ls to start... (%u s)\n", TargetExe(),
                         elapsed / 1000);
        }
        Sleep(500);
    }
}

// Case-insensitive substring search.
bool WcsContainsIgnoreCase(const wchar_t* hay, const wchar_t* needle) {
    if (!hay || !needle)
        return false;
    const std::size_t nl = wcslen(needle);
    if (nl == 0)
        return true;
    for (const wchar_t* p = hay; *p; ++p) {
        if (_wcsnicmp(p, needle, nl) == 0)
            return true;
    }
    return false;
}

// True when hwnd looks like the REAL CS2 game window (not the small
// "international / China region" selection dialog that appears at startup):
// visible, SDL window class, CS2-like title and a full-size window.
bool IsGameWindow(HWND hwnd) {
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd))
        return false;

    wchar_t cls[128] = {};
    GetClassNameW(hwnd, cls, 128);
    if (_wcsicmp(cls, L"SDL_app") != 0)
        return false;

    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, 256);
    if (!WcsContainsIgnoreCase(title, L"Counter-Strike 2") &&
        !WcsContainsIgnoreCase(title, L"CS2"))
        return false;

    RECT rc = {};
    if (!GetWindowRect(hwnd, &rc))
        return false;
    return (rc.right - rc.left) >= 800 && (rc.bottom - rc.top) >= 600;
}

// True when the foreground window is the real game window of the target pid,
// i.e. the user is actually inside the game (region dialog excluded).
bool IsForeground(DWORD pid) {
    const HWND fg = GetForegroundWindow();
    if (!fg)
        return false;
    DWORD fgPid = 0;
    GetWindowThreadProcessId(fg, &fgPid);
    return fgPid == pid && IsGameWindow(fg);
}

// True when the target process has loaded the real game engine (client.dll /
// engine2.dll) - i.e. it is past the launcher/region-selection phase.
bool GameEngineLoaded(DWORD pid) {
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE)
        return true;  // cannot check -> do not block on it
    MODULEENTRY32W me = {};
    me.dwSize = sizeof(me);
    bool found = false;
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, L"engine2.dll") == 0 ||
                _wcsicmp(me.szModule, L"client.dll") == 0) {
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

// Waits until the real game window is focused by the user AND the game engine
// is loaded (or the timeout expires).
bool WaitForFocus(DWORD pid, DWORD timeoutMs) {
    std::wprintf(L"[*] waiting for the game to load and for its window to receive focus...\n");
    const DWORD t0 = GetTickCount();
    DWORD lastStatus = 0;
    bool engineSeen = false;
    for (;;) {
        const bool focused = IsForeground(pid);
        const bool engine = GameEngineLoaded(pid);
        if (!engineSeen && engine) {
            engineSeen = true;
            std::wprintf(L"[+] game engine loaded (client.dll present).\n");
        }
        if (focused && engine) {
            std::wprintf(L"[+] game window is in focus.\n");
            return true;
        }
        const DWORD elapsed = GetTickCount() - t0;
        if (elapsed >= timeoutMs)
            return false;
        if (elapsed - lastStatus >= 5000) {
            lastStatus = elapsed;
            std::wprintf(L"[*] waiting for focus... (%u s; focused=%d engine=%d)\n",
                         elapsed / 1000, focused ? 1 : 0, engine ? 1 : 0);
        }
        Sleep(500);
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
    constexpr DWORD kProcessWaitMs = 5 * 60 * 1000;   // 5 minutes to start
    constexpr DWORD kFocusWaitMs = 15 * 60 * 1000;    // 15 minutes for user focus

    DWORD pid = FindTarget();
    if (pid == 0) {
        if (!LaunchTarget()) {
            std::wprintf(L"[!] failed to launch %ls (Steam not available?).\n", TargetExe());
            PauseOnExit();
            return 4;
        }
        pid = WaitForProcess(kProcessWaitMs);
        if (pid == 0) {
            std::wprintf(L"[!] %ls did not start within 5 minutes; aborting.\n", TargetExe());
            PauseOnExit();
            return 4;
        }
        std::wprintf(L"[+] %ls started (pid %lu).\n", TargetExe(), pid);

        // Only inject once the user has the game window focused.
        if (!WaitForFocus(pid, kFocusWaitMs)) {
            std::wprintf(L"[!] game window never received focus within 15 minutes; aborting.\n");
            PauseOnExit();
            return 4;
        }
        std::wprintf(L"[*] injecting now...\n");
    } else {
        std::wprintf(L"[*] target: %ls already running (pid %lu); injecting immediately.\n",
                     TargetExe(), pid);
    }

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
