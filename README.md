# rclone-browser 2.0

[![build](https://github.com/overnightpillow/rclone-browser/actions/workflows/build.yml/badge.svg)](https://github.com/overnightpillow/rclone-browser/actions/workflows/build.yml)

A Qt6 desktop front-end for [rclone](https://rclone.org/), with read-only
browsing of [restic](https://restic.net/) repositories.

*An independent project, not affiliated with or endorsed by the rclone or
restic projects.*

This continues [kapitainsky/RcloneBrowser](https://github.com/kapitainsky/RcloneBrowser),
whose last release was 1.8.0 in February 2020. It is still the same application and
still stores its settings under the same name, so **upgrading from 1.8.0 keeps
your existing preferences and saved tasks** — the version bump marks the Qt6
port and the restic support, not a rename.

The original README is preserved as [README.upstream.md](README.upstream.md)
and still describes most of the rclone functionality accurately.

## Download

Builds for each release are on the
[releases page](https://github.com/overnightpillow/rclone-browser/releases).

| Platform | File | Install |
|---|---|---|
| macOS 12+, Apple silicon | `rclone-browser-<version>-macos-arm64.dmg` | Open it, drag the app to Applications |
| Linux x86_64 | `rclone-browser-<version>-linux-x86_64.AppImage` | `chmod +x` it and run it |
| Windows 10/11 x64 | `rclone-browser-<version>-windows-x64-setup.exe` | Run the installer, or take the `.zip` for a portable copy |

Each release also carries `SHA256SUMS`. To check what you downloaded:

```bash
sha256sum -c SHA256SUMS --ignore-missing    # shasum -a 256 -c on macOS
```

**rclone is not bundled.** Install it from
[rclone.org/downloads](https://rclone.org/downloads/) — the application looks
for it on your `PATH` and asks in Preferences if it cannot find it.

**Intel Macs are not built.** Homebrew's Qt is single-architecture, so the disk
image above is arm64 and will not run under Rosetta, which only translates the
other direction. Building from source on an Intel Mac works — see
[Building](#building).

### macOS: "cannot be opened because the developer cannot be verified"

The bundle is ad-hoc signed rather than notarized, because notarizing needs a
paid Apple Developer account. macOS therefore quarantines it on first launch.
After dragging it to Applications:

```bash
xattr -dr com.apple.quarantine /Applications/rclone-browser.app
```

### Linux: what the AppImage needs

It bundles Qt, so the only requirement is a reasonably current system: **glibc
2.39 or newer** (Ubuntu 24.04, Debian 13, Fedora 40 and up), plus FUSE for the
AppImage itself, which most desktops already have. Without FUSE:

```bash
./rclone-browser-*.AppImage --appimage-extract-and-run
```

The floor comes from building on Ubuntu 24.04, which is also the oldest Ubuntu
carrying the Qt 6.4 this needs. On an older distribution, build from source.

### Windows: "Windows protected your PC"

The installer is unsigned — a code-signing certificate is a yearly cost —
so SmartScreen warns about it. **More info**, then **Run anyway**. The portable
zip avoids the installer entirely.

Windows builds are produced by CI but, unlike macOS and Linux, **nobody has
run one yet**. See [Status](#status).

## What this fork changes

### It builds again

Upstream targets Qt5 and declares `cmake_minimum_required(VERSION 2.8)`, which
CMake 4.x rejects outright — it does not configure at all on a current
toolchain. This fork targets **Qt6 and CMake 3.21+**.

- `AUTOMOC`/`AUTOUIC`/`AUTORCC` replace the hand-rolled `qt5_wrap_*` calls and
  the MSVC precompiled-header macro.
- `QtWinExtras` and `QtMacExtras` (both removed in Qt6) are gone.
- `-Werror` is opt-in via `-DRRM_WERROR=ON` rather than pinned on. Upstream
  hard-coded it, which is much of why the build rotted: every new compiler
  diagnostic became a build failure.

### One rclone process per listing instead of two

Upstream ran `rclone lsd` **and** `rclone lsl --max-depth 1` for every directory
you expanded, then parsed both text outputs with regular expressions. This fork
runs a single `rclone lsjson` and parses JSON — half the process spawns, and no
more filename or locale scraping bugs.

Listing failures now show an error row. Previously a failed listing was
indistinguishable from an empty directory.

### Restic snapshot browsing

Browse a restic repository read-only: snapshots, their file trees, and
restore-to-local. No `backup`, `forget`, or `prune` — this is a viewer.

restic can use rclone as a backend, so a repository on S3, B2, or Storj is
reachable **through an rclone remote you have already configured**, with no
second copy of the credentials:

```
restic -r rclone:my-s3-remote:backups/laptop
```

Three ways in:

- **Right-click a folder while browsing a remote** → *Open as Restic
  Repository* (one-shot) or *Save as Restic Repository…* (persists it).
- **Right-click a remote** in the remotes panel → *Add as restic repository…*
- **Restic Repositories…** in the Remotes panel, for repositories not backed by
  an rclone remote (`s3:https://…`, `b2:bucket:path`, or a local path).

Saved repositories appear in the remotes panel under their own heading,
alongside the rclone remotes.

Note that `restic ls` has no depth limit, so expanding a snapshot fetches its
entire file tree in one call and caches it. Fast for typical snapshots, but
memory-hungry for ones containing millions of files.

#### Repository passwords

The **Password command** field takes a shell command that prints the password
on stdout. restic runs it directly, so the password never passes through this
application and is never written to its settings file.

**macOS keychain** (recommended — encrypted at rest, unlocked with your login).
Store it once:

```bash
security add-generic-password -a "$USER" -s restic-my-repo -w
```

That prompts without echoing, so the password never enters your shell history.
Then set the password command to:

```bash
security find-generic-password -a "$USER" -s restic-my-repo -w
```

Use a distinct `-s` name per repository to point each at its own entry.

**From a file** — lock it down first, or it is no better than storing it in
plain text:

```bash
chmod 600 ~/.config/restic/my-repo.key
```

```bash
cat ~/.config/restic/my-repo.key
```

**Environment variables do not work.** The application is launched by the
system, not from your shell, so it does not inherit anything exported in
`.zshrc` or `.bash_profile`. Setting `RESTIC_PASSWORD` there has no effect on a
GUI launch.

**Leaving the field empty** keeps the default: you are prompted once per
session and the password is held in memory only, never written to disk.

The command is run through `sh`, so variables, pipes and `&&` all work, and a
command that prints your password in a terminal will behave the same here.

That wrapping is deliberate. restic splits `RESTIC_PASSWORD_COMMAND` into
arguments and execs it directly, with no shell — so `"$USER"` would be passed
through literally and the keychain lookup above would fail with `security` exit
44, "item not found". Passing it to `sh` gives the field the semantics it looks
like it has.

If the command fails or prints nothing, restic reports an authentication
failure, which surfaces as an error row in the snapshot list.

### Appearance

The interface had two visual languages: a set of custom 320×320 backend logos
used only by the remotes list, and Qt's platform icons everywhere else. The
logos are gone and the remotes list is plain text in two labelled sections.

Style sheets in Qt cannot reference palette roles — there is no `palette(base)`
function — so any colour written into one is fixed at author time and wrong in
the other colour scheme. The style sheet is therefore **generated from the live
palette** and regenerated when the system switches between light and dark, so
dark mode works and updates without a relaunch.

Also: no alternating row stripes (Qt paints them over the whole viewport, not
just the rows that exist), taller rows, flattened headers and toolbars, muted
secondary columns, and tabs whose active one is marked by weight, background
and an accent underline.

### Fixes

- Settings landed at `/rclone-browser/rclone-browser.ini` — the filesystem root
  — on any Linux system not exporting `XDG_CONFIG_HOME`, which is most of them.
- The rclone version check called `std::stoi` with no exception handling, so a
  version string like `1.65.0-beta` terminated the application.
- Directories on B2 and S3 are synthetic and have no modification time; rclone
  reports a sentinel that rendered as `1999-12-31`. Now shown as blank.
- File sizes under 10 bytes rendered as the literal string `0`.
- `rclone config` on macOS wrote a shell script to a fixed, world-readable
  `/tmp/rclone_config.command` and marked it executable. Now an unguessable
  temporary path with owner-only permissions.
- Tasks could only be created from the transfer dialog inside a remote; the
  Tasks tab now has a **New** button.

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

## Tests

```bash
ctest --test-dir build --output-on-failure
```

Three layers, all headless — the widget tests set `QT_QPA_PLATFORM=offscreen`,
so nothing opens a window or steals focus:

- **Logic** — size and timestamp formatting, version comparison. Every case is
  a regression test for a bug that was actually present.
- **Parsers** — `rclone lsjson` and `restic snapshots`/`ls`, against fixtures
  captured from rclone 1.71 and restic 0.18.1. Covers directories reporting
  `Size: -1`, empty listings, malformed JSON, filenames with quotes and
  newlines, and the JSON Lines framing of `restic ls`.
- **Widgets** — offscreen construction, selection and destruction. Deliberately
  shallow: *does interacting with this crash*, not *does it look right*.

The widget layer earns its place — it reproduces a real use-after-free that
shipped during development, headlessly, in under a second.

Disable with `-DRRM_BUILD_TESTS=OFF`. CI runs them on Linux and macOS.

## Releasing

Pushing a tag of the form `2.0.1` or `v2.0.1` runs
[`.github/workflows/release.yml`](.github/workflows/release.yml), which builds
all three artifacts, checksums them, and opens a **draft** release. Publishing
is left as a deliberate step, since nothing is signed by a certificate any
operating system trusts.

Each artifact can also be built by hand, which is how they are developed:

```bash
./scripts/release_macOS.sh --dmg     # macOS only, needs Homebrew Qt
./scripts/release_AppImage.sh        # Linux only
pwsh scripts/release_windows.ps1     # Windows only, Qt on PATH
```

**macOS.** `macdeployqt` alone is not sufficient. It copies the frameworks in
but leaves the build-time rpath pointing at the Homebrew Qt, so dyld loads Qt
twice and warns that classes are "implemented in both"; it also leaves each
copied library calling itself by its Homebrew path. The script fixes both,
re-signs (every `install_name_tool` edit invalidates the signature), then walks
every Mach-O in the bundle and fails if any of them still depends on — or names
itself after — a path outside it. The disk image is verified and mounted, and
the application inside it is checked for a valid signature before the script
reports success.

**Linux.** `linuxdeploy` with the Qt plugin, on Ubuntu 24.04 so that the glibc
floor is chosen rather than inherited from whatever the runner happens to be.
The offscreen platform plugin is bundled alongside `xcb` so the result can be
started without a display; the script does exactly that as its final check.

**Windows.** `windeployqt` plus Inno Setup, producing both an installer and a
portable zip. This one has never run outside CI — see [Status](#status).

## Status

**macOS is verified** on Qt 6.11: builds clean with `-Werror`, tests pass, and
the rclone and restic browsers have both been driven against real B2 and Storj
remotes, in light and dark mode.

**Linux builds and tests clean** on Ubuntu 24.04 — GCC 13, Qt 6.4.2, `-Werror`
— in CI on every push, alongside macOS. The application has not been driven as
a GUI there, so treat the interface as unproven even though the code is not.

**Windows is built but unproven.** The release workflow compiles it and
produces an installer, so the Qt6 replacement for the Windows-only icon path
(`QImage::fromHICON`, replacing the removed `QtWin::fromHICON`) does at least
go through a Windows compiler now. Nobody has launched the result. If you try
it, an issue saying so either way is genuinely useful.

Qt 6.4 is the floor, set by Ubuntu 24.04 LTS and enforced in `find_package`.

### Planned

- Clean up the transfer/task dialog, the densest surface left.
- Pick the remote destination rather than typing `remote:path` as free text.
- A restic backup task type, so scheduling covers rclone and restic uniformly.
- Scheduled jobs on top of that.

## License

MIT, inheriting upstream's copyright notices. See [LICENSE](LICENSE).

The application icon is rclone's logo, designed by Andreas Chlupka and carried
over from upstream.
