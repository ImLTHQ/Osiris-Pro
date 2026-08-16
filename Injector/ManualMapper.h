#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace mm {

struct ProcessInfo {
    DWORD pid = 0;
    std::wstring name;
};

// Enumerates all running processes whose image name matches (case-insensitive).
std::vector<ProcessInfo> FindProcessesByName(const wchar_t* exeName);

// Manual-map `dllBytes` (a complete x64 PE image, file or memory form) into `process`.
// The module is mapped without LoadLibrary: imports are resolved in-target by a
// position-independent payload (Shellcode.asm), relocations are applied, and the
// entry point (DllMain) is invoked with DLL_PROCESS_ATTACH.
//
// On failure returns false and writes a human-readable reason into `error`.
bool ManualMap(HANDLE process, const std::uint8_t* dllBytes, std::size_t dllSize,
               std::wstring& error);

} // namespace mm
