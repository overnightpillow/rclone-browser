#include "parsing.h"
#include "formatting.h"

namespace {
// Qt6 removed QRegExp, and QRegularExpression has no exactMatch(), so the
// patterns that must match a whole line are wrapped in anchoredPattern().
//
// File-scope because these were previously constructed inside the readyRead
// handler, recompiling eleven patterns for every chunk of rclone output.
QRegularExpression anchored(const QString &pattern) {
  return QRegularExpression(QRegularExpression::anchoredPattern(pattern));
}

// Until rclone 1.42: "Transferred:   100 Bytes (50 Bytes/sec)"
const QRegularExpression rxTotalsOld(
    anchored(R"(Transferred:\s+(\S+ \S+) \(([^)]+)\))"));

// From 1.43: "Transferred:  1.234G / 5.678 GBytes, 22%, 1.234 MBytes/s, ETA 1h2m3s"
// From 1.56: "Transferred:  3.027 MiB / 120 MiB, 3%, 3.027 MiB/s, ETA 38s"
//
// One pattern covers both: the size groups accept an optional space between
// the number and its unit. The percentage and ETA are \S+ rather than digits
// because rclone prints "-" for either until it has enough information --
// which is exactly the state a stalled transfer sits in, so refusing to match
// it would blank the panel when the user most wants to read it.
const QRegularExpression rxTotals(anchored(
    R"(Transferred:\s+([\d.]+\s*\S*)\s+/\s+([\d.]+\s*\S+),\s+(\S+),\s+([\d.]+\s*\S+),\s+ETA\s+(\S+))"));

// Deliberately not anchored: rclone appends "(retrying may help)" to the count,
// and an anchored pattern silently dropped every error line that carried it.
const QRegularExpression rxErrors(R"(Errors:\s+(\d+))");

// Until rclone 1.42
const QRegularExpression rxChecksOld(anchored(R"(Checks:\s+(\S+))"));
// From 1.43, with the ", Listed N" suffix rclone added in 1.60 made optional.
const QRegularExpression rxChecks(
    anchored(R"(Checks:\s+(\S+) / (\S+), ([0-9%-]+)(?:,\s+.*)?)"));

// Until rclone 1.42
const QRegularExpression rxFileCountOld(anchored(R"(Transferred:\s+(\S+))"));
// From 1.43
const QRegularExpression rxFileCount(
    anchored(R"(Transferred:\s+(\S+) / (\S+), ([0-9%-]+))"));

const QRegularExpression rxElapsed(anchored(R"(Elapsed time:\s+(\S+))"));

// Until rclone 1.38: "* file: 50% done.(ETA: 1h2m3s)"
const QRegularExpression rxFileOld(
    anchored(R"(\*([^:]+):\s*([^%]+)% done.+\((ETA: [^)]+)\))"));
// From 1.39: "* file:  5% /120Mi, 2.014Mi/s, 56s"
//
// The rate and ETA are \S+ for the same reason as above: rclone prints "0/s"
// and "-" for the first second or so of every transfer.
const QRegularExpression rxFile(
    anchored(R"(\*([^:]+):\s*([^%]+)% /\S+, \S+/s, \S+)"));
} // namespace

RcloneStats ParseRcloneStats(const QString &rawLine) {
  const QString line = rawLine.trimmed();

  RcloneStats stats;
  QRegularExpressionMatch m;

  if ((m = rxTotalsOld.match(line)).hasMatch()) {
    stats.kind = RcloneStats::Totals;
    stats.size = m.captured(1);
    stats.bandwidth = m.captured(2);
  } else if ((m = rxTotals.match(line)).hasMatch()) {
    stats.kind = RcloneStats::Totals;
    stats.size = m.captured(1);
    stats.totalSize = m.captured(2);
    stats.percent = m.captured(3);
    stats.bandwidth = m.captured(4);
    stats.eta = m.captured(5);
  } else if ((m = rxErrors.match(line)).hasMatch()) {
    stats.kind = RcloneStats::Errors;
    stats.text = m.captured(1);
  } else if ((m = rxChecks.match(line)).hasMatch()) {
    stats.kind = RcloneStats::Checks;
    stats.text = m.captured(1) + " / " + m.captured(2) + ", " + m.captured(3);
  } else if ((m = rxChecksOld.match(line)).hasMatch()) {
    stats.kind = RcloneStats::Checks;
    stats.text = m.captured(1);
  } else if ((m = rxFileCount.match(line)).hasMatch()) {
    stats.kind = RcloneStats::FileCount;
    stats.text = m.captured(1) + " / " + m.captured(2) + ", " + m.captured(3);
  } else if ((m = rxFileCountOld.match(line)).hasMatch()) {
    stats.kind = RcloneStats::FileCount;
    stats.text = m.captured(1);
  } else if ((m = rxElapsed.match(line)).hasMatch()) {
    stats.kind = RcloneStats::Elapsed;
    stats.text = m.captured(1);
  } else if ((m = rxFile.match(line)).hasMatch() ||
             (m = rxFileOld.match(line)).hasMatch()) {
    stats.kind = RcloneStats::FileProgress;
    stats.name = m.captured(1).trimmed();
    stats.filePercent = m.captured(2).trimmed().toInt();
    // Everything after the name, which is where the size, rate and ETA sit.
    const int colon = line.indexOf(QLatin1Char(':'));
    stats.fileDetail = colon < 0 ? line : line.mid(colon + 1).trimmed();
  }

  return stats;
}

