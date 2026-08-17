# Change Log

## [2.0.0] - 2026-08-17

First release of this fork. Upstream's last release was 1.8.0 in February 2020;
2.0.0 marks the Qt6 port and the restic support, not a rename — the application
is still Rclone Browser and still stores its settings under the same name, so
existing preferences and saved tasks carry over untouched.

-   NEW: downloads. A `.dmg` for macOS (Apple silicon), an `.AppImage` for
    Linux (x86_64, glibc 2.39 or newer), and an installer plus a portable
    `.zip` for Windows (x64), built on a tag and published with `SHA256SUMS`.
    Upstream's release scripts targeted Qt5, Visual Studio 2019 and CentOS 7
    and none of them ran any more. Intel Macs are not built: Homebrew's Qt is
    single-architecture, so build from source there.
-   NEW: read-only restic snapshot browsing. Browse snapshots, their file
    trees, and restore to a local folder. Repositories can be reached through
    an rclone remote already configured here (`rclone:remote:path`), so a
    repository on S3, B2 or Storj needs no second copy of the credentials.
    Right-click a remote to open a repository stored on it, or register
    repositories by full restic URL under Restic → Repositories.
-   NEW: restic repository passwords prefer a per-repository password command
    (`RESTIC_PASSWORD_COMMAND`), so the password never passes through this
    application. Without one you are prompted once per session; the password is
    held in memory only and never written to the settings file.
-   NEW: builds against Qt6 and CMake 3.21+. Upstream required Qt5 and declared
    `cmake_minimum_required(VERSION 2.8)`, which CMake 4.x rejects outright.
-   NEW: `-Werror` is opt-in via `-DRRM_WERROR=ON` rather than pinned on, so a
    newer compiler cannot make the project unbuildable again.
-   NEW: Gitea Actions CI replacing the dead Travis and AppVeyor configs.
-   CHANGED: cancelling a job asks rclone to stop rather than killing it, so it
    can close connections and clean up partial files, and the window no longer
    freezes while it does. The row reads "Cancelling" and closes when the
    process has actually exited.
-   CHANGED: dropping several files onto a remote at once opens one transfer
    dialog and runs a transfer per item. Only single-file drops were accepted
    before; a multiple selection did nothing at all.
-   CHANGED: Google Drive's "shared with me" is a property of the tab it is
    ticked in. It was a global setting every tab wrote to, so turning it on in
    one Drive tab silently changed what the others listed.
-   CHANGED: update checks run in the background instead of blocking startup on
    two network round trips, record the day's check only after a response that
    parses — an offline launch used to count as the check for the next 24
    hours — and the application's own check looks at this fork's releases
    rather than upstream's, which have not moved since 2020.
-   CHANGED: saved tasks are written atomically. A crash or a full disk part
    way through a save used to lose every task; a file that cannot be read is
    now kept as `tasks.bin.corrupt` with a warning instead of being discarded
    in silence.
-   CHANGED: remote paths are held as paths rather than as `QDir`, which turns
    an empty path into `"."` — the root of a remote — so listings ran against
    `remote:.` and every path below it read `./name`.
-   CHANGED: directory listings use a single `rclone lsjson` call instead of
    `rclone lsd` plus `rclone lsl --max-depth 1` parsed with regular
    expressions. Half the process spawns per expanded folder, and no more
    text-scraping.
-   CHANGED: modification times display in local time rather than whatever zone
    rclone printed.
-   CHANGED: running a task switches to the Jobs tab, where its progress, its
    per-file transfers and its output are. Running one used to leave you
    looking at the task list, where nothing visibly happened at all.
-   FIXED: a finished transfer said whether it had worked nowhere except in the
    Jobs tab. It now reports "Transfer finished" or "Transfer failed" in the
    status bar and as a desktop notification, a failed job opens its own output
    so the reason is on screen, and cancelling reports nothing since it was
    asked for. The notification was previously posted to a tray icon that is
    hidden unless "always show in tray" is on, and Qt drops a message sent from
    a hidden icon -- so with the default settings it notified nobody.
