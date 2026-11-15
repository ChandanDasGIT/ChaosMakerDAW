#!/bin/bash
# Makes the Linux packages from a release build:
#   ChaosMaker-<version>-x86_64.AppImage
#   ChaosMaker-<version>-linux-x86_64.tar.xz  (install-app.sh / uninstall-app.sh inside)
#   get-sample-kits.sh                        (for the AppImage: fetches the drum kit and piano)
#   SHA256SUMS.txt
# Both hold the app with Qt and the libraries it needs, and every bundled library's licence in
# licenses/. They run on systems as new as the one that built them (glibc), so release builds
# come from an older system (CI: Ubuntu 24.04). The sample kits are not inside: they are
# downloaded once, by install-app.sh or get-sample-kits.sh.
#
# Usage: packaging/linux/make-packages.sh <build folder> <output folder>
# Needs: the Qt the build used (qmake6 / qmake on PATH, or QMAKE=/path/to/qmake), curl,
#        rpm or dpkg (to find the licences of the bundled libraries).
set -euo pipefail

build="$(cd "$1" && pwd)"
mkdir -p "$2"
out="$(cd "$2" && pwd)"
here="$(cd "$(dirname "$0")" && pwd)"
project="$(cd "$here/../.." && pwd)"
id=io.github.chandandasgit.ChaosMaker
version=$(sed -n 's/^project(ChaosMaker VERSION \([0-9.]*\).*/\1/p' "$project/CMakeLists.txt")

export ARCH=x86_64
export APPIMAGE_EXTRACT_AND_RUN=1 # the tools are AppImages too; no FUSE needed
export NO_STRIP=1                 # linuxdeploy's own strip is older than newer systems' libraries and fails on them

# ------------------------------------------------------------------ Qt
QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake || true)}"
if [ -z "$QMAKE" ]; then
    echo "qmake not found: put Qt's bin folder on PATH, or set QMAKE=/path/to/qmake" >&2
    exit 1
fi
export QMAKE
qtPrefix="$("$QMAKE" -query QT_INSTALL_PREFIX)"
qtLibs="$("$QMAKE" -query QT_INSTALL_LIBS)"
qtPlugins="$("$QMAKE" -query QT_INSTALL_PLUGINS)"
qtQml="$("$QMAKE" -query QT_INSTALL_QML)"
echo "Using Qt from $qtPrefix"

# ------------------------------------------------------------------ the tools, pinned (downloaded once)
tools="$out/tools"
mkdir -p "$tools"
fetch() { [ -x "$tools/$2" ] || { curl -fsSL --retry 3 -o "$tools/$2" "$1" && chmod +x "$tools/$2"; }; }
fetch https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage linuxdeploy-x86_64.AppImage
fetch https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage linuxdeploy-plugin-qt-x86_64.AppImage
fetch https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage appimagetool-x86_64.AppImage
export PATH="$tools:$PATH" # linuxdeploy finds its Qt plugin here

# ------------------------------------------------------------------ the AppDir: the app, as installed
appdir="$out/AppDir"
rm -rf "$appdir"
DESTDIR="$appdir" cmake --install "$build" --prefix /usr
desktop="$appdir/usr/share/applications/$id.desktop"
icon="$appdir/usr/share/icons/hicolor/scalable/apps/$id.svg"
for f in "$appdir/usr/bin/appChaosMaker" "$desktop" "$icon"; do
    [ -e "$f" ] || { echo "Missing after install: $f" >&2; exit 1; }
done

# ------------------------------------------------------------------ Qt and the libraries (linuxdeploy)
# Only the app's QML is scanned for imports (not build folders)
qmlSources="$out/qml-sources"
rm -rf "$qmlSources"
mkdir -p "$qmlSources"
cp "$project/Main.qml" "$qmlSources/"
cp -r "$project/frontend" "$qmlSources/"
export QML_SOURCES_PATHS="$qmlSources"

# Wayland: Qt 6.10+ has one platform plugin (libqwayland.so), older Qt two
platforms=""
# (offscreen: no window at all, for checking a package on a machine without a desktop, as CI does)
for p in libqwayland.so libqwayland-generic.so libqwayland-egl.so libqoffscreen.so; do
    [ -f "$qtPlugins/platforms/$p" ] && platforms="${platforms:+$platforms;}$p"
done
export EXTRA_PLATFORM_PLUGINS="$platforms"
extra=""
for d in wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration iconengines imageformats; do
    [ -d "$qtPlugins/$d" ] && extra="${extra:+$extra;}$d"
done
export EXTRA_QT_PLUGINS="$extra"

