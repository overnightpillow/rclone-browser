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

QStringList GetDriveSharedWithMe();
QStringList GetDefaultRcloneOptionsList();
QStringList GetShowHidden();

// Returns 0 if equal, 1 if version1 is newer, 2 if version2 is newer.
// Non-numeric components (e.g. the "0-beta" in "1.65.0-beta") compare as their
// leading integer.
unsigned int compareVersion(const QString &version1, const QString &version2);