-   CHANGED: New Folder says so when the remote cannot hold an empty folder.
    On object storage — S3, B2, Storj — a folder is only the leading part of a
    file's name, so `rclone mkdir` reports success and stores nothing: the
    dialog closed as though it had worked and the folder was never there.
-   CHANGED: the operation dialog — Move, Delete and restic Restore — shows a
    progress bar with the bytes done, the rate and an ETA, instead of a static
    "Moving..." label for however long the operation takes. rclone is asked for
    its periodic stats block, and restic restore is run with `--json`, which is
    the only way it reports progress at all when it is not attached to a
    terminal. A restic failure is shown as its sentence rather than as the raw
    JSON record.
-   FIXED: restic repositories sat on "... loading snapshots" forever in the
    released builds. An app launched from Finder inherits `PATH` from launchd,
    which does not include `/opt/homebrew/bin`, so restic was never found;
    the resulting process failed to start, and a process that never starts
    never reports finishing. restic is now looked for in the usual package
    manager locations as well as on `PATH`, its location can be set in
    Preferences, and a helper that cannot be run reports that instead of
    leaving a progress row spinning.
-   FIXED: the transfer details panel had been blank since rclone 1.56 changed
    its stats format four years ago: Size, Total size, Bandwidth and ETA stayed
    empty for every transfer. Three smaller breaks in the same output are fixed
    with it — the error count was dropped whenever rclone appended
    "(retrying may help)", a file's progress bar did not appear until rclone
    had a transfer rate to report, and the Checks line stopped parsing when
    ", Listed N" was added in 1.60.
-   FIXED: Move moved a folder's *contents* into the destination and left the
    folder behind, empty. It moves the folder.
-   FIXED: the section headings in the remotes list were unreadable in dark
    mode: present, taking up space, and near-black on near-black.
-   FIXED: a failed directory listing was indistinguishable from an empty
    directory; failures now show an error row with rclone's message.
-   FIXED: the job log was emptied wholesale every 10,000 lines, and the stream
    and mount logs grew without limit.
-   FIXED: the Jobs tab's "copy command" button produced a command that could
    not be pasted into a shell if any path contained a space.
-   FIXED: a blank line in the exclude box, or a trailing newline, passed
    `--exclude ""` to rclone.
-   FIXED: the single-instance lock used one fixed name in the temporary
    directory, so on Linux the second person logged into a machine was told the
    application was already running.
-   FIXED: unmounting froze the window until it completed or timed out, and a
    stream that ended on its own could crash the application on close.
-   FIXED: `rclone config` on Linux knew three terminals and passed `-e`, which
    gnome-terminal removed in 3.38, so the button did nothing on a current GNOME
    desktop.
-   FIXED: Windows would not compile at all. Three errors, each invisible to
    every other platform: a call left on the old `std::string` signature of the
    version comparison, `Qt::AA_DisableWindowContextHelpButton` (removed in Qt
    6), and `windows.h`'s `min`/`max` macros eating `std::max`. CI now builds
    and tests Windows on every push, so this cannot hide again. It has been
    compiled and tested there, but nobody has yet launched it.
-   FIXED: on Linux, settings were written to `/rclone-browser/rclone-browser.ini`
    at the filesystem root whenever `$XDG_CONFIG_HOME` was unset, which is the
    normal case. Now falls back to `~/.config` per the XDG spec.
-   FIXED: the rclone version check crashed the application on any version
    string with a non-numeric component, such as `1.65.0-beta`.
-   FIXED: file sizes below 10 bytes displayed as the literal string `0`.
-   FIXED: `rclone config` on macOS wrote a shell script to a fixed,
    world-readable `/tmp/rclone_config.command` and marked it executable, which
    another user on the machine could pre-create. Now uses an unguessable
    temporary path with owner-only permissions.
-   FIXED: the macOS release script could not run on Apple Silicon, and
    `macdeployqt` alone left the bundle loading Qt twice.

