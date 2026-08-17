#include "restic_model.h"
#include "formatting.h"
#include "theme.h"
#include "utils.h"

// Folders first, then case-insensitive by name -- restic emits nodes in tree
// order, which interleaves them.
void SortResticTree(ResticItem *item) {
  std::sort(item->children.begin(), item->children.end(),
            [](const ResticItem *a, const ResticItem *b) {
              if (a->isFolder != b->isFolder) {
                return a->isFolder;
              }
              return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
            });

  for (ResticItem *child : item->children) {
    SortResticTree(child);
  }
}

ResticItem *BuildResticTree(const QVector<ResticNode> &nodes) {
  auto *root = new ResticItem();

  QHash<QString, ResticItem *> byPath;
  byPath.insert("/", root);

  // Resolves, creating if needed, the folder holding a path. Recursive so a
  // node can arrive before the directory that contains it.
  std::function<ResticItem *(const QString &)> folderFor =
      [&](const QString &dirPath) -> ResticItem * {
    if (dirPath.isEmpty() || dirPath == "/") {
      return root;
    }
    auto it = byPath.find(dirPath);
    if (it != byPath.end()) {
      return it.value();
    }

    ResticItem *parent = folderFor(dirPath.section('/', 0, -2));
    auto *item = new ResticItem();
    item->parent = parent;
    item->isFolder = true;
    item->name = dirPath.section('/', -1);
    item->path = dirPath;
    parent->children.append(item);
    byPath.insert(dirPath, item);
    return item;
  };

  for (const ResticNode &node : nodes) {
    if (node.isDir) {
      folderFor(node.path)->modified = node.modified;
    } else {
      ResticItem *parent = folderFor(node.path.section('/', 0, -2));
      auto *item = new ResticItem();
      item->parent = parent;
      item->name = node.name;
      item->path = node.path;
      item->size = node.size;
      item->modified = node.modified;
      parent->children.append(item);
    }
  }

  SortResticTree(root);
  return root;
}

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

  // Matches the rclone tree: secondary columns muted and slightly smaller.
  if (index.column() > 0 && !item->isPlaceholder) {
    if (role == Qt::ForegroundRole) {
      return QVariant::fromValue(SecondaryTextColor(qApp->palette()));
    }
    if (role == Qt::FontRole) {
      return QVariant::fromValue(SecondaryFont(qApp->font()));
    }
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
      return FormatSize(item->size);
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

void ResticModel::runRestic(const QStringList &args,
                            std::function<void(bool, const QString &,
                                               const QByteArray &)>
                                done) {
  auto *process = new QProcess(this);
  ApplyResticEnvironment(process, mRepo);

  const QString program = GetRestic();

  // finished() and errorOccurred() can both fire for one process -- a crash
  // reports an error and then finishes -- and FailedToStart fires alone.
  auto called = std::make_shared<bool>(false);
  auto report = [this, process, called, done](bool ok, const QString &error,
                                              const QByteArray &output) {
    if (*called) {
      return;
    }
    *called = true;
    process->deleteLater();

    if (!ok) {
      emit failed(error);
    }
    done(ok, error, output);
  };

  QObject::connect(process, &QProcess::errorOccurred, this,
                   [program, report](QProcess::ProcessError e) {
                     if (e != QProcess::FailedToStart) {
                       // Anything else still reaches finished(), which has the
                       // exit code and stderr to report.
                       return;
                     }
                     report(false, ResticNotFoundMessage(program), QByteArray());
                   });

  QObject::connect(process, &QProcess::finished, this,
                   [process, report](int exitCode,
                                     QProcess::ExitStatus status) {
                     if (status == QProcess::NormalExit && exitCode == 0) {
                       report(true, QString(),
                              process->readAllStandardOutput());
                       return;
                     }

                     QString error =
                         QString::fromUtf8(process->readAllStandardError())
                             .trimmed();
                     if (error.isEmpty()) {
                       error = process->errorString();
                     }
                     report(false, error, QByteArray());
                   });

  process->start(program, ResticBaseArgs(mRepo) + args, QIODevice::ReadOnly);
}

void ResticModel::refresh() {
  clearChildren(QPersistentModelIndex(), mRoot);
  mRoot->loaded = false;
  loadSnapshots();
}

void ResticModel::loadSnapshots() {
  const QPersistentModelIndex rootIndex;
  setPlaceholder(rootIndex, mRoot, "... loading snapshots");

  runRestic(
      QStringList() << "snapshots" << "--json",
      [this, rootIndex](bool ok, const QString &error,
                        const QByteArray &output) {
        if (!ok) {
          setPlaceholder(rootIndex, mRoot,
                         "... error: " + error.section('\n', -1));
          return;
        }

        QVector<ResticSnapshot> parsed;
        QString parseErrorText;
        if (!ParseResticSnapshots(output, &parsed, &parseErrorText)) {
          setPlaceholder(rootIndex, mRoot, "... error: " + parseErrorText);
          emit failed(parseErrorText);
          return;
        }

        clearChildren(rootIndex, mRoot);

        QVector<ResticItem *> snapshots;
        snapshots.reserve(parsed.size());
        for (const ResticSnapshot &entry : parsed) {
          auto *item = new ResticItem();
          item->parent = mRoot;
          item->isSnapshot = true;
          item->isFolder = true;
          item->snapshotId = entry.id;
          item->modified = entry.time;
          item->snapshotPaths = entry.paths;
          item->name = QString("%1  %2  %3")
                           .arg(entry.shortId, entry.time, entry.hostname)
                           .trimmed();

          snapshots.append(item);
        }

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
  runRestic(
      QStringList() << "ls" << "--json" << "--long" << snapshot->snapshotId,
      [this, parentIndex, snapshot](bool ok, const QString &error,
                                    const QByteArray &output) {
        snapshot->loading = false;

        if (!ok) {
          setPlaceholder(parentIndex, snapshot,
                         "... error: " + error.section('\n', -1));
          return;
        }

        // "restic ls --json" is JSON Lines, not a JSON array: one snapshot
        // object followed by one object per node. Parsing and tree assembly
        // both live outside this lambda so they can be tested directly.
        //
        // The subtree is assembled under a detached root, then spliced in with
        // proper insert signals. Building straight into the live snapshot node
        // would either bypass the model signals or force a full model reset,
        // which collapses every other expanded row in the view.
        QVector<ResticNode> nodes;
        ParseResticNodes(output, &nodes, nullptr);

        snapshot->loaded = true;

        if (nodes.isEmpty()) {
          setPlaceholder(parentIndex, snapshot, "... snapshot is empty");
          return;
        }

        ResticItem *staging = BuildResticTree(nodes);

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
