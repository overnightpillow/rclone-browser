#include "restic_model.h"
#include "utils.h"

namespace {

QString niceSize(quint64 size) {
  static const char prefix[] = " KMGTPE";
  for (int i = sizeof(prefix) - 2; i >= 1; i--) {
    const quint64 base = quint64(1) << (i * 10);
    if (size >= 10 * base) {
      return QString("%1 %2").arg(size / base).arg(QChar(prefix[i]));
    }
  }
  return QString::number(size);
}

// restic emits RFC3339 with nanosecond precision; Qt parses at most
// milliseconds. Rendered fixed-width so string sorting stays chronological.
QString formatTime(const QString &rfc3339) {
  if (rfc3339.isEmpty()) {
    return QString();
  }

  QString trimmed = rfc3339;
  static const QRegularExpression subSecond(R"(\.(\d{3})\d*)");
  trimmed.replace(subSecond, ".\\1");

  const QDateTime parsed = QDateTime::fromString(trimmed, Qt::ISODateWithMs);
  if (!parsed.isValid()) {
    return rfc3339;
  }
  return parsed.toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
}

// Folders first, then case-insensitive natural order -- restic emits nodes in
// tree order, which interleaves them.
void sortTree(ResticItem *item) {
  std::sort(item->children.begin(), item->children.end(),
            [](const ResticItem *a, const ResticItem *b) {
              if (a->isFolder != b->isFolder) {
                return a->isFolder;
              }
              return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
            });

  for (ResticItem *child : item->children) {
    sortTree(child);
  }
}

} // namespace

ResticModel::ResticModel(const ResticRepo &repo, QObject *parent)
    : QAbstractItemModel(parent), mRepo(repo) {
  QStyle *style = qApp->style();
  mSnapshotIcon = style->standardIcon(QStyle::SP_DriveHDIcon);
  mFolderIcon = style->standardIcon(QStyle::SP_DirIcon);
  mFileIcon = style->standardIcon(QStyle::SP_FileIcon);

  mRoot = new ResticItem();
  mRoot->isFolder = true;

  loadSnapshots();
}

ResticModel::~ResticModel() { delete mRoot; }

ResticItem *ResticModel::get(const QModelIndex &index) const {
  return index.isValid() ? static_cast<ResticItem *>(index.internalPointer())
                         : mRoot;
}

bool ResticModel::isSnapshot(const QModelIndex &index) const {
  return get(index)->isSnapshot;
}

bool ResticModel::isFolder(const QModelIndex &index) const {
  return get(index)->isFolder;
}

bool ResticModel::isPlaceholder(const QModelIndex &index) const {
  return get(index)->isPlaceholder;
}

QString ResticModel::snapshotId(const QModelIndex &index) const {
  for (const ResticItem *item = get(index); item && item != mRoot;
       item = item->parent) {
    if (item->isSnapshot) {
      return item->snapshotId;
    }
  }
  return QString();
}

QString ResticModel::path(const QModelIndex &index) const {
  return get(index)->path;
}

QModelIndex ResticModel::index(int row, int column,
                               const QModelIndex &parent) const {
  if (!hasIndex(row, column, parent)) {
    return QModelIndex();
  }
  return createIndex(row, column, get(parent)->children[row]);
}

QModelIndex ResticModel::parent(const QModelIndex &index) const {
  if (!index.isValid()) {
    return QModelIndex();
  }

  ResticItem *child = get(index);
  if (!child->parent || child->parent == mRoot) {
    return QModelIndex();
  }
  return createIndex(child->parent->num(), 0, child->parent);
}

bool ResticModel::hasChildren(const QModelIndex &parent) const {
  ResticItem *item = get(parent);
  if (item->isPlaceholder) {
    return false;
  }
  if (item->isSnapshot || item->isFolder) {
    // Snapshots claim children before loading so they show an expand arrow.
    return !item->loaded || !item->children.isEmpty();
  }
  return false;
}

int ResticModel::rowCount(const QModelIndex &parent) const {
  return get(parent)->children.count();
}

int ResticModel::columnCount(const QModelIndex &parent) const {
  Q_UNUSED(parent);
  return 3;
}

bool ResticModel::canFetchMore(const QModelIndex &parent) const {
  ResticItem *item = get(parent);
  return item->isSnapshot && !item->loaded && !item->loading;
}

void ResticModel::fetchMore(const QModelIndex &parent) {
  ResticItem *item = get(parent);
  if (!item->isSnapshot || item->loaded || item->loading) {
    return;
  }
  loadSnapshotTree(QPersistentModelIndex(parent), item);
}

