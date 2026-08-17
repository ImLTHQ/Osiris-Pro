# Manual-map injector for Windows. The Osiris.dll payload is embedded as an
# RCDATA resource whose script is generated at build time with absolute paths
# (make_embedded_rc.cmake), so no resource compiler has to guess relative-path
# resolution:
#   - MSVC/clang-cl: MASM (Shellcode.asm) + rc.exe; UAC via /MANIFESTUAC
#   - MinGW/LLVM:    NASM (Shellcode.nasm) + windres/llvm-rc; UAC via an
#                     RT_MANIFEST resource (Injector.manifest)
set(injector_rc "${CMAKE_CURRENT_BINARY_DIR}/EmbeddedDll.rc")
set(injector_manifest "")
if(MSVC)
  set(injector_asm "Shellcode.asm")
else()
  set(injector_asm "Shellcode.nasm")
  set(injector_manifest "${CMAKE_CURRENT_SOURCE_DIR}/Injector.manifest")
endif()

set(injector_rc_command
  ${CMAKE_COMMAND}
  "-DICON_PATH=${CMAKE_CURRENT_SOURCE_DIR}/Injector.ico"
  "-DDLL_PATH=$<TARGET_FILE:Osiris>"
  "-DMANIFEST_PATH=${injector_manifest}"
  "-DOUTPUT=${injector_rc}"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/make_embedded_rc.cmake")

add_executable(Injector
  Injector.cpp
  ManualMapper.cpp
  "${injector_asm}"
  "${injector_rc}"
)

set_source_files_properties("${injector_rc}" PROPERTIES GENERATED TRUE)

# Osiris.dll must exist before the resource script is generated / compiled.
add_dependencies(Injector Osiris)

if(CMAKE_CONFIGURATION_TYPES)
  # Multi-config generators share one binary directory: a single OUTPUT file
  # would go stale on in-place configuration switches (timestamp comparison
  # across configurations). Regenerate before every build instead - always
  # correct, at the cost of a resource recompile + relink.
  add_custom_command(TARGET Injector PRE_BUILD
    COMMAND ${injector_rc_command}
    VERBATIM
    COMMENT "Generating ${injector_rc} (embeds Osiris.dll as RCDATA)")
else()
  # Single-config generators: fully incremental via INPUT/OUTPUT timestamps.
  add_custom_command(
    OUTPUT "${injector_rc}"
    COMMAND ${injector_rc_command}
    DEPENDS Osiris "$<TARGET_FILE:Osiris>"
    VERBATIM
    COMMENT "Generating ${injector_rc} (embeds Osiris.dll as RCDATA)")
endif()

target_include_directories(Injector PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(Injector PRIVATE UNICODE _UNICODE)

if(MSVC)
  # UAC: requireAdministrator (mirrors the vcxproj EnableUAC settings).
  target_link_options(Injector PRIVATE
    "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
  # CMake defaults MSVC Debug to /INCREMENTAL, which routes the codeStart/
  # codeEnd address references through ILT thunks and breaks ShellcodeSize()
  # (codeEnd - codeStart comes out negative). Disable it for this target.
  target_link_options(Injector PRIVATE "/INCREMENTAL:NO")
else()
  # MinGW/LLVM: console apps using wmain() need the Unicode startup entry
  # point (MSVC picks it up automatically).
  target_link_options(Injector PRIVATE -municode)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # -static keeps the standalone exe free of libstdc++/libgcc/libwinpthread
    # DLL dependencies on vanilla Windows.
    target_link_options(Injector PRIVATE -static)
  endif()
endif()
