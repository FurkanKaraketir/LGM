#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-0.1.0}"
BUILD_DIR="${2:-build}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/$BUILD_DIR/LGM.app"
DIST="$ROOT/dist"
STAGING="$DIST/dmg-staging"
DMG="$DIST/LGM-${VERSION}-macos.dmg"

if [[ ! -d "$APP" ]]; then
  echo "LGM.app not found in $BUILD_DIR — build with -DLGM_MACOS_BUNDLE=ON first." >&2
  exit 1
fi

QT_BIN="$(dirname "$(command -v macdeployqt)")"
"$QT_BIN/macdeployqt" "$APP" -always-overwrite

mkdir -p "$DIST"
rm -rf "$STAGING" "$DMG"
mkdir -p "$STAGING"
cp -R "$APP" "$STAGING/"
ln -s /Applications "$STAGING/Applications"

hdiutil create \
  -volname "LGM ${VERSION}" \
  -srcfolder "$STAGING" \
  -ov \
  -format UDZO \
  "$DMG"

rm -rf "$STAGING"
echo "Created $DMG"

# ponytail: optional zip for portable use without mounting a disk image
ZIP="$DIST/LGM-${VERSION}-macos-portable.zip"
rm -f "$ZIP"
ditto -c -k --sequesterRsrc --keepParent "$APP" "$ZIP"
echo "Created $ZIP"
