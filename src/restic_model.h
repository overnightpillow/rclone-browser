#pragma once

#include "parsing.h"
#include "pch.h"
#include "restic.h"

// One node in the snapshot browser. The top level is snapshots; below that,
// the directory tree reconstructed from "restic ls".
struct ResticItem {
  ~ResticItem() { qDeleteAll(children); }

  int num() const {
    Q_ASSERT(parent);
    return parent->children.indexOf(const_cast<ResticItem *>(this));
  }

  ResticItem *parent = nullptr;
  QVector<ResticItem *> children;

  bool isSnapshot = false;
  bool isFolder = false;
  // Set on placeholder rows ("loading", "error") so they render without an
  // icon and are excluded from selection-driven actions.
  bool isPlaceholder = false;

  QString name;
  QString modified;
  quint64 size = 0;

  // Snapshot rows only.
  QString snapshotId;
  QStringList snapshotPaths;

  // Absolute path inside the snapshot, as restic reports it. Used for restore.
  QString path;

  bool loaded = false;
  bool loading = false;
};

// Rebuilds a directory tree from the flat, absolute-path node stream that
// "restic ls" produces, under a detached root the caller owns. Intermediate
// directories are created on demand, so the stream's order does not matter.
//
// Exposed for testing: this is the most intricate part of the restic browser
// and the part least likely to be exercised by clicking around.
ResticItem *BuildResticTree(const QVector<ResticNode> &nodes);

// Folders first, then case-insensitive by name, applied recursively.
void SortResticTree(ResticItem *item);

class ResticModel : public QAbstractItemModel {
  Q_OBJECT

public:
  ResticModel(const ResticRepo &repo, QObject *parent);
  ~ResticModel() override;

  const ResticRepo &repo() const { return mRepo; }

  // Reloads the snapshot list from scratch.
  void refresh();

  bool isSnapshot(const QModelIndex &index) const;
  bool isFolder(const QModelIndex &index) const;
  bool isPlaceholder(const QModelIndex &index) const;
  // Snapshot id owning this index, walking up from files to their snapshot.
  QString snapshotId(const QModelIndex &index) const;
  QString path(const QModelIndex &index) const;

  QModelIndex index(int row, int column,
                    const QModelIndex &parent) const override;
  QModelIndex parent(const QModelIndex &index) const override;
  bool hasChildren(const QModelIndex &parent) const override;
  int rowCount(const QModelIndex &parent) const override;
  int columnCount(const QModelIndex &parent) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;
  bool canFetchMore(const QModelIndex &parent) const override;
  void fetchMore(const QModelIndex &parent) override;

signals:
  void failed(const QString &message);

private:
  ResticRepo mRepo;
  ResticItem *mRoot;

  QIcon mSnapshotIcon;
  QIcon mFolderIcon;
  QIcon mFileIcon;

  ResticItem *get(const QModelIndex &index) const;

  void loadSnapshots();
  void loadSnapshotTree(const QPersistentModelIndex &parentIndex,
                        ResticItem *snapshot);

  void setPlaceholder(const QPersistentModelIndex &parentIndex,
                      ResticItem *parent, const QString &text);
  void clearChildren(const QPersistentModelIndex &parentIndex,
                     ResticItem *parent);

  QProcess *startRestic(const QStringList &args);
};
