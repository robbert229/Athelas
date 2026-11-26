#!/bin/bash
# wincheck.sh <source>...: a Windows syntax check from Linux. clang for x86_64-w64-mingw32 against Fedora's mingw64
# headers (sudo dnf install mingw64-gcc-c++ mingw64-headers), with the include folders and definitions of a Windows
# build of the runtime and the ports. Uses the dependencies sdk/smoke/build downloaded (configure it first) and, for
# a port's sources, the stand in of its last -DXBE_PORT configure. Exit 1 when any file has errors.
set -u
[ $# -gt 0 ] || { sed -n 2,5p "$0"; exit 2; }
H=$(dirname "$(realpath "$0")"); R=$(realpath "$H/../.."); B=${SMOKE_BUILD:-$H/build}; D=$B/_deps
MW=${MINGW_ROOT:-/usr/x86_64-w64-mingw32/sys-root/mingw}
[ -d "$D/imgui-src" ] || { echo "configure sdk/smoke first (README.md)"; exit 2; }
[ -f "$MW/include/windows.h" ] || { echo "no mingw64 headers in $MW"; exit 2; }
bad=0
for f in "$@"; do
    out=$(clang++ --target=x86_64-w64-mingw32 -std=c++20 -fsyntax-only -march=x86-64-v2 -fno-strict-aliasing -w \
        -nostdinc++ -isystem "$MW/include/c++" -isystem "$MW/include/c++/x86_64-w64-mingw32" --sysroot="$MW" \
        -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -DXBE_HAS_UPNP=1 -DMINIUPNP_STATICLIB \
        -DXBE_LOCKSTEP=1 -DXBE_RENDER_THREAD=0 -I"$R/sdk/runtime" -I"$B/port_gen" -I"$B/gen" \
        -isystem "$D/imgui-src" -isystem "$R/sdk/third_party/vulkan/include" -I"$R/sdk/third_party/vma" \
        -I"$D/tomlplusplus-src/include" -I"$D/sdl3-src/include" -I"$D/miniupnpc-src/miniupnpc/include" \
        -I"$D/glslang-src" -I"$D/spirv-tools-src/include" -I"$(dirname "$(realpath "$f")")/.." "$f" 2>&1)
    if [ $? -eq 0 ]; then echo "ok     $f"; else echo "ERRORS $f"; echo "$out" | grep error | head -5; bad=1; fi
done
exit $bad
