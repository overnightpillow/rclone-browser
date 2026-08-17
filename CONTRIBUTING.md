# Contributing

This is a continuation of [kapitainsky/RcloneBrowser](https://github.com/kapitainsky/RcloneBrowser),
which has had no commits since December 2020. Issues and pull requests opened
against that repository will not be seen here, and fixes made here are not
pushed back there.

It is maintained by one person alongside other work. Issues are read and
welcome, but there is no response-time commitment.

## Where to file things

The public tracker is [GitHub Issues](https://github.com/overnightpillow/rclone-browser/issues).

For a bug, the useful details are your OS, your Qt version, the output of
`rclone version`, and — if a listing or a snapshot is involved — the command
shown in the error row.

## How code reaches this repository

GitHub is a **mirror**. Development happens on a private Gitea instance, which
push-mirrors here; nothing is committed on GitHub directly.

This matters for pull requests. Mirror syncs overwrite branches and prune refs
that no longer exist upstream, so **a pull request merged on GitHub would be
erased by the next sync**, silently and with no record. Pull requests are still
the right way to propose a change and CI runs on them as normal — but they are
landed by applying the commits on the Gitea side, which then mirrors back here.
Expect your PR to be closed with a reference to the commit that carries your
work rather than showing GitHub's purple "merged" badge. Authorship is
preserved; the commit keeps your name.

Issues, comments and releases are unaffected — the mirror only moves git refs.

## Building and testing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DRRM_WERROR=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

On macOS add `-DCMAKE_PREFIX_PATH="$(brew --prefix qt)"`.

Build with `-DRRM_WERROR=ON` before opening a pull request — CI does, and it is
the setting that keeps the build from rotting the way upstream's did. Tests are
headless (`QT_QPA_PLATFORM=offscreen`), so they need no display.

If you develop on macOS, check the Linux build before you push:

```bash
scripts/preflight_linux.sh
```

It builds the committed tree in an Ubuntu 24.04 container with the same
compiler, Qt version and flags CI uses. Homebrew's Qt is far newer than
Ubuntu's, and clang diagnoses less than GCC — both differences have broken CI
here in ways nothing on a Mac could reveal. Roughly 15 seconds once its image
is built. Wire it in permanently with:

```bash
git config core.hooksPath scripts/hooks
```

## Scope

- **restic support is read-only.** Browsing snapshots and restoring from them
  is in scope; `backup`, `forget` and `prune` are not, for now.
- **Qt6 only.** Qt5 compatibility shims will not be taken.
- The application name, the binary name and the QSettings domain all stay
  `rclone-browser`, so that preferences from 1.8.0 keep loading. Please don't
  submit a rename.

## Help that is especially wanted

**Windows.** The Qt6 replacement for the removed `QtWin::fromHICON` has never
been through a Windows compiler. If you build there, a report either way is
useful.

## Style

Match the surrounding code. Commit messages are a short imperative summary and
a body explaining why the change was needed, not what the diff shows.
