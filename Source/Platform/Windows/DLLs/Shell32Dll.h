#pragma once

// MinGW-w64 ships the SHGetKnownFolderPath declaration in <shlobj.h>
// (older headers have no separate shlobj_core.h).
#ifdef __MINGW32__
#include <shlobj.h>
#else
#include <ShlObj_core.h>
#endif

#include <Platform/Windows/WindowsDynamicLibrary.h>

struct Shell32Dll : WindowsDynamicLibrary {
    Shell32Dll() : WindowsDynamicLibrary{ "shell32.dll" }
    {
    }

    [[nodiscard]] auto SHGetKnownFolderPath() const noexcept
    {
        return getFunctionAddress("SHGetKnownFolderPath").as<decltype(&::SHGetKnownFolderPath)>();
    }
};
