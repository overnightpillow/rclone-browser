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

## Releasing (macOS)

```bash
./scripts/release_macOS.sh --zip
```

Builds, bundles the Qt frameworks, and produces a self-contained ~88 MB `.app`
in `release/`.

`macdeployqt` alone is not sufficient: it copies the frameworks in but leaves
the build-time rpath pointing at the Homebrew Qt, so dyld loads Qt twice and
warns that classes are "implemented in both". The script strips that rpath,
points it at the bundled frameworks, re-signs (every `install_name_tool` edit
invalidates the signature), and fails if the bundle still links against the Qt
prefix.

The result is ad-hoc signed, which is fine locally. On another Mac it will be
quarantined; clear it with `xattr -dr com.apple.quarantine`.

## Status

**macOS is verified** on Qt 6.11: builds clean with `-Werror`, tests pass, and
the rclone and restic browsers have both been driven against real B2 and Storj
remotes, in light and dark mode.

**Linux builds and tests clean** on Ubuntu 24.04 — GCC 13, Qt 6.4.2, `-Werror`
— in CI on every push, alongside macOS. The application has not been driven as
a GUI there, so treat the interface as unproven even though the code is not.

**Windows is unbuilt.** The Qt6 replacement for the Windows-only icon path
(`QImage::fromHICON`, replacing the removed `QtWin::fromHICON`) has never been
through a Windows compiler, and there is no Windows runner yet.

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
