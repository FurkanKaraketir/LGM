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

# ponytail: ad-hoc sign only (no Apple notarization). Sign nested binaries first, not --deep on the bundle.
sign_macos_bundle() {
  local app="$1"

  if [[ -d "$app/Contents/Frameworks" ]]; then
    while IFS= read -r framework; do
      local name
      name="$(basename "$framework" .framework)"
      if [[ -f "$framework/Versions/A/$name" ]]; then
        codesign --force --sign - "$framework/Versions/A/$name"
      fi
      codesign --force --sign - "$framework"
    done < <(find "$app/Contents/Frameworks" -depth -type d -name '*.framework')
    find "$app/Contents/Frameworks" -type f -name '*.dylib' -exec codesign --force --sign - {} \;
  fi

  if [[ -d "$app/Contents/PlugIns" ]]; then
    find "$app/Contents/PlugIns" -type f -name '*.dylib' -exec codesign --force --sign - {} \;
  fi

  codesign --force --sign - "$app/Contents/MacOS/LGM"
  codesign --force --sign - "$app"
}

QT_BIN="$(dirname "$(command -v macdeployqt)")"
"$QT_BIN/macdeployqt" "$APP" -always-overwrite

sign_macos_bundle "$APP"
xattr -cr "$APP"
codesign --verify --deep --strict "$APP"

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
