#!/usr/bin/env bash
#
# Reproduces the Linux CI job locally, in a container, before anything is
# published.
#
# This exists because the Mac and CI differ in two independent ways that both
# bite: Homebrew ships a much newer Qt than Ubuntu LTS (a 6.5-only API compiled
# fine here and broke there), and clang diagnoses less than GCC does (a
# range-for temporary passed here and failed -Werror there). Neither is visible
# from macOS. Gitea is private and mirrors to a public GitHub repository, so a
# bad push is public before it can be taken back.
#
# Usage:  scripts/preflight_linux.sh [rev]     (default: HEAD)
#
# The committed tree at <rev> is what gets built — never the working tree — so
# this checks exactly what a push would publish.

set -euo pipefail

REV="${1:-HEAD}"
IMAGE="rclone-browser-preflight:ubuntu-24.04"
CCACHE_VOLUME="rclone-browser-preflight-ccache"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! docker info >/dev/null 2>&1; then
  echo "error: the Docker daemon is not running (open -a Docker), so the Linux" >&2
  echo "       build cannot be checked. To push without checking:" >&2
  echo "         git push --no-verify" >&2
  exit 1
fi

SHA="$(git -C "$ROOT" rev-parse --short "$REV")"

# Qt and the toolchain are baked into an image so that repeat runs cost a build
# rather than an apt-get. ccache lives in a named volume for the same reason.
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  echo "==> Building the preflight image (first run only, a few minutes)"
  docker build -t "$IMAGE" - >/dev/null <<'DOCKERFILE'
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build ccache \
      qt6-base-dev qt6-base-dev-tools libgl1-mesa-dev \
    && rm -rf /var/lib/apt/lists/*
DOCKERFILE
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
git -C "$ROOT" archive "$REV" | tar -x -C "$WORK"

echo "==> Building $SHA on Ubuntu 24.04 (GCC, Qt 6.4, -Werror)"

# -Werror on purpose: this is the setting CI uses, and matching it is the whole
# point. The tests set QT_QPA_PLATFORM=offscreen themselves, so no display is
# needed in the container.
docker run --rm \
  -v "$WORK:/src:ro" \
  -v "$CCACHE_VOLUME:/ccache" \
  -e CCACHE_DIR=/ccache \
  "$IMAGE" bash -c '
    set -e
    cp -r /src /work && cd /work
    cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DRRM_WERROR=ON \
      -DCMAKE_C_COMPILER_LAUNCHER=ccache \
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache >/tmp/cfg.log 2>&1 || {
        echo "--- configure failed ---"; tail -25 /tmp/cfg.log; exit 1; }
    if ! cmake --build build >/tmp/build.log 2>&1; then
      echo "--- build failed ---"
      grep -E "error:" /tmp/build.log | sort -u | head -20
      exit 1
    fi
    ctest --test-dir build --output-on-failure
  '

echo "==> $SHA builds and tests clean on Linux"
