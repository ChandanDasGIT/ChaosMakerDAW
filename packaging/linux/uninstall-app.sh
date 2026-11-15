#!/bin/bash
# Removes what install-app.sh installed, and the sample kits. Your settings and projects (in
# ~/.config/ChaosMaker and ~/.local/share/ChaosMaker/ChaosMaker) are kept.
set -euo pipefail

id=io.github.chandandasgit.ChaosMaker
data="${XDG_DATA_HOME:-$HOME/.local/share}"
target="$HOME/.local/opt/ChaosMaker"

for file in "$data/applications/$id.desktop" "$data/icons/hicolor/scalable/apps/$id.svg" "$HOME/.local/bin/ChaosMaker" \
            "$target" "$target.new" "$target.old" "$data/ChaosMaker/sample-kits" "$data/ChaosMaker/sample-kits.old"; do
    if [ -e "$file" ] || [ -L "$file" ]; then
        rm -rf -- "$file"
        echo "Removed $file"
    fi
done
command -v update-desktop-database >/dev/null && update-desktop-database -q "$data/applications" || true
echo "Your settings and projects are still in ~/.config/ChaosMaker and $data/ChaosMaker/ChaosMaker."
