#!/bin/bash
# build.sh: the Linux twin of build.bat. XDK symbols on the first build, the recompile, then CMake (preset
# linux-release, or the one named: ./build.sh linux-debug). Needs clang, lld, cmake, ninja, git and Python 3.
set -e
GAME_TITLE="ROTK Xbox PC port"
GAME_XBE=default.xbe
GAME_EXE=ROTKXbox
PRESET=${1:-linux-release}
cd "$(dirname "$(realpath "$0")")"
OUT=$PWD/out/build/$PRESET

fail() {
    echo
    echo "  BUILD FAILED. $1"
    echo
    exit 1
}

echo
echo "  $GAME_TITLE - build ($PRESET)"
echo "  ================================================"
echo

if [ ! -f "assets/$GAME_XBE" ]; then
    echo "  Put the full game files in the assets folder first:"
    echo "    $PWD/assets"
    echo "  It needs $GAME_XBE with the game's folders beside it."
    fail "assets/$GAME_XBE is missing."
fi

echo "[1/3] Toolchain"
missing=""
for t in clang++ ld.lld cmake ninja git python3; do command -v $t >/dev/null || missing="$missing $t"; done
[ -z "$missing" ] || fail "Missing:$missing (Fedora: sudo dnf install clang lld cmake ninja-build git python3)."
echo "      $(clang++ --version | head -1)"

echo "[2/3] XDK symbols"
cmake --preset "$PRESET" >/dev/null || fail "cmake --preset $PRESET did not configure."
if grep -q '^\[symbols\]' config/game.toml 2>/dev/null; then
    echo "      config/game.toml already written"
else
    cmake --build "$OUT" --target xbe_symbols >/dev/null || fail "xbe_symbols did not build."
    # the configure step wrote where the SDK is
    [ -f "$OUT/sdk_dir.txt" ] || fail "$OUT/sdk_dir.txt was not written, update the SDK."
    SDK=$(head -1 "$OUT/sdk_dir.txt")
    mkdir -p config
    "$OUT/xbe_symbols" "assets/$GAME_XBE" "$SDK/xdk" --out config/game.toml || fail "xbe_symbols failed."
fi

echo "[3/3] Game code"
echo "      first build recompiles $GAME_XBE, expect 30 to 90 minutes ..."
cmake --build "$OUT" || fail "Read the last message above."

echo
echo "  $GAME_TITLE built."
echo "  $OUT/$GAME_EXE"
echo
