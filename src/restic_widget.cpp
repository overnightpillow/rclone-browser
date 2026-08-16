#include "restic_widget.h"
#include "progress_dialog.h"
#include "restic_model.h"
#include "utils.h"

ResticWidget::ResticWidget(const ResticRepo &repo, QWidget *parent)
    : QWidget(parent), mRepo(repo) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  auto *toolBar = new QToolBar(this);
  StyleToolBar(toolBar);

  mRefresh = toolBar->addAction(
      qApp->style()->standardIcon(QStyle::SP_BrowserReload), "Refresh");
  mRefresh->setToolTip("Reload the snapshot list from the repository");

  // SP_DialogSaveButton is a floppy disk, which reads as "save a document",
  // not "pull this out of a backup". A down arrow matches what the action
  // actually does: bring data down from the repository to local disk.
  mRestore = toolBar->addAction(
      qApp->style()->standardIcon(QStyle::SP_ArrowDown), "Restore...");
  mRestore->setToolTip("Restore the selected snapshot or file to a local "
                       "folder");

  layout->addWidget(toolBar);

  mTree = new QTreeView(this);
  mTree->setSelectionMode(QAbstractItemView::SingleSelection);
  mTree->setSelectionBehavior(QAbstractItemView::SelectRows);
  mTree->setContextMenuPolicy(Qt::ActionsContextMenu);
  mTree->addAction(mRestore);
  StyleTreeView(mTree);
  layout->addWidget(mTree, 1);

  mStatus = new QLabel(repo.repository, this);
  mStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
  // Secondary information: the repository path and any error text.
  mStatus->setContentsMargins(6, 2, 6, 2);
  QPalette statusPalette = mStatus->palette();
  statusPalette.setBrush(QPalette::WindowText,
                         palette().brush(QPalette::Disabled,
                                         QPalette::WindowText));
  mStatus->setPalette(statusPalette);
  layout->addWidget(mStatus);

  mModel = new ResticModel(repo, this);
  mTree->setModel(mModel);

  // Column 0 absorbs the spare width, Size and Modified stay at their content
  // width. A fixed width on column 0 left dead space to the right of the last
  // header section, since StyleTreeView turns off stretchLastSection.
  mTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);

  auto fitColumns = [this]() {
    mTree->resizeColumnToContents(1);
    mTree->resizeColumnToContents(2);
  };
  QObject::connect(mModel, &QAbstractItemModel::rowsInserted, this, fitColumns);
  QObject::connect(mModel, &QAbstractItemModel::modelReset, this, fitColumns);
  QObject::connect(mModel, &QAbstractItemModel::layoutChanged, this,
                   fitColumns);
  fitColumns();

  QObject::connect(mModel, &ResticModel::failed, this,
                   [=](const QString &message) {
                     mStatus->setText("Error: " + message.section('\n', -1));
                   });

  QObject::connect(mTree->selectionModel(),
                   &QItemSelectionModel::selectionChanged, this,
                   [=]() { updateActions(); });

  QObject::connect(mRefresh, &QAction::triggered, this, [=]() {
    mStatus->setText(mRepo.repository);
    mModel->refresh();
  });

  QObject::connect(mRestore, &QAction::triggered, this,
                   [=]() { restoreSelection(); });

  updateActions();
}

void ResticWidget::updateActions() {
  const auto selected = mTree->selectionModel()->selectedRows();
  const bool restorable =
      !selected.isEmpty() && !mModel->isPlaceholder(selected.front());
  mRestore->setEnabled(restorable);
}

void ResticWidget::restoreSelection() {
  const auto selected = mTree->selectionModel()->selectedRows();
  if (selected.isEmpty()) {
    return;
  }

  const QModelIndex index = selected.front();
  if (mModel->isPlaceholder(index)) {
    return;
  }

  const QString snapshot = mModel->snapshotId(index);
  if (snapshot.isEmpty()) {
    return;
  }

  const QString target = QFileDialog::getExistingDirectory(
      this, "Restore to folder",
      GetSettings()->value("Settings/lastUsedDestFolder").toString());
  if (target.isEmpty()) {
    return;
  }

  QStringList args;
  args << "restore" << snapshot << "--target" << target;

  const QString path = mModel->path(index);
  QString description;
  if (mModel->isSnapshot(index)) {
    description = QString("whole snapshot %1").arg(snapshot.left(8));
  } else {
    // restic matches --include against the snapshot's absolute paths.
    args << "--include" << path;
    description = path;
  }

  auto *process = new QProcess(this);
  ApplyResticEnvironment(process, mRepo);
  process->setProgram(GetRestic());
  process->setArguments(ResticBaseArgs(mRepo) + args);
  process->setProcessChannelMode(QProcess::MergedChannels);

  ProgressDialog progress("Restore", "Restoring...",
                          description + "\n→ " + target, process, this);
  progress.exec();

  process->deleteLater();
}
