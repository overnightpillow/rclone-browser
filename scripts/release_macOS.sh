#!/bin/bash
#
# Builds a self-contained macOS .app bundle.
#
# Replaces the Qt5-era release_macOS.sh, which hardcoded the Intel Homebrew
# prefix (/usr/local/opt/qt), called nproc, passed -qmldir for QML this app
# does not have, and depended on a global npm appdmg plus p7zip.
#
# Usage:  scripts/release_macOS.sh [--zip|--dmg]

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
# Homebrew's Qt is built for the machine it is installed on, so the bundle is
# single-architecture and the file has to say which: an arm64 build will not
# run on an Intel Mac at all, and Rosetta only translates the other direction.
ARCH="$(uname -m)"
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

# macdeployqt copies the libraries in but leaves each one's own install name
# (LC_ID_DYLIB) pointing at where it came from -- libbrotlicommon still called
# itself /opt/homebrew/opt/brotli/lib/libbrotlicommon.1.dylib inside the
# bundle. Nothing referenced it by that name, so it loaded anyway, but it is a
# Homebrew path shipped to machines that have no Homebrew, and anything that
# later links against it by its own name would look there.
echo "==> Fixing install names"
while read -r MACHO; do
  file "$MACHO" | grep -q Mach-O || continue

  CURRENT_ID="$(otool -D "$MACHO" 2>/dev/null | sed -n '2p')"
  [ -n "$CURRENT_ID" ] || continue
  case "$CURRENT_ID" in
    @rpath/*|@executable_path/*|@loader_path/*) continue ;;
  esac

  # Frameworks keep their Name.framework/Versions/A/Name shape; a plain
  # library is just its file name.
  case "$MACHO" in
    *.framework/Versions/*)
      NAME="$(basename "$MACHO")"
      NEW_ID="@rpath/$NAME.framework/Versions/A/$NAME"
      ;;
    *)
      NEW_ID="@rpath/$(basename "$MACHO")"
      ;;
  esac

  install_name_tool -id "$NEW_ID" "$MACHO"
done < <(find "$APP/Contents/Frameworks" "$APP/Contents/PlugIns" -type f 2>/dev/null)

# Every install_name_tool edit invalidates the signature, so sign last.
echo "==> Signing (ad-hoc)"
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

# The whole bundle, not just the main executable: the leak found by checking
# only that one was in a library three levels down from it.
echo "==> Verifying the bundle is self-contained"
LEAKED=0
OUTSIDE='/opt/homebrew\|/usr/local/opt\|/usr/local/Cellar'
while read -r MACHO; do
  file "$MACHO" | grep -q Mach-O || continue

  # otool -L prints the file name, then -- for a library -- its own install
  # name, then the dependencies. Mistaking that install name for a dependency
  # reports every library as leaking, so it is dropped explicitly rather than
  # by counting lines: an executable has no install name line to drop.
  ID="$(otool -D "$MACHO" 2>/dev/null | sed -n '2p')"
  DEPS="$(otool -L "$MACHO" | tail -n +2 | sed 's/ (compatibility.*//; s/^[[:space:]]*//')"
  if [ -n "$ID" ]; then
    DEPS="$(printf '%s\n' "$DEPS" | grep -vxF "$ID" || true)"
  fi

  if printf '%s\n' "$DEPS" | grep -q "$OUTSIDE"; then
    echo "error: $MACHO depends on a library outside the bundle:" >&2
    printf '%s\n' "$DEPS" | grep "$OUTSIDE" >&2
    LEAKED=1
  fi

  case "$ID" in
    /opt/homebrew/*|/usr/local/opt/*|/usr/local/Cellar/*)
      echo "error: $MACHO calls itself $ID" >&2
      LEAKED=1
      ;;
  esac
done < <(find "$APP" -type f)

if [ "$LEAKED" -ne 0 ]; then
  exit 1
fi

mkdir -p "$RELEASE"
rm -rf "${RELEASE:?}/$APP_NAME.app"
cp -R "$APP" "$RELEASE/"

if [ "${1:-}" = "--zip" ]; then
  echo "==> Zipping"
  # ditto preserves the signature and resource forks; plain zip does not.
  ( cd "$RELEASE" && ditto -c -k --keepParent "$APP_NAME.app" "$APP_NAME-$VERSION-macos-$ARCH.zip" )
fi

if [ "${1:-}" = "--dmg" ]; then
  DMG="$RELEASE/$APP_NAME-$VERSION-macos-$ARCH.dmg"
  echo "==> Building $(basename "$DMG")"

  # A disk image is what a Mac user expects to download: it mounts to a window
  # holding the application and a shortcut to /Applications, and dragging
  # between the two is the install. A zip leaves the bundle wherever the
  # browser happened to put it, which is usually Downloads, where it keeps
  # running from until it is noticed.
  STAGE="$(mktemp -d)"
  trap 'rm -rf "$STAGE"' EXIT

  cp -R "$RELEASE/$APP_NAME.app" "$STAGE/"
  ln -s /Applications "$STAGE/Applications"

  rm -f "$DMG"
  # UDZO is the compressed read-only format; anything else is bigger for no
  # benefit on a download.
  hdiutil create \
    -volname "$APP_NAME $VERSION" \
    -srcfolder "$STAGE" \
    -ov \
    -format UDZO \
    "$DMG" >/dev/null

  rm -rf "$STAGE"
  trap - EXIT

  echo "==> Verifying the image"
  hdiutil verify "$DMG" >/dev/null

  # Mount it and check the bundle inside is the signed one, rather than
  # trusting that the copy above did what it looked like it did.
  MOUNT="$(mktemp -d)"
  hdiutil attach "$DMG" -nobrowse -readonly -mountpoint "$MOUNT" >/dev/null
  if ! codesign --verify --deep --strict "$MOUNT/$APP_NAME.app"; then
    hdiutil detach "$MOUNT" >/dev/null
    rmdir "$MOUNT"
    echo "error: the application inside the image is not correctly signed" >&2
    exit 1
  fi
  if [ ! -L "$MOUNT/Applications" ]; then
    hdiutil detach "$MOUNT" >/dev/null
    rmdir "$MOUNT"
    echo "error: the image has no /Applications shortcut to drag onto" >&2
    exit 1
  fi
  hdiutil detach "$MOUNT" >/dev/null
  rmdir "$MOUNT"

  echo "Done: $DMG  ($(du -sh "$DMG" | cut -f1))"
fi

echo
echo "Done: $RELEASE/$APP_NAME.app  ($(du -sh "$RELEASE/$APP_NAME.app" | cut -f1))"
echo
echo "The bundle is ad-hoc signed, which is fine on this machine. Copied to"
echo "another Mac it will be quarantined; clear that there with:"
echo "  xattr -dr com.apple.quarantine /Applications/$APP_NAME.app"
