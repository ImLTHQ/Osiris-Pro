cmake_minimum_required(VERSION 3.28)
project(Osiris LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Toolchain-portable assembly: MSVC/clang-cl use MASM (ml64), MinGW/LLVM use
# NASM. Linux uses gas (AT&T syntax) as before.
if(WIN32)
  if(MSVC)
    enable_language(ASM_MASM)
  else()
    enable_language(ASM_NASM)
  endif()
elseif(UNIX)
  enable_language(ASM-ATT)
endif()

set(ENABLE_TESTS CACHE STRING "List of test types to enable e.g. \"unit;functional\"")

add_subdirectory(Source)

if(WIN32)
  add_subdirectory(Injector)
endif()

# Unified output directory: Osiris.dll and Injector.exe share one folder,
# mirroring the MSBuild layout (repo-root x64\<Configuration>).
if(CMAKE_CONFIGURATION_TYPES)
  foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
    set_target_properties(Osiris Injector PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/x64/${config}")
  endforeach()
else()
  set_target_properties(Osiris Injector PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/x64")
endif()

if(ENABLE_TESTS)
  enable_testing()
  add_subdirectory(Tests)
endif()
