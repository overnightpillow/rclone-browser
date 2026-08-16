#include "restic_widget.h"
#include "progress_dialog.h"
#include "restic_model.h"
#include "utils.h"

ResticWidget::ResticWidget(const ResticRepo &repo, QWidget *parent)
    : QWidget(parent), mRepo(repo) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  auto *toolBar = new QToolBar(this);
  mRefresh = toolBar->addAction(
      qApp->style()->standardIcon(QStyle::SP_BrowserReload), "Refresh");
  mRestore = toolBar->addAction(
      qApp->style()->standardIcon(QStyle::SP_DialogSaveButton), "Restore...");
  layout->addWidget(toolBar);

  mTree = new QTreeView(this);
  mTree->setUniformRowHeights(true);
  mTree->setSelectionMode(QAbstractItemView::SingleSelection);
  mTree->setSelectionBehavior(QAbstractItemView::SelectRows);
  mTree->setContextMenuPolicy(Qt::ActionsContextMenu);
  mTree->addAction(mRestore);
  layout->addWidget(mTree, 1);

  mStatus = new QLabel(repo.repository, this);
  mStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(mStatus);

  mModel = new ResticModel(repo, this);
  mTree->setModel(mModel);
  mTree->setColumnWidth(0, 420);

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
