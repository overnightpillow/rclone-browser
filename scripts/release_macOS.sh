#!/bin/bash
#
# Builds a self-contained macOS .app bundle.
#
# Replaces the Qt5-era release_macOS.sh, which hardcoded the Intel Homebrew
# prefix (/usr/local/opt/qt), called nproc, passed -qmldir for QML this app
# does not have, and depended on a global npm appdmg plus p7zip.
#
# Usage:  scripts/release_macOS.sh [--zip]

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build-release"
RELEASE="$ROOT/release"
APP_NAME="rclone-browser"

QT_PREFIX="$(brew --prefix qt)"
MACDEPLOYQT="$QT_PREFIX/bin/macdeployqt"

if [ ! -x "$MACDEPLOYQT" ]; then
  echo "error: macdeployqt not found at $MACDEPLOYQT (brew install qt)" >&2
  exit 1
fi

VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")-$(git -C "$ROOT" rev-parse --short HEAD)"
APP="$BUILD/build/$APP_NAME.app"

echo "==> Building $APP_NAME $VERSION"
rm -rf "$BUILD"
cmake -S "$ROOT" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX"
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu)"

echo "==> Bundling Qt frameworks (this takes a few minutes)"
# macdeployqt prints alarming "ERROR: Cannot resolve rpath" lines for transitive
# dependencies of the image-format plugins (libwebp, libsharpyuv, libbrotli) and
# an "invalid signature" complaint, then exits 0 anyway. Both are noise: the
# libraries do get copied, and the signature is replaced below. The checks at
# the end of this script are what actually decide whether the bundle is good.
"$MACDEPLOYQT" "$APP" -executable="$APP/Contents/MacOS/$APP_NAME"

# macdeployqt copies the frameworks in but leaves the build-time rpath pointing
# at the Homebrew Qt. dyld then loads both copies, which prints
# "Class ... is implemented in both" warnings and risks the mysterious crashes
# those warnings threaten. Point the rpath at the bundled frameworks instead.
echo "==> Fixing rpath"
while read -r RPATH; do
  case "$RPATH" in
    @executable_path/*) ;;
    *) install_name_tool -delete_rpath "$RPATH" "$APP/Contents/MacOS/$APP_NAME" 2>/dev/null || true ;;
  esac
done < <(otool -l "$APP/Contents/MacOS/$APP_NAME" | awk '/LC_RPATH/{f=1} f&&/path /{print $2; f=0}')

if ! otool -l "$APP/Contents/MacOS/$APP_NAME" | grep -q "@executable_path/../Frameworks"; then
  install_name_tool -add_rpath "@executable_path/../Frameworks" "$APP/Contents/MacOS/$APP_NAME"
fi

# Every install_name_tool edit invalidates the signature, so sign last.
echo "==> Signing (ad-hoc)"
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

echo "==> Verifying the bundle is self-contained"
if otool -L "$APP/Contents/MacOS/$APP_NAME" | grep -q "$QT_PREFIX"; then
  echo "error: bundle still links against $QT_PREFIX" >&2
  exit 1
fi

mkdir -p "$RELEASE"
rm -rf "${RELEASE:?}/$APP_NAME.app"
cp -R "$APP" "$RELEASE/"

if [ "${1:-}" = "--zip" ]; then
  echo "==> Zipping"
  # ditto preserves the signature and resource forks; plain zip does not.
  ( cd "$RELEASE" && ditto -c -k --keepParent "$APP_NAME.app" "$APP_NAME-$VERSION-macos.zip" )
fi

echo
echo "Done: $RELEASE/$APP_NAME.app  ($(du -sh "$RELEASE/$APP_NAME.app" | cut -f1))"
echo
echo "The bundle is ad-hoc signed, which is fine on this machine. Copied to"
echo "another Mac it will be quarantined; clear that there with:"
echo "  xattr -dr com.apple.quarantine /Applications/$APP_NAME.app"
