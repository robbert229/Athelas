# A Windows build from Linux (wincross.sh): clang and lld for x86_64-w64-mingw32 against Fedora's mingw64 sysroot
# (sudo dnf install mingw64-gcc-c++ mingw64-headers mingw64-crt mingw64-winpthreads-static). For checks only: the
# exe is linked statically (libstdc++ and winpthread inside) so it runs under Wine without the sysroot's DLLs.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(XBE_MINGW_ROOT /usr/x86_64-w64-mingw32/sys-root/mingw CACHE PATH "Fedora's mingw64 sysroot")

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET x86_64-w64-mingw32)
set(CMAKE_CXX_COMPILER_TARGET x86_64-w64-mingw32)
set(CMAKE_RC_COMPILER llvm-windres)

set(CMAKE_FIND_ROOT_PATH ${XBE_MINGW_ROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# clang's MinGW driver doesn't add winpthread for a static libstdc++ as gcc does; lld finds it wherever it is named
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -static -lpthread")