## [1.8.0][1.8.0] - 2020-02-17
-   NEW: http(s) proxy configuration for rclone
-   NEW: remotes icons size option selector
-   NEW: directories tree display for remotes
-   NEW: rclone extra default options for all operations (e.g. --fast-list)
-   NEW: added "Public Link" button to remote view
-   FIXED: option to show hidden files and folders was not always working as expected
-   FIXED: for sftp server default to home user directory (as normal sftp would do)
-   FIXED: an issue when on Windows local remote only allowed to browse drive C:
-   FIXED: problem using rclone and rclone.conf when path contained spaces
-   FIXED: bandwidth box on jobs tab is too small for fast connections
-   bunch of usual small tweaks and fixes

## [1.7.0][1.7.0] - 2019-11-27
-   NEW: built all releases with the latest Qt 5.13.2
-   NEW: changed Linux releases format to AppImage only
-   NEW: changed macOS release format to dmg image file
-   NEW: added installer for Windows releases - implemented using [Inno Setup](https://github.com/jrsoftware/issrc)
-   NEW: added Linux i386 release
-   NEW: changed macOS release compilation options to make it work on all macOS versions starting with 10.9
-   NEW: added portable mode for macOS and Linux
-   NEW: on Linux multiple terminals are tried for rclone config ($TERMINAL then gnome-terminal followed by xfce4-terminal, xterm, x-terminal-emulator and konsole)
-   NEW: enabled Qt HighDpiScaling - should help people with high DPI monitors
-   NEW: added dark mode - configurable via preferences or system setting (newer macOS) - thank you @noaione for initial PR
-   changed preferences window - added tabs to create more space for new options
-   fixed Windows portable mode
-   fixed mount/unmount on FreeBSD
-   disabled mount on OpenBSD and NetBSD (as not supported by rclone)
-   updated build and install for Linux - now all files will be installed in /usr/local root
-   fixed possible crashes when old rclone is used (with different version information output)
-   fixed an issue with long file names leading sometimes to inaccurate transfer progress bar display
-   added additional info to file progress bar tooltip - individual file stats
-   changed program icon
-   bunch of usual small tweaks and fixes

## [1.6.0][1.6.0] - 2019-10-27
-   fixed Windows mount/unmount (requires rclone v1.50+)
-   Rclone Browser checks now for used rclone version (mount is disabled in Windows if rclone <v1.50)
-   added default download/upload folders - configurable in settings
-   add default download/upload extra options - configurable in settings
-   added available updates' notifications for both Rclone Browser and rclone - can be turned on/off in settings
-   all mount options are configurable via settings - generic "rclone mount remote local" is used without any options specified
-   default mount option (in settings) is "--vfs-cache-mode writes"
-   Google Drive with "shared with me" option on is always mounted as read-only
-   Windows deployment includes now all required runtime files for users without MSVCR installed
-   added ftp, MS Azureblob and Google Photos remote icons
-   modified main application window status bar to save space
-   released binary for Windows 32 bits
-   released binary for armhf 32 bits - for Raspberry Pi running raspbian
-   bunch of usual small tweaks and fixes

## [1.5.3][1.5.3] - 2019-10-24
-   Windows only update - include all required runtime dll files

## [1.5.2][1.5.2] - 2019-09-27
-   code cleanup - clean compilation with -Werror enabled, GCC8 compilation fixed
-   add tooltips showing rclone options used to all transfer window options
-   Google "drive shared with me" caused multiple of issues - now all should work
-   as always small cosmetic UI improvements - still plenty to do but core functionality was first

## [1.5.1][1.5.1] - 2019-09-25
-   after task edit initiated by double click main window does not get proper focus back and subsequent Run click might lead to wrong task execution. For time being I disable double click edit - until proper fix is produced.

## [1.5][1.5] - 2019-09-25
-   tasks - jobs can be saved/edited/run/deleted. No need creating the same job again and again.
-   on Google drive DriveSharedWithMe can be mounted to local filesystem
-   DriveSharedWithMe checkbox is now disabled for non Google destinations - it is Google only feature and turning it on for other destinations does not make sense - could even crash the browser.
-   verbose option is now always on and has been removed from UI - which means that stats will be always displayed. No more wondering how long it is going to take for some long job to finish.
-   fixed an issue with local remote on Windows when local drive content was not properly displayed
-   replaced remote Amazon icon with generic S3 one. S3 became name on its own and almost de-facto standard in cloud access used by many rclone supported destinations
-   new application logo

## [1.4.1][1.4.1] - 2019-09-18
-   small GUI tweaks to make all progress fields always visible (they were too small for large transfers) and adjust some screen sizes to make all GUI elements visible
-   update all builds with latest Qt (5.13.1)

## [1.4][1.4] - 2019-08-23
-   Fix compliation errors and update all builds with latest Qt (5.13)
-   Fix Config button command
-   Further fix and tweak progress display. Add ETA and Total Size fields
-   Fix remotes icons display
-   Add sftp icon
-   Fix progress display for rclone > 1.37 (by DinCahill)
-   Add a Public Link option to the right-click menu (by DinCahill)
-   Add preference: Show hidden files and folders (by DinCahill)
-   Add Mega icon (by DinCahill)
-   Refresh when Shared is toggled (by DinCahill)
-   Disable Upload button for Shared (by DinCahill)
-   Support for shared Google Drive files. Enable the checkbox when you open a remote, and all rclone commands will be passed --drive-shared-with-me (by DinCahill)
-   Set cache mode for mounts (by DinCahill)
-   Fixed missing leading / in path (required for some SFTP servers) (by DinCahill)

## [1.2][1.2] - 2017-03-11
-   Calculate size of folders, issue #4
-   Copy transfer command to clipboard, issue #20
-   Support custom .rclone.conf location, #21
-   Export list of files, issue #27
-   Bugfix for folder refresh not working after rename, issue #30
-   Remember empty text fields in transfer dialog, issue #32
-   Error message when too old rclone version is selected
-   Support portable mode, issue #28
-   Create .deb packages, issue #26

## [1.1][1.1] - 2017-01-31
-   Added `--transfer` option in UI, issue #1
-   Supports encrypted `.rclone.conf` configuration file, issue #2
-   Fixed crash when canceling active stream
-   Added ETA tooltip for transfer progress bars
-   Allow to specify extra arguments for rclone, issue #7
-   Fix for browsing Hubic remotes, issue #10
-   Support high-dpi mode for macOS

## [1.0.0][1.0.0] - 2017-01-29
-   Allows to browse and modify any rclone remote, including encrypted ones
-   Uses same configuration file as rclone, no extra configuration required
-   Simultaneously navigate multiple repositories in separate tabs
-   Lists files hierarchically with file name, size and modify date
-   All rclone commands are executed asynchronously, no freezing GUI
-   File hierarchy is lazily cached in memory, for faster traversal of folders
-   Allows to upload, download, create new folders, rename or delete files and folders
-   Can process multiple upload or download jobs in background
-   Drag & drop support for dragging files from local file explorer for uploading
-   Streaming media files for playback in player like mpv or similar
-   Mount and unmount folders on macOS and GNU/Linux
-   Optionally minimizes to tray, with notifications when upload/download finishes

[1.8.0]: https://github.com/kapitainsky/RcloneBrowser/releases/tag/1.8.0
[1.7.0]: https://github.com/kapitainsky/RcloneBrowser/releases/tag/1.7.0
[1.6.0]: https://github.com/kapitainsky/RcloneBrowser/releases/tag/1.6.0
[1.5.3]: https://github.com/kapitainsky/RcloneBrowser/releases/tag/1.5.3
[1.5.2]: https://github.com/kapitainsky/RcloneBrowser/releases/tag/1.5.2
[1.5.1]: https://github.com/kapitainsky/RcloneBrowser/releases/tag/1.5.1
[1.5]: https://github.com/kapitainsky/RcloneBrowser/releases/tag/1.5
[1.4.1]: https://github.com/kapitainsky/RcloneBrowser/releases/tag/1.4.1
[1.4]: https://github.com/kapitainsky/RcloneBrowser/releases/tag/1.4
[1.2]: https://github.com/mmozeiko/RcloneBrowser/releases/tag/1.2
[1.1]: https://github.com/mmozeiko/RcloneBrowser/releases/tag/1.1
[1.0.0]: https://github.com/mmozeiko/RcloneBrowser/releases/tag/1.0.0