bool ParseRcloneListing(const QByteArray &json, QVector<RcloneEntry> *entries,
                        QString *error) {
  Q_ASSERT(entries);

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);

  if (parseError.error != QJsonParseError::NoError) {
    if (error) {
      *error = "could not parse rclone lsjson output: " +
               parseError.errorString();
    }
    return false;
  }
  if (!doc.isArray()) {
    if (error) {
      *error = "rclone lsjson output was not a JSON array";
    }
    return false;
  }

  for (const QJsonValue &value : doc.array()) {
    const QJsonObject object = value.toObject();

    RcloneEntry entry;
    entry.isFolder = object.value("IsDir").toBool();
    entry.name = object.value("Name").toString();
    entry.modified = FormatModTime(object.value("ModTime").toString());

    if (!entry.isFolder) {
      // Directories report -1 on remotes, or a meaningless inode size locally.
      const qint64 size = object.value("Size").toVariant().toLongLong();
      entry.size = size > 0 ? quint64(size) : 0;
    }

    entries->append(entry);
  }

  return true;
}

bool ParseResticSnapshots(const QByteArray &json,
                          QVector<ResticSnapshot> *snapshots, QString *error) {
  Q_ASSERT(snapshots);

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);

  if (parseError.error != QJsonParseError::NoError) {
    if (error) {
      *error = "could not parse restic snapshots output: " +
               parseError.errorString();
    }
    return false;
  }
  if (!doc.isArray()) {
    if (error) {
      *error = "restic snapshots output was not a JSON array";
    }
    return false;
  }

  for (const QJsonValue &value : doc.array()) {
    const QJsonObject object = value.toObject();

    ResticSnapshot snapshot;
    snapshot.id = object.value("id").toString();
    snapshot.shortId = object.value("short_id").toString();
    snapshot.hostname = object.value("hostname").toString();
    snapshot.time = FormatModTime(object.value("time").toString());

    for (const QJsonValue &path : object.value("paths").toArray()) {
      snapshot.paths << path.toString();
    }

    snapshots->append(snapshot);
  }

  // Newest first.
  std::reverse(snapshots->begin(), snapshots->end());

  return true;
}

bool ParseResticNodes(const QByteArray &jsonLines, QVector<ResticNode> *nodes,
                      QString *error) {
  Q_ASSERT(nodes);
  Q_UNUSED(error);

  for (const QByteArray &line : jsonLines.split('\n')) {
    if (line.trimmed().isEmpty()) {
      continue;
    }

    const QJsonObject object = QJsonDocument::fromJson(line).object();
    // The first record describes the snapshot, not a file.
    if (object.value("struct_type").toString() != "node") {
      continue;
    }

    const QString path = object.value("path").toString();
    if (path.isEmpty()) {
      continue;
    }

    ResticNode node;
    node.path = path;
    node.name = object.value("name").toString();
    node.isDir = object.value("type").toString() == "dir";
    node.modified = FormatModTime(object.value("mtime").toString());
    if (!node.isDir) {
      const qint64 size = object.value("size").toVariant().toLongLong();
      node.size = size > 0 ? quint64(size) : 0;
    }

    nodes->append(node);
  }

  return true;
}
