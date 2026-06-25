#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-0.1.0}"
BUILD_DIR="${2:-build}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BINARY="$ROOT/$BUILD_DIR/LGM"
DIST="$ROOT/dist"
APPDIR="$DIST/AppDir"

if [[ ! -x "$BINARY" ]]; then
  echo "LGM binary not found in $BUILD_DIR — build first." >&2
  exit 1
fi

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications"
cp "$BINARY" "$APPDIR/usr/bin/LGM"
if [[ -d "$ROOT/$BUILD_DIR/Examples" ]]; then
  cp -r "$ROOT/$BUILD_DIR/Examples" "$APPDIR/usr/bin/Examples"
fi

cat >"$APPDIR/usr/share/applications/lgm.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=LGM
Exec=LGM
Icon=LGM
Categories=Education;Science;
EOF

LINUXDEPLOY="${LINUXDEPLOY:-linuxdeploy-x86_64.AppImage}"
if [[ ! -x "$LINUXDEPLOY" ]]; then
  wget -q -O "$LINUXDEPLOY" \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
  chmod +x "$LINUXDEPLOY"
fi

QT_PLUGIN="${QT_PLUGIN:-linuxdeploy-plugin-qt-x86_64.AppImage}"
if [[ ! -x "$QT_PLUGIN" ]]; then
  wget -q -O "$QT_PLUGIN" \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
  chmod +x "$QT_PLUGIN"
fi

export QML_SOURCES_PATHS="$ROOT"
"$LINUXDEPLOY" --appdir "$APPDIR" --plugin qt -e "$APPDIR/usr/bin/LGM" --output appimage
mv "$APPDIR"/*.AppImage "$DIST/LGM-${VERSION}-linux-x86_64.AppImage"
rm -rf "$APPDIR"
echo "Created $DIST/LGM-${VERSION}-linux-x86_64.AppImage"
