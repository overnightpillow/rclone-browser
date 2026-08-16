#pragma once

#include "pch.h"

// A restic repository the browser knows about.
//
// "repository" is passed to restic as -r, so it accepts anything restic does:
// a local path, "s3:https://...", "b2:bucket:path", or -- the case this app is
// built around -- "rclone:remote:path", which lets restic reach S3/B2/Storj
// through the rclone remotes already configured here, with no second copy of
// the credentials.
struct ResticRepo {
  QString name;
  QString repository;
  // Optional shell command printing the repository password on stdout. When
  // set it is handed to restic as RESTIC_PASSWORD_COMMAND and the password
  // never passes through this process.
  QString passwordCommand;

  bool isValid() const { return !repository.isEmpty(); }
};

// Builds "rclone:<remote>:<path>" for a repository stored on an rclone remote.
QString ResticRepoForRemote(const QString &remote, const QString &path);

QList<ResticRepo> GetResticRepos();
void SetResticRepos(const QList<ResticRepo> &repos);

QString GetRestic();
void SetRestic(const QString &restic);

// Ensures a usable password source for the repository, prompting if the repo
// has no password command and nothing is cached yet. Returns false if the user
// cancelled. Prompted passwords are held in memory for the session only --
// never written to QSettings.
bool EnsureResticPassword(const ResticRepo &repo, QWidget *parent);

void ForgetResticPassword(const ResticRepo &repo);

// Applies RESTIC_REPOSITORY plus the password source to a child process, and
// forwards the rclone config password so "rclone:" backends can open an
// encrypted rclone.conf.
void ApplyResticEnvironment(QProcess *process, const ResticRepo &repo);

// Arguments common to every restic invocation for this repository. Includes
// "-o rclone.program=..." for rclone-backed repositories so restic drives the
// same rclone binary configured in preferences.
QStringList ResticBaseArgs(const ResticRepo &repo);
