#!/bin/sh
# Build a Linux AppImage from a configured, built CMake build directory.
#
#   packaging/linux/make-appimage.sh BUILD_DIR [OUT_DIR]
#
# Needs appimagetool (https://github.com/AppImage/appimagetool) on PATH, or in
# $APPIMAGETOOL. Without it, the AppDir is still made (BUILD_DIR/AppDir), which
# runs as is via AppDir/AppRun.
#
# System libraries (X11, OpenGL, PulseAudio, D-Bus, libc, libstdc++) are not
# bundled: build on the oldest distribution you want to support (CI uses
# Ubuntu 22.04). No ROMs are ever packaged.
set -eu

build=${1:?usage: make-appimage.sh BUILD_DIR [OUT_DIR]}
out=${2:-$build}
build=$(cd "$build" && pwd)
mkdir -p "$out"
out=$(cd "$out" && pwd)

cache="$build/CMakeCache.txt"
[ -f "$cache" ] || { echo "make-appimage: $build is not a CMake build directory" >&2; exit 1; }
name=$(sed -n 's/^BB_OUTPUT_NAME:[A-Z]*=//p' "$cache")
version=$(sed -n 's/^BB_PACKAGE_VERSION:[A-Z]*=//p' "$cache")
[ -n "$name" ] || { echo "make-appimage: BB_OUTPUT_NAME not found in $cache" >&2; exit 1; }
arch=$(uname -m)

appdir="$build/AppDir"
rm -rf "$appdir"
cmake --install "$build" --prefix "$appdir/usr" --strip >/dev/null

desktop="$appdir/usr/share/applications/$name.desktop"
icon="$appdir/usr/share/icons/hicolor/256x256/apps/$name.png"
[ -f "$desktop" ] && [ -f "$icon" ] || { echo "make-appimage: install did not produce the desktop file/icon" >&2; exit 1; }
cp "$desktop" "$appdir/$name.desktop"
cp "$icon" "$appdir/$name.png"
ln -sf "$name.png" "$appdir/.DirIcon"

cat > "$appdir/AppRun" <<APPRUN
#!/bin/sh
here=\$(dirname "\$(readlink -f "\$0")")
exec "\$here/usr/bin/$name" "\$@"
APPRUN
chmod +x "$appdir/AppRun"

tool=${APPIMAGETOOL:-$(command -v appimagetool || true)}
if [ -z "$tool" ]; then
    echo "make-appimage: appimagetool not found; AppDir left at $appdir"
    exit 0
fi
target="$out/$name-$version-linux-$arch.AppImage"
ARCH=$arch "$tool" --no-appstream "$appdir" "$target"
echo "wrote $target"
