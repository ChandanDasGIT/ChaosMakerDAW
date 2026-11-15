#!/bin/bash
# Makes ChaosMaker-sample-kits.tar.xz (a sample-kits folder: the acoustic drum kit and the grand
# piano, each with its own licence file) and prints its SHA-256 for get-sample-kits.sh. It is
# uploaded once, to the repository's "sample-kits-1" release.
# Usage: packaging/linux/make-sample-kits.sh <output folder>
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
project="$(cd "$here/../.." && pwd)"
mkdir -p "$1"
out="$(cd "$1" && pwd)"
kits="$project/resources/sample-kits"

for folder in "AccousticDrums/Gogodze_Phu_vol_II" "GrandPiano/SplendidGrandPiano"; do
    [ -d "$kits/$folder" ] || { echo "Missing: $kits/$folder" >&2; exit 1; }
done
[ -f "$kits/AccousticDrums/Gogodze_Phu_vol_II/LICENSE" ] || { echo "The drum kit's licence file is missing." >&2; exit 1; }
[ -f "$kits/GrandPiano/SplendidGrandPiano/README.md" ] || { echo "The piano's README (its licence note) is missing." >&2; exit 1; }

archive="$out/ChaosMaker-sample-kits.tar.xz"
XZ_OPT=-T0 tar -C "$project/resources" --exclude='.git*' -cJf "$archive" sample-kits
(cd "$out" && sha256sum ChaosMaker-sample-kits.tar.xz > ChaosMaker-sample-kits.tar.xz.sha256)
echo "Made $archive"
cat "$out/ChaosMaker-sample-kits.tar.xz.sha256"
