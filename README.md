# Osiris

[![Windows](https://github.com/danielkrupinski/Osiris/actions/workflows/windows.yml/badge.svg?branch=master&event=push)](https://github.com/danielkrupinski/Osiris/actions/workflows/windows.yml)
[![Linux](https://github.com/danielkrupinski/Osiris/actions/workflows/linux.yml/badge.svg?branch=master&event=push)](https://github.com/danielkrupinski/Osiris/actions/workflows/linux.yml)

Cross-platform (Windows, Linux) game hack for **Counter-Strike 2** with GUI and rendering based on game's Panorama UI. Compatible with the latest game update on Steam.

## What's new

* 04 November 2025
    * Improved smoothness of "Player Info in World" on moving players

* 30 October 2025
    * Added Bomb Plant Alert feature
        * Green color means the bomb will be planted before the end of the round if uninterrupted
        * Red color means the bomb can not be planted before the end of the round

    <img width="201" height="146" alt="Bomb Plant Alert" src="https://github.com/user-attachments/assets/21c0f8fb-a20d-42df-9857-f578cfc9b9f9" />

* 23 October 2025
    * Hostage Outline Glow hue is now customizable

* 20 October 2025
    * Added "No Scope Inaccuracy Visualization" feature

    <img height="300" alt="no scope inaccuracy visualization" src="https://github.com/user-attachments/assets/860c944a-00b1-4b67-9d41-6f43e46f4252" />

* 09 October 2025
    * Added viewmodel fov modification

    ![Viewmodel fov modification](https://github.com/user-attachments/assets/3b9d6bde-a68c-4739-913c-d3b6caba4117)

## Technical features

* C++ runtime library (CRT) is not used in release builds
* No heap memory allocations
* No static imports in release build on Windows
* No threads are created
* Exceptions are not used
* No external dependencies

## Compiling

### Prerequisites

**Windows**: Visual Studio 2022 with the **Desktop development with C++** workload
**Linux**: CMake 3.28 or newer, g++ 14 or newer or clang++ 18 or newer

### Windows (single command)

```powershell
& ((& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe")[0]) Osiris.sln /p:Platform=x64 /p:Configuration=Release
```

This locates MSBuild automatically (it is not on PATH in a regular PowerShell
window) and builds the whole solution with the projects' default toolset
(v143). Inside a **Developer PowerShell / Developer Command Prompt for VS** the
plain command works too:

```bat
msbuild Osiris.sln /p:Platform=x64 /p:Configuration=Release
```

The projects target the **v143** toolset (Visual Studio 2022). If only a newer
toolset is installed (e.g. v144/v145 from a newer Visual Studio), either add the
matching component in the Visual Studio Installer ("MSVC v143 - VS 2022 C++
x64/x86 build tools") or override it per invocation:

```powershell
... Osiris.sln /p:Platform=x64 /p:Configuration=Release /p:PlatformToolset=v144
```

One build produces both artifacts (unified output directory `x64\Release\`, no scripts involved):
- `Osiris.dll`
- `Injector.exe` — embeds the freshly built `Osiris.dll` as an RCDATA resource
  (a one-line `EmbeddedDll.rc` is generated during the build and rc.exe packs the
  bytes at link time). Double-click it and it injects.

Debug artifacts land in `x64\Debug\`, intermediate files in `x64\obj\`.

### Other toolchains (CMake: MSVC / ClangCL / MinGW-w64)

```bat
:: MSVC
cmake -S . -B build -A x64
cmake --build build --config Release --target Injector

:: ClangCL
cmake -S . -B build -A x64 -T ClangCL
cmake --build build --config Release --target Injector
```

```bash
# MinGW-w64 (requires gcc/g++, windres, nasm and Ninja — e.g. from MSYS2)
cmake -S . -B build -G Ninja -D CMAKE_C_COMPILER=gcc -D CMAKE_CXX_COMPILER=g++
cmake --build build --target Injector
```

Artifacts are unified in `build/x64/<config>/` (multi-config generators) or
`build/x64/` (single-config generators). Assembly is picked per toolchain
(MASM `Shellcode.asm` for MSVC/ClangCL, NASM `Shellcode.nasm` for MinGW/LLVM);
resource compilation falls back to rc.exe / windres / llvm-rc automatically;
MinGW artifacts link the runtime statically and do not depend on extra DLLs
such as libstdc++/libwinpthread.

### Linux

```bash
cmake -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build -j $(nproc --all)
```

The build produces `libOsiris.so` in `build/Source/`.

## Injector usage (zero arguments)

Double-click `Injector.exe` — the whole flow is automatic (no command-line
arguments are required or accepted; console output is diagnostic logging):

```
double-click
  -> the executable embeds a requireAdministrator manifest, so a UAC prompt
     appears without "Run as administrator"; if denied, it aborts and loads nothing
  -> locate cs2.exe:
      already running -> wait for client.dll, then inject immediately
      not running     -> launch CS2 via Steam (steam://rungameid/730)
                         wait for cs2.exe to appear (no timeout, 3 s refresh)
                         wait for client.dll (no timeout, 3 s refresh), then inject
  -> inject the embedded Osiris.dll
  -> keep the window open with the result (press any key to close)
```

Early-injection guard (no window-focus check): injection happens only once
`client.dll` is present in the process — at the "international / China region"
selection dialog engine2.dll is already loaded but client.dll is not, so that
phase is naturally excluded. If you get stuck at the region dialog the injector
waits indefinitely (checking client.dll every 3 seconds) and injects
automatically once you pick a region and the real game starts.

### Injector developer switches (environment variables, invisible to normal users)

| Variable | Effect |
|---|---|
| `INJECTOR_NO_ELEVATE=1` | Skip the UAC elevation (automated testing; do not set for normal use) |
| `INJECTOR_TARGET_EXE=<name>` | Override the target process name (default `cs2.exe`, testing) |
| `INJECTOR_NO_LAUNCH=1` | Do not launch via Steam when the target is missing; only wait |
| `INJECTOR_DRY_RUN=1` | Run the whole flow but skip the actual injection |

### How manual mapping works

1. Parse the PE headers (validate x64 / PE32+) and `VirtualAllocEx` RWX memory
   inside the target sized `SizeOfImage` (prefer the preferred base address,
   fall back to any address).
2. Rebuild the image section by section, VA-aligned, and write it into the target process.
3. Apply `.reloc` relocations (DIR64 / HIGHLOW) when the actual base differs
   from the preferred base.
4. Assemble the parameter block plus the position-independent payload
   (`Shellcode.asm`/`Shellcode.nasm`, PIC, no relocations) and write it into the target.
5. `CreateRemoteThread` runs the payload in the target:
   - resolves the import table inside the target via `ntdll!LdrLoadDll` /
     `LdrGetProcedureAddress` (equivalent to LoadLibrary, guaranteeing correct addresses);
   - registers the exception directory (`RtlAddFunctionTable`, x64 SEH);
   - invokes TLS callbacks;
   - calls the entry point (`DllMain`, DLL_PROCESS_ATTACH).
6. Read the payload return status, free the payload memory, keep the mapped image.

### Notes

- **Automatic elevation (UAC)**: when not running as administrator, the injector
  requests elevation through UAC and restarts itself. **If elevation is denied
  it aborts without loading anything.**
- **No rights, no load**: even when elevated, if `OpenProcess` on the target is
  still denied (protected process etc.) the injector aborts without injecting.
- **Window persistence**: after finishing, an interactive console stays on
  "Press any key to continue..."; when output is redirected (CI) the wait is
  skipped automatically.
- **Anti-cheat risk**: CS2 ships with VAC and injection can be detected.
  For learning and personal testing only.
- If the mapped DLL entry point (DllMain) returns `FALSE`, the injector reports
  failure and frees the image.
- A payload timeout (60 seconds) is treated as failure, so a stuck DllMain
  cannot hang the injector.

## Loading / Injecting into game process

### Windows

Counter-Strike 2 blocks the LoadLibrary injection method, so the bundled
**Injector.exe** manual-maps (reflective injection) the embedded **Osiris.dll**
into the game — see the usage section above.

Third-party injectors **Xenos** and **Extreme Injector** are known to be **detected** by VAC.

### Linux

You can simply run the following script in the directory containing **libOsiris.so**:

    sudo gdb -batch-silent -p $(pidof cs2) -ex "call (void*)dlopen(\"$PWD/libOsiris.so\", 2)"

However, this injection method might be detected by VAC as gdb is visible under **TracerPid** in `/proc/$(pidof cs2)/status` for the duration of the injection.

## GitHub Actions builds (zero local setup)

After pushing or triggering the workflow manually, go to Actions → the run →
**Artifacts** and download `Osiris-Release-MSVC-windows-2022` (**no folder
nesting inside the zip**):
- `Osiris.dll`
- `Injector.exe` (embeds the matching Osiris.dll; ready to inject)

`windows.yml` has three jobs covering every toolchain: msbuild
(MSVC/ClangCL × Debug/Release), cmake (same matrix plus tests) and mingw
(MSYS2 + NASM + Ninja).

## FAQ

### Where are the settings stored on disk?

In a configuration file `default.cfg` inside `%appdata%\OsirisCS2\configs` directory on Windows and `$HOME/OsirisCS2/configs` on Linux.

On Windows the file is written atomically: a temporary `default.cfg.new` is
written first and then renamed over `default.cfg`; changes made in-game are
saved automatically.

## Files (Injector directory)

| File | Description |
|---|---|
| `Injector.cpp` | Zero-argument entry point, automatic elevation, automatic injection, diagnostics |
| `ManualMapper.cpp/.h` | PE parsing, relocations, import resolution, payload assembly |
| `Shellcode.asm` / `Shellcode.nasm` | PIC payload executed inside the target (MASM for MSVC/ClangCL, NASM for MinGW/LLVM; identical semantics) |
| `Injector.ico` / `Injector.rc` | Executable icon |
| `resource.h` | Resource ID (`IDR_EMBEDDED_DLL`), kept in sync with the generator |
| `Injector.manifest` | requireAdministrator UAC manifest (embedded by the CMake/MinGW path; MSBuild uses linker flags) |
| `CMakeLists.txt` | Injector CMake target (toolchain-adaptive assembly/resources/manifest) |
| `make_embedded_rc.cmake` | Generates the one-line resource script (RCDATA pointing at Osiris.dll) for the CMake path |
| `EmbeddedDll.rc` | Generated: one-line RCDATA resource script (do not edit) |
| `make_icon.ps1` | Icon generation script (rerun after changing the icon, then rebuild) |
| `dump_modules.ps1` | Dumps the target process module list (development aid) |

## License

> Copyright (c) 2018-2025 Daniel Krupiński

This project is licensed under the [MIT License](https://opensource.org/licenses/mit-license.php) - see the [LICENSE](https://github.com/danielkrupinski/Osiris/blob/master/LICENSE) file for details.
