#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-0.1.0}"
BUILD_DIR="${2:-build}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/$BUILD_DIR/LGM.app"
DIST="$ROOT/dist"

if [[ ! -d "$APP" ]]; then
  echo "LGM.app not found in $BUILD_DIR — build with -DLGM_MACOS_BUNDLE=ON first." >&2
  exit 1
fi

QT_BIN="$(dirname "$(command -v macdeployqt)")"
"$QT_BIN/macdeployqt" "$APP" -always-overwrite

mkdir -p "$DIST"
OUT="$DIST/LGM-${VERSION}-macos.zip"
rm -f "$OUT"
ditto -c -k --sequesterRsrc --keepParent "$APP" "$OUT"
echo "Created $OUT"
