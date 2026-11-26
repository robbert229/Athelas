#!/bin/bash
# appimage.sh: the Linux release, as release.bat is the Windows one. Builds the game in an Ubuntu 22.04 container
# (appimage/Containerfile, so it runs on any distribution from 2022 on and on the Steam Deck), then packs
#   release/ROTKXbox-<version>-x86_64.AppImage         the exe, lang\, LICENSE.txt and THIRD_PARTY.txt (both must
#                                                      ship with it), AppRun (appimage/AppRun: where the game files,
#                                                      settings and saves are), the .desktop file and the icon
#   release/ROTKXbox-<version>-x86_64.AppImage.sha256  as sha256sum writes it
#   release/ROTKXbox-<version>-x86_64.debug            the exe's debug information, for reading a crash dump
# No game files go in. Needs podman (from a distrobox or toolbox it uses the host's) and the game in assets/ as
# build.sh does; the container image, CMake's downloads and appimagetool need the network the first time.
set -e
GAME_EXE=ROTKXbox
PRESET=linux-appimage
IMAGE=localhost/athelas-rotkxbox-appimage:jammy-llvm22
# appimagetool and the AppImage runtime it puts in front, pinned with their SHA-256 (GitHub's release digests)
APPIMAGETOOL_URL=https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage
APPIMAGETOOL_SHA256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
RUNTIME_URL=https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64
RUNTIME_SHA256=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
# the newest glibc the exe may ask for: Ubuntu 22.04's
GLIBC_MAX=2.35

cd "$(dirname "$(realpath "$0")")"
HERE=$PWD
ROOT=$(realpath ../../..)
OUT=$HERE/out/build/$PRESET
TOOLS=$HERE/out/appimage-tools

fail() {
    echo
    echo "  APPIMAGE FAILED. $1"
    echo
    exit 1
}

# ---- outside: the container built and this script run again inside it ----
if [ "$1" != "--in-container" ]; then
    [ -f assets/default.xbe ] || fail "Put the full game files in $HERE/assets first (as for build.sh)."
    if command -v podman >/dev/null; then
        PODMAN=(podman)
    elif command -v distrobox-host-exec >/dev/null; then
        PODMAN=(distrobox-host-exec podman)
    else
        fail "podman is needed (Fedora: sudo dnf install podman; Ubuntu: sudo apt install podman)."
    fi
    echo "[1/4] Build container ($IMAGE)"
    "${PODMAN[@]}" build -q -t "$IMAGE" -f appimage/Containerfile appimage >/dev/null ||
        fail "The container image didn't build."
    # the source, the game files and git's folder (a worktree's lives elsewhere) at the same paths inside, so the
    # build folder's absolute paths hold either side
    mounts=(-v "$ROOT:$ROOT")
    for extra in "$(realpath assets)" "$(realpath "$(git -C "$ROOT" rev-parse --git-common-dir)")"; do
        case "$extra/" in "$ROOT"/*) ;; *) mounts+=(-v "$extra:$extra:ro") ;; esac
    done
    exec "${PODMAN[@]}" run --rm --userns=keep-id --security-opt label=disable "${mounts[@]}" -w "$HERE" \
        -e CI=1 -e HOME=/tmp "$IMAGE" ./appimage.sh --in-container
fi

# ---- inside ----
echo "[2/4] Game build ($PRESET)"
./build.sh "$PRESET" || exit 1
EXE=$OUT/$GAME_EXE
SDK=$(head -1 "$OUT/sdk_dir.txt")

# nothing newer than GLIBC_MAX, and no libstdc++ or libgcc_s to find on the player's system
newest=$(llvm-readelf --dyn-syms "$EXE" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -Vu | tail -1)
[ "$(printf '%s\n%s\n' "$newest" "$GLIBC_MAX" | sort -V | tail -1)" = "$GLIBC_MAX" ] ||
    fail "$GAME_EXE asks for glibc $newest, newer than $GLIBC_MAX."
if llvm-readelf -d "$EXE" | grep -E 'NEEDED.*lib(stdc\+\+|gcc_s)'; then fail "$GAME_EXE isn't linked with libstdc++ inside."; fi

echo "[3/4] appimagetool"
mkdir -p "$TOOLS"
fetch() {  # <url> <sha256> <file>
    if [ ! -f "$3" ] || ! echo "$2  $3" | sha256sum -c --status; then
        curl -fsSL -o "$3.part" "$1" || fail "Couldn't download $1."
        echo "$2  $3.part" | sha256sum -c --status || fail "$1 isn't the file pinned in appimage.sh."
        mv "$3.part" "$3"
    fi
}
fetch "$APPIMAGETOOL_URL" "$APPIMAGETOOL_SHA256" "$TOOLS/appimagetool"
fetch "$RUNTIME_URL" "$RUNTIME_SHA256" "$TOOLS/runtime-x86_64"
chmod +x "$TOOLS/appimagetool"

echo "[4/4] AppImage"
version=$("$EXE" --version | awk '{print $NF}')
[ -n "$version" ] || fail "$GAME_EXE --version said nothing."
name=$GAME_EXE-$version-x86_64
APPDIR=$OUT/AppDir
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps" \
    "$APPDIR/usr/share/doc/rotkxbox" release
# the debug information kept out (22 MB of the exe), linked by name so a debugger finds it beside the AppImage
llvm-objcopy --only-keep-debug "$EXE" "release/$name.debug"
llvm-objcopy --strip-debug --add-gnu-debuglink="release/$name.debug" "$EXE" "$APPDIR/usr/bin/$GAME_EXE"
# the subtitles' translations are read beside the exe
[ -d lang ] && cp -r lang "$APPDIR/usr/bin/lang"
cp "$SDK/LICENSE" "$APPDIR/usr/share/doc/rotkxbox/LICENSE.txt"
cp "$SDK/THIRD_PARTY.txt" "$APPDIR/usr/share/doc/rotkxbox/THIRD_PARTY.txt"
install -m 755 appimage/AppRun "$APPDIR/AppRun"
cp appimage/rotkxbox.desktop "$APPDIR/rotkxbox.desktop"
cp appimage/rotkxbox.desktop "$APPDIR/usr/share/applications/rotkxbox.desktop"
cp "$ROOT/launcher/art/icon_256.png" "$APPDIR/rotkxbox.png"
cp "$ROOT/launcher/art/icon_256.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/rotkxbox.png"
ln -s rotkxbox.png "$APPDIR/.DirIcon"
desktop-file-validate "$APPDIR/rotkxbox.desktop" || fail "rotkxbox.desktop isn't valid."

rm -f "release/$name.AppImage" "release/$name.AppImage.sha256"
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 "$TOOLS/appimagetool" --no-appstream --runtime-file "$TOOLS/runtime-x86_64" \
    "$APPDIR" "release/$name.AppImage" >"$OUT/appimagetool.log" 2>&1 || {
    cat "$OUT/appimagetool.log"
    fail "appimagetool failed."
}
(cd release && sha256sum "$name.AppImage" >"$name.AppImage.sha256")

echo
echo "  AppImage: $HERE/release/$name.AppImage ($(du -h "release/$name.AppImage" | cut -f1))"
echo "  Put the game (an .iso, or a folder named assets) beside it, or run it with --game <folder or .iso>."
echo "  Settings, saves and logs: ~/.local/share/athelas/rotk-xbox"
echo
