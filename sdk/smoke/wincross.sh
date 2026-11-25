#!/bin/bash
# wincross.sh [ninja target...]: the smoke test built and linked for Windows from Linux, through XbeRecomp.cmake's
# Windows path (mingw-clang.cmake: clang, lld and Fedora's mingw64 sysroot). The link proves every Windows symbol
# still has a definition. Builds xbe_smoke.exe, and xbe_port_check.exe with XBE_PORT=<port project> set. With Wine,
# RUN=1 then starts xbe_smoke.exe with no arguments. Writes sdk/smoke/build-win (WIN_BUILD to change).
set -eu
H=$(dirname "$(realpath "$0")")
B=${WIN_BUILD:-$H/build-win}
MW=${MINGW_ROOT:-/usr/x86_64-w64-mingw32/sys-root/mingw}
[ -f "$MW/include/windows.h" ] || {
    echo "no mingw64 sysroot in $MW: sudo dnf install mingw64-gcc-c++ mingw64-headers mingw64-crt mingw64-winpthreads-static"
    exit 2
}
port=()
[ -n "${XBE_PORT:-}" ] && port=(-DXBE_PORT="$(realpath "$XBE_PORT")")
mkdir -p "$B"
cmake -S "$H" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$H/mingw-clang.cmake" \
    -DXBE_MINGW_ROOT="$MW" "${port[@]}" >"$B/configure.log" 2>&1 || { tail -30 "$B/configure.log"; exit 1; }
targets=("$@")
if [ ${#targets[@]} -eq 0 ]; then
    targets=(xbe_smoke)
    [ -n "${XBE_PORT:-}" ] && targets+=(xbe_port_check)
fi
ninja -C "$B" "${targets[@]}"
if [ "${RUN:-0}" = 1 ]; then
    cd "$B" && WINEDEBUG=${WINEDEBUG:--all} wine ./xbe_smoke.exe
fi
