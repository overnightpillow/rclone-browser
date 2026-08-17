#pragma once

#include "pch.h"

// Parsers for the JSON that rclone and restic produce. These live outside the
// models so they can be exercised without spawning a process: previously they
// sat inside QProcess::finished lambdas and were unreachable from a test.

// One entry of an "rclone lsjson" listing.
struct RcloneEntry {
  QString name;
  // Path as rclone reports it, relative to the directory that was listed.
  // Usually the name again, but not on every backend -- see ChildRemotePath in
  // remote_path.h. Falls back to the name when the backend omits it.
  QString path;
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

// Reads one flag out of "rclone backend features <remote>:", which reports
// what a backend can do as a JSON object of booleans under "Features".
// Returns false when the output cannot be parsed or does not carry the flag,
// leaving value untouched -- an unknown answer is not a "no".
bool ParseRcloneFeature(const QByteArray &json, const QString &feature,
                        bool *value);

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

// One progress record from a restic command run with --json.
//
// Only restore is parsed here, and only the two records it emits: a "status"
// roughly once a second while it runs, and one "summary" at the end. restic
// omits fields it has nothing to say about yet -- files_restored is absent
// until the first file is complete -- so every field has to survive being
// missing.
struct ResticProgress {
  enum Kind {
    // Not a progress record: another message type, or not JSON at all.
    Other,
    Status,
    Summary,
  };

  Kind kind = Other;

  // 0 to 100. Negative when restic reported no percentage, which happens on
  // the first records before it knows the total.
  double percent = -1;

  quint64 bytesDone = 0;
  quint64 totalBytes = 0;
  quint64 filesDone = 0;
  quint64 totalFiles = 0;
};

// Parses one line of restic's --json output. Returns false for anything that
// is not a status or summary record, including blank lines, log lines and
// error records, leaving progress untouched.
bool ParseResticProgress(const QByteArray &line, ResticProgress *progress);

// With --json, restic reports failures as JSON too:
//
//   {"message_type":"exit_error","code":12,"message":"Fatal: wrong password"}
//
// Returns the human-readable part, so what reaches the output pane is the
// sentence rather than the record. Empty for anything else, including the
// plain-text output of every other command.
QString ResticMessageText(const QByteArray &line);
