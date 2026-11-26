#!/bin/sh
# rotkxbox.sh: how the Linux release starts the game, the AppImage's AppRun and the .tar.xz's start script. The exe
# keeps its settings, saves, logs and caches beside itself unless told otherwise; an AppImage is read-only and a
# tarball may be unpacked anywhere, so both keep them in one folder in the home instead:
#   $ROTKXBOX_DATA, else $XDG_DATA_HOME/athelas/rotk-xbox (~/.local/share/athelas/rotk-xbox)
# A portable.txt beside the AppImage, or in the tarball's folder, keeps them in that folder instead.
# The game (the Xbox disc as an .iso, or its extracted files with default.xbe at the top) is, in this order:
#   --game <folder or .iso>, $ROTKXBOX_GAME, a folder "assets" beside the AppImage (in the tarball's folder), that
#   folder itself when an .iso or default.xbe is in it. An argument the player gives (--config, --saves, ...) wins.
# --install-desktop puts the game in the desktop's menu (and so in Steam's "Add a Non-Steam Game" list);
# --remove-desktop takes it out again.
here=$(dirname "$(readlink -f "$0")")
if [ -n "$APPIMAGE" ] && [ "$APPDIR" = "$here" ]; then
    exe=$here/usr/bin/ROTKXbox
    beside=$(dirname "$APPIMAGE")
    self=$APPIMAGE
else
    exe=$here/ROTKXbox
    [ -x "$exe" ] || exe=$here/usr/bin/ROTKXbox  # an AppImage's extracted folder, run as it is
    beside=$here
    self=$here/rotkxbox.sh
fi

share=${XDG_DATA_HOME:-$HOME/.local/share}
case "$1" in
--install-desktop)
    case "$self" in *[\"\`\$\\%]*)
        echo "rotkxbox: $self has a character a .desktop file can't hold (\" \` \$ \\ %): move it and try again." >&2
        exit 1 ;;
    esac
    mkdir -p "$share/applications" "$share/icons/hicolor/256x256/apps" || exit 1
    cp "$here/rotkxbox.png" "$share/icons/hicolor/256x256/apps/rotkxbox.png" || exit 1
    sed "s|^Exec=.*|Exec=\"$self\"|" "$here/rotkxbox.desktop" >"$share/applications/rotkxbox.desktop" || exit 1
    echo "rotkxbox: in the menu as \"ROTK Xbox PC port\", starting $self"
    exit 0 ;;
--remove-desktop)
    rm -f "$share/applications/rotkxbox.desktop" "$share/icons/hicolor/256x256/apps/rotkxbox.png"
    echo "rotkxbox: taken out of the menu"
    exit 0 ;;
esac

if [ -f "$beside/portable.txt" ]; then
    data=$beside
else
    data=${ROTKXBOX_DATA:-$share/athelas/rotk-xbox}
fi
mkdir -p "$data/logs" || exit 1

given() {
    for a in "$@"; do [ "$a" = "$want" ] && return 0; done
    return 1
}
for pair in "--config:$data/settings.toml" "--saves:$data/saves" "--log:$data/logs/rotkx.log" \
            "--shader-cache:$data/shadercache" "--screenshots:$data/Screenshots" "--textures:$data/textures"; do
    want=${pair%%:*}
    given "$@" || set -- "$@" "$want" "${pair#*:}"
done

want=--game
if ! given "$@"; then
    if [ -n "$ROTKXBOX_GAME" ]; then
        game=$ROTKXBOX_GAME
    elif [ -d "$beside/assets" ]; then
        game="$beside/assets"
    else
        game="$beside/assets"  # not there: the exe then says plainly where it looked
        for f in "$beside/default.xbe" "$beside"/*.iso "$beside"/*.ISO; do
            [ -f "$f" ] && game=$beside && break
        done
    fi
    set -- "$@" --game "$game"
fi

exec "$exe" "$@"
