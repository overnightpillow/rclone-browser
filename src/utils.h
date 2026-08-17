#pragma once

#include "pch.h"

std::unique_ptr<QSettings> GetSettings();

void ReadSettings(QSettings *settings, QObject *widget);
void WriteSettings(QSettings *settings, QObject *widget);

bool IsPortableMode();

QString GetRclone();
void SetRclone(const QString &rclone);

QStringList GetRcloneConf();
void SetRcloneConf(const QString &rcloneConf);

void UseRclonePassword(QProcess *process);
void SetRclonePassword(const QString &rclonePassword);

// Shared presentation for the tree views. The rclone browser and the restic
// browser are the same kind of surface and were drifting apart, each setting
// its own subset of view properties.
void StyleTreeView(QTreeView *tree);

// Shared presentation for the tab tool bars, so actions read the same way
// wherever they appear.
void StyleToolBar(QToolBar *toolBar);

// There is deliberately no GetDriveSharedWithMe() here any more: "shared with
// me" is a property of one Drive tab, and reading it from a global setting is
// what let two tabs fight over it. ItemModel and RemoteWidget each hold their
// own, and pass it to whatever they run.
QStringList GetDefaultRcloneOptionsList();
QStringList GetShowHidden();

// Joins an argument list into a command line that can be pasted into a
// terminal, quoting the arguments that need it for the platform's shell.
QString BuildCommandLine(const QStringList &args);

// Returns 0 if equal, 1 if version1 is newer, 2 if version2 is newer.
// Non-numeric components (e.g. the "0-beta" in "1.65.0-beta") compare as their
// leading integer.
unsigned int compareVersion(const QString &version1, const QString &version2);
