#include "parsing.h"
#include "formatting.h"

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
