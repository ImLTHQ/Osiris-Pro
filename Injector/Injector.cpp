// Injector - command-line manual-map DLL injector for cs2.exe (x64).
//
// Modes:
//   Injector.exe                       inject the EMBEDDED Osiris.dll into cs2.exe
//   Injector.exe <path\to\module.dll>  inject the given DLL into cs2.exe
//   Injector.exe --pid <pid> [dll]     target a specific process id
//   Injector.exe --list                list running cs2.exe processes
//   Injector.exe --help                show usage

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
// e.g. from a script or CI — in that case we must not block on key input).
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
int EnsureElevated(int argc, wchar_t** argv) {
    if (IsElevated())
        return 1;

    std::wprintf(L"[*] not running as administrator; requesting elevation...\n");
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::wstring cmdline;
    for (int i = 1; i < argc; ++i) {
        if (i > 1)
            cmdline += L' ';
        const std::wstring a = argv[i];
        if (a.find(L' ') != std::wstring::npos) {
            cmdline += L'"';
            cmdline += a;
            cmdline += L'"';
        } else {
            cmdline += a;
        }
    }

    const HINSTANCE result = ShellExecuteW(nullptr, L"runas", exePath,
                                           cmdline.empty() ? nullptr : cmdline.c_str(),
                                           nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<std::intptr_t>(result) <= 32) {
        std::wprintf(L"[!] elevation was denied; injection aborted (no module loaded).\n");
        return -1;
    }
    std::wprintf(L"[+] elevation accepted; the elevated instance continues in a new window.\n");
    return 0;
}

void PrintUsage() {
    std::wprintf(
        L"Injector - manual-map DLL injector for cs2.exe (x64)\n"
        L"\n"
        L"Usage:\n"
        L"  Injector.exe                           inject embedded Osiris.dll into cs2.exe\n"
        L"  Injector.exe <module.dll>              inject the given DLL into cs2.exe\n"
        L"  Injector.exe --pid <pid> [module.dll]  target a specific process id\n"
        L"  Injector.exe --list                    list running cs2.exe processes\n"
        L"  Injector.exe --help                    show this help\n"
        L"\n"
        L"Notes:\n"
        L"  - The module is manual-mapped (no LoadLibrary / no PEB module entry).\n"
        L"  - If not running as administrator, the injector self-elevates via UAC;\n"
        L"    if elevation is denied, nothing is loaded.\n"
        L"  - If no DLL path is given, the DLL embedded at build time is used.\n"
        L"  - Osiris saves its config to %%APPDATA%%\\OsirisCS2\\configs\\default.cfg\n");
}

bool ReadFileBytes(const wchar_t* path, std::vector<std::uint8_t>& out) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        std::wprintf(L"[!] cannot open '%ls' (error %lu)\n", path, GetLastError());
        return false;
    }
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(f, &size) || size.QuadPart <= 0 || size.QuadPart > 512LL * 1024 * 1024) {
        std::wprintf(L"[!] bad file size for '%ls'\n", path);
        CloseHandle(f);
        return false;
    }
    out.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    const BOOL ok = ReadFile(f, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);
    CloseHandle(f);
    if (!ok || read != out.size()) {
        std::wprintf(L"[!] failed to read '%ls'\n", path);
        return false;
    }
    return true;
}

HANDLE OpenTarget(DWORD pid) {
    return OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                           PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                       FALSE, pid);
}

}  // namespace

int Run(int argc, wchar_t** argv) {
    std::wstring dllPath;
    DWORD pidOverride = 0;
    bool listOnly = false;

    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--help" || a == L"-h" || a == L"/?" || a == L"-?") {
            PrintUsage();
            return 0;
        } else if (a == L"--list") {
            listOnly = true;
        } else if (a == L"--pid") {
            if (i + 1 >= argc) {
                std::wprintf(L"[!] --pid requires an argument\n");
                return 1;
            }
            pidOverride = static_cast<DWORD>(_wtoi(argv[++i]));
            if (pidOverride == 0) {
                std::wprintf(L"[!] invalid pid '%ls'\n", argv[i]);
                return 1;
            }
        } else {
            dllPath = a;
        }
    }

    if (listOnly) {
        const auto procs = mm::FindProcessesByName(L"cs2.exe");
        if (procs.empty()) {
            std::wprintf(L"[-] no cs2.exe process is currently running\n");
        } else {
            std::wprintf(L"[+] found %zu cs2.exe process(es):\n", procs.size());
            for (const auto& p : procs)
                std::wprintf(L"    pid %lu  (%ls)\n", p.pid, p.name.c_str());
        }
        return 0;
    }

    // ---- elevation gate (injection path only) ----
    // Insufficient rights -> request elevation; if denied, abort WITHOUT loading.
    const int elev = EnsureElevated(argc, argv);
    if (elev <= 0)
        return elev == 0 ? 0 : 3;

    // ---- locate target ----
    DWORD pid = pidOverride;
    if (pid == 0) {
        const auto procs = mm::FindProcessesByName(L"cs2.exe");
        if (procs.empty()) {
            std::wprintf(L"[!] cs2.exe not running. Start the game, or use --pid <pid>.\n");
            return 2;
        }
        pid = procs.front().pid;
        if (procs.size() > 1)
            std::wprintf(L"[!] multiple cs2.exe processes; using first (pid %lu). "
                         L"Use --pid to pick another.\n",
                         pid);
    }
    std::wprintf(L"[*] target: cs2.exe (pid %lu)\n", pid);

    HANDLE hProc = OpenTarget(pid);
    if (!hProc) {
        std::wprintf(L"[!] OpenProcess failed (error %lu). "
                     L"Try running as administrator.\n",
                     GetLastError());
        return 3;
    }

    // ---- load module bytes ----
    std::vector<std::uint8_t> bytes;
    if (!dllPath.empty()) {
        std::wprintf(L"[*] module: %ls (external)\n", dllPath.c_str());
        if (!ReadFileBytes(dllPath.c_str(), bytes)) {
            CloseHandle(hProc);
            return 4;
        }
    } else {
        if (kEmbeddedDllSize == 0) {
            std::wprintf(L"[!] no embedded DLL and no DLL path given. "
                         L"Rebuild via build_injector.ps1 or pass a path.\n");
            CloseHandle(hProc);
            return 4;
        }
        std::wprintf(L"[*] module: embedded Osiris.dll (%u bytes)\n", kEmbeddedDllSize);
        bytes.assign(kEmbeddedDll, kEmbeddedDll + kEmbeddedDllSize);
    }

    // ---- map ----
    std::wstring error;
    const DWORD t0 = GetTickCount();
    if (mm::ManualMap(hProc, bytes.data(), bytes.size(), error)) {
        std::wprintf(L"[+] success: module mapped into pid %lu (%lu ms)\n", pid,
                     GetTickCount() - t0);
        if (dllPath.empty())
            std::wprintf(L"[*] Osiris saves its config to %%APPDATA%%\\OsirisCS2\\configs\\default.cfg\n");
        CloseHandle(hProc);
        return 0;
    }
    std::wprintf(L"[!] manual map failed: %ls\n", error.c_str());
    CloseHandle(hProc);
    return 5;
}

int wmain(int argc, wchar_t** argv) {
    const int rc = Run(argc, argv);
    PauseOnExit();
    return rc;
}