QVariant ResticModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid()) {
    return QVariant();
  }

  const ResticItem *item = get(index);

  if (role == Qt::DecorationRole && index.column() == 0) {
    if (item->isPlaceholder) {
      return QIcon();
    }
    if (item->isSnapshot) {
      return mSnapshotIcon;
    }
    return item->isFolder ? mFolderIcon : mFileIcon;
  }

  if (role == Qt::TextAlignmentRole && index.column() == 1) {
    return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
  }

  if (role == Qt::ToolTipRole && item->isSnapshot) {
    return QString("Snapshot %1\n%2\nPaths:\n  %3")
        .arg(item->snapshotId, item->modified,
             item->snapshotPaths.join("\n  "));
  }

  if (role == Qt::DisplayRole) {
    switch (index.column()) {
    case 0:
      return item->name;
    case 1:
      if (item->isPlaceholder || item->isFolder || item->isSnapshot) {
        return QString();
      }
      return niceSize(item->size);
    case 2:
      return item->modified;
    }
  }

  return QVariant();
}

QVariant ResticModel::headerData(int section, Qt::Orientation orientation,
                                 int role) const {
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
    switch (section) {
    case 0:
      return "Snapshot / Name";
    case 1:
      return "Size";
    case 2:
      return "Modified";
    }
  }
  return QVariant();
}

void ResticModel::clearChildren(const QPersistentModelIndex &parentIndex,
                                ResticItem *parent) {
  if (parent->children.isEmpty()) {
    return;
  }
  beginRemoveRows(parentIndex, 0, parent->children.count() - 1);
  qDeleteAll(parent->children);
  parent->children.clear();
  endRemoveRows();
}

void ResticModel::setPlaceholder(const QPersistentModelIndex &parentIndex,
                                 ResticItem *parent, const QString &text) {
  clearChildren(parentIndex, parent);

  auto *placeholder = new ResticItem();
  placeholder->parent = parent;
  placeholder->isPlaceholder = true;
  placeholder->name = text;

  beginInsertRows(parentIndex, 0, 0);
  parent->children.append(placeholder);
  endInsertRows();
}

QProcess *ResticModel::startRestic(const QStringList &args) {
  auto *process = new QProcess(this);
  ApplyResticEnvironment(process, mRepo);
  process->start(GetRestic(), ResticBaseArgs(mRepo) + args,
                 QIODevice::ReadOnly);
  return process;
}

void ResticModel::refresh() {
  clearChildren(QPersistentModelIndex(), mRoot);
  mRoot->loaded = false;
  loadSnapshots();
}

