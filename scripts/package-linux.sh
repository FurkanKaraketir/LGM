#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-0.2.0}"
BUILD_DIR="${2:-build}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BINARY="$ROOT/$BUILD_DIR/LGM"
ICON="$ROOT/src/assets/app_logo.png"
DIST="$ROOT/dist"
APPDIR="$DIST/AppDir"
OUT="$DIST/LGM-${VERSION}-linux-x86_64.AppImage"

if [[ ! -x "$BINARY" ]]; then
  echo "LGM binary not found in $BUILD_DIR — build first." >&2
  exit 1
fi

if [[ ! -f "$ICON" ]]; then
  echo "App icon not found at $ICON" >&2
  exit 1
fi

rm -rf "$APPDIR" "$OUT"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications"

cp "$BINARY" "$APPDIR/usr/bin/LGM"
if [[ -d "$ROOT/$BUILD_DIR/Examples" ]]; then
  cp -r "$ROOT/$BUILD_DIR/Examples" "$APPDIR/usr/bin/Examples"
fi

cat >"$APPDIR/lgm.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=LGM
Comment=Linear Graph Modeling
Exec=LGM
Icon=app_logo
Terminal=false
Categories=Education;Science;
StartupWMClass=LGM
EOF

cp "$APPDIR/lgm.desktop" "$APPDIR/usr/share/applications/lgm.desktop"

LINUXDEPLOY="${LINUXDEPLOY:-$ROOT/dist/linuxdeploy-x86_64.AppImage}"
if [[ ! -x "$LINUXDEPLOY" ]]; then
  mkdir -p "$DIST"
  wget -q -O "$LINUXDEPLOY" \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
  chmod +x "$LINUXDEPLOY"
fi

QT_PLUGIN="${QT_PLUGIN:-$ROOT/dist/linuxdeploy-plugin-qt-x86_64.AppImage}"
if [[ ! -x "$QT_PLUGIN" ]]; then
  wget -q -O "$QT_PLUGIN" \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
  chmod +x "$QT_PLUGIN"
fi

export QML_SOURCES_PATHS="$ROOT"
export LINUXDEPLOY_PLUGIN_QT="$QT_PLUGIN"

mkdir -p "$DIST"
pushd "$DIST" >/dev/null
"$LINUXDEPLOY" \
  --appdir "$APPDIR" \
  --desktop-file "$APPDIR/lgm.desktop" \
  --icon-file "$ICON" \
  --plugin qt \
  -e "$APPDIR/usr/bin/LGM" \
  --output appimage

shopt -s nullglob
built=(LGM*.AppImage)
if ((${#built[@]} == 0)); then
  echo "linuxdeploy did not produce an AppImage." >&2
  exit 1
fi
mv "${built[0]}" "$(basename "$OUT")"
popd >/dev/null

rm -rf "$APPDIR"
rm -f "$LINUXDEPLOY" "$QT_PLUGIN"
echo "Created $OUT"
