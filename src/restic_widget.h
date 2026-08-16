#pragma once

#include "pch.h"
#include "restic.h"

class ResticModel;

// Read-only browser for one restic repository: snapshots on top, the files of
// a snapshot below, with restore-to-local for the selected entry.
class ResticWidget : public QWidget {
  Q_OBJECT

public:
  ResticWidget(const ResticRepo &repo, QWidget *parent);

private:
  ResticRepo mRepo;
  ResticModel *mModel;
  QTreeView *mTree;
  QLabel *mStatus;

  QAction *mRefresh;
  QAction *mRestore;

  void updateActions();
  void restoreSelection();
};