linuxdeploy-x86_64.AppImage --appdir "$appdir" --plugin qt \
    --executable "$appdir/usr/bin/appChaosMaker" --desktop-file "$desktop" --icon-file "$icon"

# Wayland's plugins: linuxdeploy doesn't pick them up. Without the shell integration (xdg-shell)
# Qt can't open a window on Wayland and falls back to X11 (XWayland); without the graphics one
# it can't draw with OpenGL; the decorations draw title bars where the desktop doesn't.
for kind in wayland-shell-integration wayland-graphics-integration-client wayland-decoration-client; do
    [ -d "$qtPlugins/$kind" ] || continue
    mkdir -p "$appdir/usr/plugins/$kind"
    for f in "$qtPlugins/$kind"/*.so; do
        [ -e "$f" ] || continue
        case "$f" in
            *-server.so) ;; # (server side: not needed by an app)
            *) cp -L "$f" "$appdir/usr/plugins/$kind/" ;;
        esac
    done
done
# The window icon is an SVG
for lib in libQt6WaylandClient.so.6 libQt6WaylandEglClientHwIntegration.so.6 libQt6WlShellIntegration.so.6 libQt6Svg.so.6; do
    if [ ! -e "$appdir/usr/lib/$lib" ] && [ -e "$qtLibs/$lib" ]; then cp -L "$qtLibs/$lib" "$appdir/usr/lib/"; fi
done

# ------------------------------------------------------------------ untouched libraries, plugins, QML modules
# linuxdeploy rewrites the headers of everything it bundles (where to look for libraries); on
# newer systems that damages some and the app crashes as they load. So each is put back exactly
# as it is in Qt or on this system; AppRun (below) sets the library paths instead.
originalLib() { # <file name> -> the untouched 64-bit library, or nothing
    local src="$qtLibs/$1"
    if [ ! -e "$src" ]; then
        src="$( { ldconfig -p 2>/dev/null || /sbin/ldconfig -p 2>/dev/null || true; } \
                | awk -v l="$1" '$1 == l && /x86-64/ && !found { print $NF; found = 1 }' )"
    fi
    printf '%s' "$src"
}
sources="$out/bundled-sources.txt" # each bundled file's original, for the licences below
: > "$sources"
while IFS= read -r -d '' f; do
    src="$(originalLib "$(basename "$f")")"
    if [ -n "$src" ] && [ -e "$src" ]; then cp -L --remove-destination "$src" "$f"; readlink -f "$src" >> "$sources"; fi
done < <(find "$appdir/usr/lib" -maxdepth 1 \( -type f -o -type l \) -name '*.so*' -print0)

restoreTree() { # <folder in the AppDir> <the same folder in Qt>
    local f rel
    [ -d "$1" ] || return 0
    while IFS= read -r -d '' f; do
        rel="${f#"$1"/}"
        if [ -f "$2/$rel" ]; then cp -L --remove-destination "$2/$rel" "$f"; readlink -f "$2/$rel" >> "$sources"; fi
    done < <(find "$1" -type f -name '*.so*' -print0)
}
restoreTree "$appdir/usr/plugins" "$qtPlugins"
restoreTree "$appdir/usr/qml" "$qtQml"

# Left out: libraries that must be the system's own, matching its services (systemd, the sound
# server and JACK); every desktop Linux has them.
for library in libsystemd.so libudev.so libpipewire-0.3.so libpulse libasound.so libjack.so; do
    rm -f "$appdir"/usr/lib/"$library"*
done

# ------------------------------------------------------------------ licences
# Every bundled library's licence, found through the system package that owns it; Qt installed
# without packages (CI) brings its own Licenses folder. nlohmann json is built into the app.
licenses="$appdir/licenses"
rm -rf "$licenses"
mkdir -p "$licenses"
cp "$project/LICENSE" "$licenses/ChaosMaker-GPL-3.0.txt"
owner() { # <file> -> its package, or nothing
    if command -v rpm >/dev/null 2>&1; then rpm -qf --qf '%{NAME}\n' "$1" 2>/dev/null | head -1
    elif command -v dpkg >/dev/null 2>&1; then dpkg -S "$1" 2>/dev/null | head -1 | cut -d: -f1
    fi
}
copyLicence() { # <package>: its licence files into licenses/<package>/
    local pkg="$1" dest="$licenses/$1" f
    [ -d "$dest" ] && return 0
    mkdir -p "$dest"
    if command -v rpm >/dev/null 2>&1; then
        while IFS= read -r f; do [ -f "$f" ] && cp "$f" "$dest/"; done < <(rpm -qL "$pkg" 2>/dev/null)
        if [ -z "$(ls -A "$dest")" ]; then
            # A sub-package (qt6-qtbase-gui): the licence ships with a package of the same source
            local source sibling
            source="$(rpm -q --qf '%{SOURCERPM}\n' "$pkg" 2>/dev/null | head -1)"
            while IFS= read -r sibling; do
                while IFS= read -r f; do [ -f "$f" ] && cp "$f" "$dest/"; done < <(rpm -qL "$sibling" 2>/dev/null)
            done < <(rpm -qa --qf '%{NAME} %{SOURCERPM}\n' | awk -v s="$source" '$2 == s { print $1 }' | sort -u)
        fi
        if [ -z "$(ls -A "$dest")" ]; then
            # Licence shipped as documentation (/usr/share/doc/<package>/COPYING)
            while IFS= read -r f; do [ -f "$f" ] && cp "$f" "$dest/"; done \
                < <(rpm -qd "$pkg" 2>/dev/null | grep -i -E '/(copying|license|licence|copyright)[^/]*$')
        fi
    elif [ -f "/usr/share/doc/$pkg/copyright" ]; then
        cp "/usr/share/doc/$pkg/copyright" "$dest/"
    fi
    # Packages that ship no licence file at all: the text kept in packaging/linux/licenses/
    if [ -z "$(ls -A "$dest")" ] && [ -d "$here/licenses/$pkg" ]; then cp "$here/licenses/$pkg"/* "$dest/"; fi
    [ -n "$(ls -A "$dest")" ] || { rmdir "$dest"; return 1; }
}
missing=()
sort -u "$sources" -o "$sources"
jsonHeader="$(printf '#include <nlohmann/json.hpp>\n' | ${CXX:-c++} -E -H -x c++ - 2>&1 >/dev/null \
              | awk '/nlohmann\/json.hpp/ && !found { print $2; found = 1 }' || true)"
[ -n "$jsonHeader" ] && echo "$jsonHeader" >> "$sources"
qtNoPackage=0
while IFS= read -r f; do
    pkg="$(owner "$f")"
    if [ -n "$pkg" ]; then
        copyLicence "$pkg" || missing+=("$pkg ($f)")
    elif [[ "$f" == "$qtPrefix"* ]]; then
        qtNoPackage=1
    else
        missing+=("$f")
    fi
done < "$sources"
if [ "$qtNoPackage" = 1 ]; then
    qtLicences="$(cd "$qtPrefix/../.." 2>/dev/null && pwd)/Licenses"
    [ -d "$qtLicences" ] || qtLicences="$qtPrefix/Licenses"
    if [ -d "$qtLicences" ]; then cp -r "$qtLicences" "$licenses/Qt"; else missing+=("Qt licences ($qtPrefix)"); fi
fi
if [ "${#missing[@]}" -gt 0 ]; then
    printf 'No licence found for:\n' >&2
    printf '  %s\n' "${missing[@]}" >&2
    exit 1
fi

# AppRun: points the app at the bundled libraries, plugins and QML modules
rm -f "$appdir/AppRun"
cat > "$appdir/AppRun" <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
export QML_IMPORT_PATH="$HERE/usr/qml"
export QML2_IMPORT_PATH="$HERE/usr/qml"
exec "$HERE/usr/bin/appChaosMaker" "$@"
EOF
chmod +x "$appdir/AppRun"

# ------------------------------------------------------------------ the AppImage
appImage="ChaosMaker-$version-x86_64.AppImage"
appimagetool-x86_64.AppImage --no-appstream "$appdir" "$out/$appImage"

# ------------------------------------------------------------------ the tarball: the same files, with the install scripts
name="ChaosMaker-$version-linux-x86_64"
stage="$out/$name"
rm -rf "$stage"
mkdir -p "$stage"
cp -a "$appdir" "$stage/app"
install -m755 "$here/install-app.sh" "$here/uninstall-app.sh" "$here/get-sample-kits.sh" "$stage/"
cp "$project/LICENSE" "$stage/"
XZ_OPT=-T0 tar -C "$out" -cJf "$out/$name.tar.xz" "$name"
rm -rf "$stage" "$qmlSources"

# ------------------------------------------------------------------ the sample kit script next to the AppImage, and checksums
install -m755 "$here/get-sample-kits.sh" "$out/get-sample-kits.sh"
(cd "$out" && sha256sum -- "$appImage" "$name.tar.xz" get-sample-kits.sh > SHA256SUMS.txt)

echo "Made, in $out:"
echo "  $appImage"
echo "  $name.tar.xz"
echo "  get-sample-kits.sh"
echo "  SHA256SUMS.txt"
