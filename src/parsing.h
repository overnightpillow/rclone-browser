#pragma once

#include "pch.h"

// Parsers for the JSON that rclone and restic produce. These live outside the
// models so they can be exercised without spawning a process: previously they
// sat inside QProcess::finished lambdas and were unreachable from a test.

// One entry of an "rclone lsjson" listing.
struct RcloneEntry {
  QString name;
  bool isFolder = false;
  quint64 size = 0;
  // Already formatted for display; empty when the backend has no timestamp.
  QString modified;
};

// Parses one "rclone lsjson" array. Returns false and sets error on malformed
// input. An empty array is valid and yields no entries.
bool ParseRcloneListing(const QByteArray &json, QVector<RcloneEntry> *entries,
                        QString *error);

// One snapshot from "restic snapshots --json".
struct ResticSnapshot {
  QString id;
  QString shortId;
  QString hostname;
  QStringList paths;
  QString time; // formatted
};

// Parses "restic snapshots --json", which is a single JSON array. Snapshots
// are returned newest first; restic emits them oldest first.
bool ParseResticSnapshots(const QByteArray &json,
                          QVector<ResticSnapshot> *snapshots, QString *error);

// One node from "restic ls --json --long".
struct ResticNode {
  QString name;
  // Absolute path within the snapshot, as restic reports it.
  QString path;
  bool isDir = false;
  quint64 size = 0;
  QString modified; // formatted
};

// Parses "restic ls --json --long", which is JSON Lines rather than an array:
// one snapshot object followed by one object per node. Non-node records and
// blank lines are skipped. Malformed individual lines are skipped rather than
// failing the whole listing, since a single bad record should not hide an
// entire snapshot.
bool ParseResticNodes(const QByteArray &jsonLines, QVector<ResticNode> *nodes,
                      QString *error);
