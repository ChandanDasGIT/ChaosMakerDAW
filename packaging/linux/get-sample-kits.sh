#!/bin/bash
# Downloads Chaos Maker's sample kits (the acoustic drum kit and the grand piano: a 170 MB download, 550 MB unpacked)
# into ~/.local/share/ChaosMaker/sample-kits, where the app finds them. install-app.sh runs it;
# with the AppImage, run it once yourself. Running it again does nothing once the kits are there.
# The download is checked against its SHA-256 before anything is unpacked.
#   KITS_URL / KITS_SHA256 replace the address and checksum (tests).
set -euo pipefail

release=sample-kits-1
url="${KITS_URL:-https://github.com/ChandanDasGIT/ChaosMakerDAW/releases/download/$release/ChaosMaker-sample-kits.tar.xz}"
sha256="${KITS_SHA256:-2296072c00da0daa9e71738cfe6240c603f5fd0853ac5db6ec573fe684e05dff}"
target="${XDG_DATA_HOME:-$HOME/.local/share}/ChaosMaker/sample-kits"

if [ -f "$target/.release" ] && [ "$(cat "$target/.release")" = "$release" ]; then
    echo "The sample kits are already in $target."
    exit 0
fi
if [ -z "$sha256" ]; then
    echo "No checksum for the sample kits: nothing downloaded." >&2
    exit 1
fi

work="$(mktemp -d "${TMPDIR:-/tmp}/chaosmaker-kits-XXXXXX")"
trap 'rm -rf "$work"' EXIT
echo "Downloading the sample kits (about 170 MB; 550 MB once unpacked)..."
if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 --progress-bar -o "$work/kits.tar.xz" "$url"
elif command -v wget >/dev/null 2>&1; then
    wget -q --show-progress -O "$work/kits.tar.xz" "$url"
else
    echo "Neither curl nor wget is installed: install one and run this again." >&2
    exit 1
fi
echo "$sha256  $work/kits.tar.xz" | sha256sum -c --quiet - \
    || { echo "The download is damaged or not the expected file: nothing unpacked." >&2; exit 1; }

mkdir -p "$work/unpacked"
tar -xJf "$work/kits.tar.xz" -C "$work/unpacked"
[ -d "$work/unpacked/sample-kits" ] || { echo "The archive holds no sample-kits folder." >&2; exit 1; }
echo "$release" > "$work/unpacked/sample-kits/.release"

# The new kits replace the old only once they are complete.
mkdir -p "$(dirname "$target")"
rm -rf "$target.old"
[ -e "$target" ] && mv "$target" "$target.old"
mv "$work/unpacked/sample-kits" "$target"
rm -rf "$target.old"
echo "The sample kits are in $target."
