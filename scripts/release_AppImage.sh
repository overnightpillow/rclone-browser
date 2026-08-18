#!/usr/bin/env bash
#
# Builds a self-contained AppImage. Linux only -- run it on a Linux machine, in
# CI, or through a container (scripts/preflight_linux.sh shows the pattern).
#
# Replaces the Qt5-era script, which assumed a hand-built CentOS 7 toolchain
# from a comment block, a Qt unpacked under /opt, an OpenSSL 1.1.1 built from
# source, and linuxdeploy already on PATH. None of that exists any more.
#
# The build happens on Ubuntu 24.04, which is what CI runs and what
# preflight_linux.sh checks, so the AppImage carries that glibc floor: 2.39,
# meaning Ubuntu 24.04, Debian 13, Fedora 40 or newer. Building on an older
# distro would widen that, but Ubuntu 22.04 ships Qt 6.2 and this needs 6.4.
#
# Usage:  scripts/release_AppImage.sh

set -euo pipefail

if [ "$(uname -s)" != "Linux" ]; then
  echo "error: AppImages can only be built on Linux." >&2
  echo "       From macOS, run this inside a container:" >&2
  echo "         docker run --rm -v \"\$PWD:/src:ro\" ubuntu:24.04 ..." >&2
  echo "       or let the release workflow do it." >&2
  exit 1
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build-appimage"
APPDIR="$BUILD/AppDir"
RELEASE="$ROOT/release"
TOOLS="${LINUXDEPLOY_CACHE:-$BUILD/tools}"
ARCH="$(uname -m)"
APP_NAME="rclone-browser"

# A tagged build names itself after the tag; anything else carries the commit,
# which is what tells two development builds apart. Naming a *release* build
# after the commit is what let the 2.0.0 release end up offering three Windows
# zips: rebuilding the tag produced differently-named files, so uploading them
# added a set beside the old one instead of replacing it -- and one of the old
# ones did not run.
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
if TAG="$(git -C "$ROOT" describe --exact-match --tags HEAD 2>/dev/null)"; then
  VERSION="${TAG#v}"
elif git -C "$ROOT" rev-parse --short HEAD >/dev/null 2>&1; then
  VERSION="$VERSION-$(git -C "$ROOT" rev-parse --short HEAD)"
fi

echo "==> Building $APP_NAME $VERSION for $ARCH"
rm -rf "$BUILD"
cmake -S "$ROOT" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD" -j"$(nproc)"

# The install rules already place the binary, the .desktop file and every icon
# size where an AppDir wants them, so there is nothing to assemble by hand.
echo "==> Installing into an AppDir"
DESTDIR="$APPDIR" cmake --install "$BUILD" >/dev/null

echo "==> Fetching linuxdeploy"
mkdir -p "$TOOLS"
fetch() {
  local url="$1" out="$2"
  if [ ! -x "$TOOLS/$out" ]; then
    curl -fsSL "$url" -o "$TOOLS/$out"
    chmod +x "$TOOLS/$out"
  fi
}
BASE="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous"
QT_BASE="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous"
fetch "$BASE/linuxdeploy-$ARCH.AppImage" "linuxdeploy"
fetch "$QT_BASE/linuxdeploy-plugin-qt-$ARCH.AppImage" "linuxdeploy-plugin-qt"

echo "==> Bundling Qt"
# Containers and CI runners have no FUSE, which an AppImage normally needs to
# mount itself; this makes the tools unpack and run instead. It applies to
# building, not to the AppImage produced.
export APPIMAGE_EXTRACT_AND_RUN=1
# The plugin looks for "qmake" first, which on Ubuntu is Qt 5 or absent.
export QMAKE="${QMAKE:-/usr/bin/qmake6}"
export EXTRA_QT_MODULES="${EXTRA_QT_MODULES:-}"
# xcb is all the plugin bundles by default, which is right for a desktop but
# leaves no way to start the application where there is no display -- neither
# in CI nor in the check at the end of this script. The offscreen plugin is
# about 50KB and makes it testable.
export EXTRA_PLATFORM_PLUGINS="${EXTRA_PLATFORM_PLUGINS:-libqoffscreen.so}"
# Without this the file is named after the .desktop entry, with the version
# from the environment appended in a shape of linuxdeploy's choosing.
export VERSION

( cd "$BUILD" && "$TOOLS/linuxdeploy" \
    --appdir "$APPDIR" \
    --plugin qt \
    --output appimage )

mkdir -p "$RELEASE"
OUT="$RELEASE/$APP_NAME-$VERSION-linux-$ARCH.AppImage"
mv "$BUILD"/*.AppImage "$OUT"
chmod +x "$OUT"

echo "==> Verifying"
# Does it hold together: unpack it, and check the binary inside links only to
# libraries that are either bundled or part of any Linux system.
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
( cd "$WORK" && "$OUT" --appimage-extract >/dev/null )

if [ ! -x "$WORK/squashfs-root/usr/bin/$APP_NAME" ]; then
  echo "error: no $APP_NAME binary inside the AppImage" >&2
  exit 1
fi
for LIB in libQt6Core libQt6Widgets libQt6Gui libQt6Network; do
  if ! find "$WORK/squashfs-root" -name "$LIB.so*" | grep -q .; then
    echo "error: $LIB is missing from the AppImage" >&2
    exit 1
  fi
done

# And does it actually start. There is no headless mode to ask for, so it runs
# against the offscreen platform for a few seconds: still running when the
# timeout fires is the pass, since a missing library or plugin aborts at once.
echo "==> Starting it"
set +e
QT_QPA_PLATFORM=offscreen APPIMAGE_EXTRACT_AND_RUN=1 timeout 10 "$OUT" >"$WORK/run.log" 2>&1
CODE=$?
set -e
if [ "$CODE" -ne 124 ] && [ "$CODE" -ne 0 ]; then
  echo "error: it exited with $CODE instead of running:" >&2
  tail -20 "$WORK/run.log" >&2
  exit 1
fi

echo
echo "Done: $OUT  ($(du -sh "$OUT" | cut -f1))"
echo
echo "It needs no installation: make it executable and run it."
echo "  chmod +x $(basename "$OUT") && ./$(basename "$OUT")"
