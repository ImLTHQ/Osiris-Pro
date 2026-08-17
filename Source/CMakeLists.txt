add_library(Osiris SHARED dllmain.cpp)

set_target_properties(Osiris PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  C_VISIBILITY_PRESET hidden
  POSITION_INDEPENDENT_CODE ON)

target_include_directories(Osiris PRIVATE "${CMAKE_SOURCE_DIR}/Source")
target_link_libraries(Osiris PRIVATE ${CMAKE_DL_LIBS})

if(WIN32)
  if(MSVC)
    target_sources(Osiris PRIVATE Platform/Windows/Syscalls/WindowsSyscall.asm)
  else()
    target_sources(Osiris PRIVATE Platform/Windows/Syscalls/WindowsSyscall.nasm)
    # MinGW-w64 provides GUID constants (FOLDERID_*, ...) in libuuid.
    target_link_libraries(Osiris PRIVATE uuid)
  endif()
endif()

if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # Manual-mapped payloads must be self-contained: the target process has no
  # libstdc++/libgcc/libwinpthread DLLs. -static links the whole MinGW runtime
  # statically (a -Bstatic/-Bdynamic pair is NOT enough: the posix-threads g++
  # spec injects its own dynamic -lwinpthread after user flags).
  target_link_options(Osiris PRIVATE -static)
endif()

if(MSVC)
  set_target_properties(Osiris PROPERTIES MSVC_RUNTIME_LIBRARY "$<$<CONFIG:Debug>:MultiThreadedDebugDLL>")
  string(REGEX REPLACE "/EH[a-z]+" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
  target_sources(Osiris PRIVATE $<$<CONFIG:Release>:Platform/Windows/CRTWindows.cpp>)
  target_compile_options(Osiris PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 $<$<CONFIG:Release>:/sdl- /GS->>)
  target_compile_definitions(Osiris PRIVATE $<$<CONFIG:Release>:_USE_STD_VECTOR_ALGORITHMS=0>)
  target_link_options(Osiris PRIVATE $<$<CONFIG:Release>:/nodefaultlib /ENTRY:"DllMain">)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(Osiris PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall>)
    target_link_options(Osiris PRIVATE LINKER:--no-undefined)
    if(UNIX)
      # -z noexecstack is an ELF-only linker option; MinGW (PE) rejects it.
      target_link_options(Osiris PRIVATE LINKER:-z,noexecstack)
    endif()
  endif()

  target_compile_options(Osiris PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-missing-braces>)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 14.0)
  target_compile_options(Osiris PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-dangling-reference>) # false positive
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  if(UNIX)
    target_compile_options(Osiris PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-stack-protector -fno-exceptions -fno-asynchronous-unwind-tables -fno-unwind-tables>)
    target_link_options(Osiris PRIVATE -nostdlib)
    target_link_libraries(Osiris PRIVATE c)

    if(CMAKE_STRIP)
      add_custom_command(TARGET Osiris POST_BUILD COMMAND ${CMAKE_STRIP} "$<TARGET_FILE:Osiris>")
    endif()
  endif()
endif()
