#!/bin/bash
# Installs Chaos Maker for your user (no administrator rights needed): the app in
# ~/.local/opt/ChaosMaker, the ChaosMaker command in ~/.local/bin, a menu entry and its icon,
# then the sample kits (get-sample-kits.sh; --no-kits skips them, run it later).
# Run it from the unpacked folder: ./install-app.sh. Running it again updates the app; until the
# new copy is complete, the old one stays as it was. Remove it with ./uninstall-app.sh (your
# settings and projects stay).
set -euo pipefail

id=io.github.chandandasgit.ChaosMaker
here="$(cd "$(dirname "$0")" && pwd)"
target="$HOME/.local/opt/ChaosMaker"
data="${XDG_DATA_HOME:-$HOME/.local/share}"
bin="$HOME/.local/bin"
apps="$data/applications"
icons="$data/icons/hicolor/scalable/apps"
kits=1
[ "${1:-}" = "--no-kits" ] && kits=0

if [ ! -x "$here/app/AppRun" ]; then
    echo "Run this from the unpacked ChaosMaker folder (app/AppRun is missing)." >&2
    exit 1
fi

# The app. The new copy goes next to the old one first and replaces it only once it's complete,
# so a failed copy (disk full...) leaves the old one working.
next="$target.new"
previous="$target.old"
restore() {
    rm -rf "$next"
    # (stopped between the two moves: the old copy goes back)
    if [ ! -e "$target" ] && [ -e "$previous" ]; then
        mv "$previous" "$target"
    fi
}
swapped=0
trap 'status=$?
if [ $status -ne 0 ] && [ $swapped = 0 ]; then
    restore
    echo "Not installed: the app you had is unchanged." >&2
fi' EXIT

rm -rf "$next" "$previous"
mkdir -p "$next"
cp -a "$here/app/." "$next/"
if [ ! -x "$next/AppRun" ] || [ ! -x "$next/usr/bin/appChaosMaker" ] \
    || [ "$(cd "$here/app" && find . | wc -l)" != "$(cd "$next" && find . | wc -l)" ]; then
    echo "The copy is incomplete." >&2
    exit 1
fi
if [ -e "$target" ]; then
    mv "$target" "$previous"
fi
mv "$next" "$target"
swapped=1
rm -rf "$previous" # (a running copy carries on: its files stay until it closes)

# Menu entry and icon. Desktop files quote with "…"; inside, \ " ` $ are escaped.
quoted="\"$(printf '%s' "$target/AppRun" | sed -e 's/[\\"`$]/\\&/g')\""
sedEscape() { printf '%s' "$1" | sed -e 's/[\\&|]/\\&/g'; }
mkdir -p "$apps" "$icons"
sed -e "s|^Exec=[^ ]*|Exec=$(sedEscape "$quoted")|" "$target/usr/share/applications/$id.desktop" > "$apps/$id.desktop"
chmod 644 "$apps/$id.desktop"
install -m644 "$target/usr/share/icons/hicolor/scalable/apps/$id.svg" "$icons/$id.svg"
command -v update-desktop-database >/dev/null && update-desktop-database -q "$apps" || true

# The ChaosMaker command
mkdir -p "$bin"
ln -sf "$target/AppRun" "$bin/ChaosMaker"

echo "Chaos Maker is installed."
echo "  Program : $target"
echo "  Command : $bin/ChaosMaker"
echo "  Menu    : $apps/$id.desktop"
case ":$PATH:" in
    *":$bin:"*) ;;
    *) echo "Note: $bin is not on your PATH, so the ChaosMaker command works only once you add it." ;;
esac

# JACK is the system's own (it must match the sound server): only checked here, by asking the
# loader whether the app finds it (PipeWire's copy is often outside the ldconfig cache).
if LD_LIBRARY_PATH="$target/usr/lib" ldd "$target/usr/bin/appChaosMaker" 2>/dev/null | grep -q 'libjack\.so\.0 => not found'; then
    echo "Note: JACK was not found. Chaos Maker plays through JACK: install PipeWire's JACK support"
    echo "      (pipewire-jack, or pipewire-jack-audio-connection-kit) or JACK itself."
fi

if [ "$kits" = 1 ]; then
    "$here/get-sample-kits.sh" || echo "The sample kits were not installed: run $here/get-sample-kits.sh later." >&2
fi
