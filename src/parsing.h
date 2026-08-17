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

// One line of rclone's periodic --stats block, or one of the per-file progress
// lines printed under it.
//
// rclone has changed this format four times: parenthesised totals until 1.42,
// SI units with the number and unit run together ("1.234G / 5.678 GBytes")
// from 1.43, IEC units separated by a space ("3.027 MiB / 120 MiB") from 1.56,
// and a ", Listed N" suffix on the Checks line from 1.60. All four are parsed
// here so that a job started against any rclone still fills the details panel.
struct RcloneStats {
  enum Kind {
    Unknown,
    // Bytes moved, with the total, percentage, rate and ETA.
    Totals,
    Errors,
    Checks,
    // "Transferred: 1 / 1, 100%" -- files, not bytes. Shares its prefix with
    // Totals, so it is only recognised once Totals has been ruled out.
    FileCount,
    Elapsed,
    // One "* name: 12% /120Mi, 2Mi/s, 56s" line.
    FileProgress,
  };

  Kind kind = Unknown;

  // Totals. Display-ready; rclone's own units are kept rather than reformatted.
  QString size;
  QString totalSize;
  // "3%", or "-" until rclone knows the total.
  QString percent;
  QString bandwidth;
  // "38s", or "-" until rclone can estimate one.
  QString eta;

  // Errors, Checks, FileCount and Elapsed, already formatted for their field.
  QString text;

  // FileProgress.
  QString name;
  int filePercent = 0;
  // Everything after the file name, for the progress bar's tooltip.
  QString fileDetail;
};

// Parses one line of rclone output. Leading and trailing whitespace is
// ignored. A line that is not part of the stats block returns kind Unknown,
// which is the common case: most of rclone's output is log lines.
RcloneStats ParseRcloneStats(const QString &line);

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
