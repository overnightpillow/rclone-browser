# Security Policy

## Supported versions

Only the 2.x series, from this repository, is maintained.

Versions 1.8.0 and earlier came from the upstream project, which has been
inactive since 2020 and will not issue fixes. If you are running 1.8.0, note
that it writes a shell script to a fixed, world-readable, executable path
(`/tmp/rclone_config.command`) whenever you open the rclone configuration on
macOS — a local user could replace it between write and execution. That is
fixed in 2.0.0.

## Reporting a vulnerability

Report privately, not as a public issue:

- **Preferred:** GitHub's [private vulnerability reporting](https://github.com/overnightpillow/rclone-browser/security/advisories/new).
- **Or:** email overnightpillow@proton.me.

Please include what an attacker gains and how to reproduce it. You will get an
acknowledgement; this is a one-person project, so a fix may take longer than
the acknowledgement does.

## What is in scope

This application is a front-end: it builds command lines and runs `rclone` and
`restic` as child processes. Relevant to that:

- The **password command** configured per restic repository is executed through
  `sh`, by design, so it accepts pipes and variables. It runs with your
  privileges — treat it as you would any line in your shell profile.
- Repository passwords entered at the prompt are held in memory only and are
  never written to the settings file.
- Credentials for remotes live in rclone's own configuration file, which this
  application reads through rclone and never copies.

Issues in rclone or restic themselves belong to
[rclone](https://github.com/rclone/rclone/security) and
[restic](https://github.com/restic/restic/security).
