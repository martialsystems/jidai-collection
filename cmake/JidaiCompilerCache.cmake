# Copyright (c) 2026 Martial Systems LLC. All rights reserved.
#
# Optional sccache/ccache compiler launcher, auto-detected. Include this before
# project() in a top-level CMakeLists. It is a no-op if the user already set
# CMAKE_CXX_COMPILER_LAUNCHER, or if the generator is not Ninja or Makefiles.
# MSVC + a compiler cache: /Zi writes a shared PDB the cache cannot reuse, so
# embed debug info (/Z7) instead (CMake >= 3.25).

if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
endif()

option(JIDAI_USE_CCACHE "Use sccache or ccache as the compiler launcher when one is found (Ninja/Makefile generators)" ON)
if(JIDAI_USE_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER AND CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
  find_program(JIDAI_CCACHE_PROGRAM NAMES sccache ccache)
  if(JIDAI_CCACHE_PROGRAM)
    message(STATUS "Compiler cache: ${JIDAI_CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${JIDAI_CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${JIDAI_CCACHE_PROGRAM}")
  endif()
endif()