void ResticModel::loadSnapshots() {
  const QPersistentModelIndex rootIndex;
  setPlaceholder(rootIndex, mRoot, "... loading snapshots");

  QProcess *process = startRestic(QStringList() << "snapshots" << "--json");

  QObject::connect(
      process, &QProcess::finished, this,
      [this, process, rootIndex](int exitCode, QProcess::ExitStatus status) {
        process->deleteLater();

        if (status != QProcess::NormalExit || exitCode != 0) {
          QString error =
              QString::fromUtf8(process->readAllStandardError()).trimmed();
          if (error.isEmpty()) {
            error = process->errorString();
          }
          setPlaceholder(rootIndex, mRoot, "... error: " + error.section('\n', -1));
          emit failed(error);
          return;
        }

        QJsonParseError parseError;
        const QJsonDocument doc =
            QJsonDocument::fromJson(process->readAllStandardOutput(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
          const QString error =
              "could not parse restic snapshots output: " + parseError.errorString();
          setPlaceholder(rootIndex, mRoot, "... error: " + error);
          emit failed(error);
          return;
        }

        clearChildren(rootIndex, mRoot);

        QVector<ResticItem *> snapshots;
        for (const QJsonValue &value : doc.array()) {
          const QJsonObject entry = value.toObject();

          auto *item = new ResticItem();
          item->parent = mRoot;
          item->isSnapshot = true;
          item->isFolder = true;
          item->snapshotId = entry.value("id").toString();
          item->modified = formatTime(entry.value("time").toString());

          for (const QJsonValue &p : entry.value("paths").toArray()) {
            item->snapshotPaths << p.toString();
          }

          const QString shortId = entry.value("short_id").toString();
          const QString host = entry.value("hostname").toString();
          item->name = QString("%1  %2  %3")
                           .arg(shortId, item->modified, host)
                           .trimmed();

          snapshots.append(item);
        }

        // Newest first: restic returns oldest first.
        std::reverse(snapshots.begin(), snapshots.end());

        if (snapshots.isEmpty()) {
          setPlaceholder(rootIndex, mRoot, "... no snapshots in this repository");
          mRoot->loaded = true;
          return;
        }

        beginInsertRows(rootIndex, 0, snapshots.count() - 1);
        mRoot->children = snapshots;
        endInsertRows();

        mRoot->loaded = true;
      });
}

void ResticModel::loadSnapshotTree(const QPersistentModelIndex &parentIndex,
                                   ResticItem *snapshot) {
  snapshot->loading = true;
  setPlaceholder(parentIndex, snapshot, "... loading files");

  // restic ls has no depth limit -- it walks the whole snapshot -- so the
  // entire tree is fetched once per snapshot and cached, rather than lazily
  // per directory the way the rclone side works.
  QProcess *process = startRestic(QStringList()
                                  << "ls" << "--json" << "--long"
                                  << snapshot->snapshotId);

  QObject::connect(
      process, &QProcess::finished, this,
      [this, process, parentIndex, snapshot](int exitCode,
                                             QProcess::ExitStatus status) {
        process->deleteLater();
        snapshot->loading = false;

        if (status != QProcess::NormalExit || exitCode != 0) {
          QString error =
              QString::fromUtf8(process->readAllStandardError()).trimmed();
          if (error.isEmpty()) {
            error = process->errorString();
          }
          setPlaceholder(parentIndex, snapshot,
                         "... error: " + error.section('\n', -1));
          emit failed(error);
          return;
        }

        // "restic ls --json" is JSON Lines, not a JSON array: one snapshot
        // object followed by one object per node.
        //
        // The subtree is assembled under a detached root first, then spliced
        // in with proper insert signals. Building straight into the live
        // snapshot node would either bypass the model signals or force a full
        // model reset, which collapses every other expanded row in the view.
        auto *staging = new ResticItem();
        QHash<QString, ResticItem *> byPath;
        byPath.insert("/", staging);

        // Resolves (creating if needed) the folder holding a given path, so
        // the flat node stream can be rebuilt into a tree regardless of order.
        std::function<ResticItem *(const QString &)> folderFor =
            [&](const QString &dirPath) -> ResticItem * {
          if (dirPath.isEmpty() || dirPath == "/") {
            return staging;
          }
          auto it = byPath.find(dirPath);
          if (it != byPath.end()) {
            return it.value();
          }

          ResticItem *parent =
              folderFor(dirPath.section('/', 0, -2));
          auto *item = new ResticItem();
          item->parent = parent;
          item->isFolder = true;
          item->name = dirPath.section('/', -1);
          item->path = dirPath;
          parent->children.append(item);
          byPath.insert(dirPath, item);
          return item;
        };

        int nodeCount = 0;
        const QByteArray output = process->readAllStandardOutput();
        for (const QByteArray &line : output.split('\n')) {
          if (line.trimmed().isEmpty()) {
            continue;
          }

          const QJsonObject entry = QJsonDocument::fromJson(line).object();
          if (entry.value("struct_type").toString() != "node") {
            continue;
          }

          const QString path = entry.value("path").toString();
          const QString type = entry.value("type").toString();
          if (path.isEmpty()) {
            continue;
          }

          if (type == "dir") {
            ResticItem *item = folderFor(path);
            item->modified = formatTime(entry.value("mtime").toString());
          } else {
            ResticItem *parent = folderFor(path.section('/', 0, -2));
            auto *item = new ResticItem();
            item->parent = parent;
            item->name = entry.value("name").toString();
            item->path = path;
            item->size = quint64(entry.value("size").toVariant().toLongLong());
            item->modified = formatTime(entry.value("mtime").toString());
            parent->children.append(item);
          }
          nodeCount++;
        }

        snapshot->loaded = true;

        if (nodeCount == 0) {
          delete staging;
          setPlaceholder(parentIndex, snapshot, "... snapshot is empty");
          return;
        }

        sortTree(staging);

        // Drop the "loading" placeholder, then splice the staged subtree in.
        clearChildren(parentIndex, snapshot);

        QVector<ResticItem *> adopted = staging->children;
        staging->children.clear();
        delete staging;

        for (ResticItem *child : adopted) {
          child->parent = snapshot;
        }

        beginInsertRows(parentIndex, 0, adopted.count() - 1);
        snapshot->children = adopted;
        endInsertRows();
      });
}
