# Rclone Remote Manager

A Qt6 desktop front-end for [rclone](https://rclone.org/), with read-only
browsing of [restic](https://restic.net/) repositories.

This is a fork of [kapitainsky/RcloneBrowser](https://github.com/kapitainsky/RcloneBrowser),
which was last updated in December 2020. The original README is preserved as
[README.upstream.md](README.upstream.md) and still describes most of the rclone
functionality accurately.

## What this fork changes

### Builds again

Upstream targets Qt5 and declares `cmake_minimum_required(VERSION 2.8)`, which
CMake 4.x rejects outright — it does not build on a current toolchain at all.
This fork targets **Qt6 and CMake 3.21+**.

- `AUTOMOC`/`AUTOUIC`/`AUTORCC` replace the hand-rolled `qt5_wrap_*` calls and
  the MSVC precompiled-header macro.
- `QtWinExtras` and `QtMacExtras` (both removed in Qt6) are gone: the two icon
  conversions they provided are implemented directly.
- `-Werror` is now opt-in via `-DRRM_WERROR=ON` rather than pinned on. Upstream
  hard-coded it, which is a large part of why the build rotted — every new
  compiler diagnostic became a build failure.

### One rclone process per listing instead of two

Upstream ran `rclone lsd` **and** `rclone lsl --max-depth 1` for every directory
you expanded, then parsed both text outputs with regular expressions. This fork
runs a single `rclone lsjson` and parses JSON, which halves the process spawns
and removes a class of filename and locale parsing bugs.

Listing failures now show an error row. Previously a failed listing was
indistinguishable from an empty directory.

### Restic snapshot browsing

Browse a restic repository read-only: snapshots, their file trees, and
restore-to-local. No `backup`, `forget`, or `prune` — this is a viewer.

The interesting part is that restic can use rclone as a backend, so a repository
on S3, B2, or Storj is reachable **through an rclone remote you have already
configured**, with no second copy of the credentials:

```
restic -r rclone:my-s3-remote:backups/laptop
```

Two ways in:

- **Right-click any remote** in the remotes list → *Open as restic repository*,
  then give the path within that remote.
- **Restic → Repositories…** to register repositories by full restic URL. This
  handles repositories that are not backed by an rclone remote (`s3:https://…`,
  `b2:bucket:path`, or a local path).

#### Repository passwords

Two options, in order of preference:

1. **Password command** — a shell command that prints the password on stdout,
   configured per repository. It is passed to restic as
   `RESTIC_PASSWORD_COMMAND`, so the password never passes through this
   application. On macOS, for example:

   ```
   security find-generic-password -s my-restic-repo -w
   ```

2. **Prompt** — if no password command is set you are asked once per session.
   The password is held in memory only and is never written to the settings
   file.

Note that `restic ls` has no depth limit, so expanding a snapshot fetches its
entire file tree in one call and caches it. This is fast for typical snapshots
but will use meaningful memory for snapshots containing millions of files.

### Fixes

- Settings landed at `/rclone-browser/rclone-browser.ini` — the filesystem root —
  on any Linux system that does not export `XDG_CONFIG_HOME`, which is most of
  them. Now falls back to `~/.config` per the XDG spec.
- The rclone version check called `std::stoi` with no exception handling, so a
  version string like `1.65.0-beta` terminated the application.
- File sizes under 10 bytes rendered as the literal string `0`.
- `rclone config` on macOS wrote a shell script to a fixed, world-readable
  `/tmp/rclone_config.command` and marked it executable. It now uses a
  `QTemporaryFile` at an unguessable path with owner-only permissions.

## Building

Requires Qt6 and CMake 3.21+.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

On macOS, point CMake at Homebrew's Qt:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
```

The binary lands in `build/build/`.

### Releasing (macOS)

```bash
./scripts/release_macOS.sh --zip
```

Builds, bundles the Qt frameworks with `macdeployqt`, and produces a
self-contained ~88 MB `.app` in `release/`.

`macdeployqt` alone is not sufficient: it copies the frameworks in but leaves
the build-time rpath pointing at the Homebrew Qt, so dyld loads Qt twice and
warns that classes are "implemented in both" — with a threat of "mysterious
crashes". The script strips that rpath, points it at the bundled frameworks,
re-signs (every `install_name_tool` edit invalidates the signature), and fails
if the bundle still links against the Qt prefix.

The result is ad-hoc signed, which is fine locally. On another Mac it will be
quarantined; clear it with `xattr -dr com.apple.quarantine`.

## Status

The rclone side and the restic side both build clean and run on macOS with
Qt 6.11. **The Windows and Linux builds have not been run** — the Qt6 port of
the Windows-only icon path (`QImage::fromHICON`) in particular is unverified on
a real Windows toolchain.

There is still no test suite.

## License

MIT, inheriting upstream's copyright notices. See [LICENSE](LICENSE).
