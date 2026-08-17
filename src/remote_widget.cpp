#include "remote_widget.h"
#include "export_dialog.h"
#include "icon_cache.h"
#include "item_model.h"
#include "list_of_job_options.h"
#include "progress_dialog.h"
#include "transfer_dialog.h"
#include "utils.h"

RemoteWidget::RemoteWidget(IconCache *iconCache, const QString &remote,
                           bool isLocal, bool isGoogle, QWidget *parent)
    : QWidget(parent) {
  ui.setupUi(this);

QString root = isLocal ? "/" : QString();

#ifndef Q_OS_WIN
  isLocal = false;
#endif

  auto settings = GetSettings();
  QString rcloneVersion = settings->value("Settings/rcloneVersion").toString();
  ui.checkBoxShared->setChecked(false);
  ui.checkBoxShared->setDisabled(!isGoogle);
  // hide checkBoxShared for non Google remotes
  if (!isGoogle) {
    ui.checkBoxShared->hide();
  }

  QStyle *style = QApplication::style();
  ui.refresh->setIcon(style->standardIcon(QStyle::SP_BrowserReload));
  ui.mkdir->setIcon(style->standardIcon(QStyle::SP_FileDialogNewFolder));
  ui.rename->setIcon(style->standardIcon(QStyle::SP_FileIcon));
  ui.move->setIcon(style->standardIcon(QStyle::SP_DirOpenIcon));
  ui.purge->setIcon(style->standardIcon(QStyle::SP_TrashIcon));
  ui.mount->setIcon(style->standardIcon(QStyle::SP_DriveNetIcon));
  ui.stream->setIcon(style->standardIcon(QStyle::SP_MediaPlay));
  ui.upload->setIcon(style->standardIcon(QStyle::SP_ArrowUp));
  ui.download->setIcon(style->standardIcon(QStyle::SP_ArrowDown));
  ui.getSize->setIcon(style->standardIcon(QStyle::SP_FileDialogInfoView));
  ui.getTree->setIcon(style->standardIcon(QStyle::SP_FileDialogListView));
  ui.export_->setIcon(style->standardIcon(QStyle::SP_FileDialogDetailedView));
  ui.link->setIcon(style->standardIcon(QStyle::SP_FileLinkIcon));

  // The root layout in the .ui file never set margins, so it inherited the
  // platform default and inset the whole tab by roughly 28px. That made the
  // tool bar read as a floating rounded bar instead of a full-width one, which
  // was the actual difference from the restic tab -- not the tool bar itself.
  if (QLayout *root = layout()) {
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
  }

  // The thirteen actions used to be thirteen QToolButtons in a fixed
  // QHBoxLayout. That row's minimum width became the widget's minimum width,
  // so opening a remote forced the main window wider than the user had sized
  // it. A QToolBar collapses whatever does not fit into an overflow menu, so
  // the tab imposes no width of its own -- and it matches the restic tab.
  auto *toolBar = new QToolBar(this);
  StyleToolBar(toolBar);

  // Every action is also in the tree's right-click menu, so the tool bar does
  // not have to carry all thirteen. It keeps the ones used while browsing;
  // Stream, Public Link, Size, Tree and Export stay context-menu only.
  toolBar->addAction(ui.refresh);
  toolBar->addSeparator();
  toolBar->addAction(ui.upload);
  toolBar->addAction(ui.download);
  toolBar->addSeparator();
  toolBar->addAction(ui.mkdir);
  toolBar->addAction(ui.rename);
  toolBar->addAction(ui.move);
  toolBar->addAction(ui.purge);
  toolBar->addSeparator();
  toolBar->addAction(ui.mount);

  // "Shared with me" is a child of the old button row, so it has to be
  // reparented onto the tool bar before that row goes away. addWidget()
  // reparents it. Deleting the row with the check box still inside it left
  // every later ui.checkBoxShared->checkState() reading freed memory, which
  // crashed on the first selection in the tree.
  QAction *sharedSeparator = toolBar->addSeparator();
  QAction *sharedAction = toolBar->addWidget(ui.checkBoxShared);
  // Only Google Drive has "shared with me". Hiding the wrapping action rather
  // than the check box keeps the tool bar from reserving an empty slot.
  sharedSeparator->setVisible(isGoogle);
  sharedAction->setVisible(isGoogle);

  // Swap the tool bar in where the button row sat. The row is only hidden,
  // never deleted: replaceWidget() already takes it out of the layout, so it
  // no longer affects sizing, and destroying a .ui-owned widget risks exactly
  // the dangling-child problem described above.
  if (auto *buttonsLayout =
          qobject_cast<QBoxLayout *>(ui.buttons->parentWidget()->layout())) {
    buttonsLayout->replaceWidget(ui.buttons, toolBar);
    ui.buttons->hide();

    // Both browser tabs are now the same shape: tool bar on top, tree filling
    // the middle, one line of de-emphasised context at the bottom. The current
    // path used to sit directly under the tool bar as a boxed, read-only line
    // edit, which read as an input field it is not.
    buttonsLayout->removeWidget(ui.path);
    buttonsLayout->addWidget(ui.path);
  }

  ui.path->setFrame(false);
  ui.path->setContentsMargins(6, 2, 6, 2);
  ui.path->setText(remote + ":");
  {
    QPalette pathPalette = ui.path->palette();
    const QBrush subdued =
        palette().brush(QPalette::Disabled, QPalette::WindowText);
    pathPalette.setBrush(QPalette::Text, subdued);
    // The line edit paints its own background; match the surrounding tab.
    pathPalette.setBrush(QPalette::Base, palette().brush(QPalette::Window));
    ui.path->setPalette(pathPalette);
  }

  ui.tree->sortByColumn(0, Qt::AscendingOrder);
  StyleTreeView(ui.tree);

  ItemModel *model = new ItemModel(iconCache, remote, this);
  ui.tree->setModel(model);
  QTimer::singleShot(0, ui.tree, SLOT(setFocus()));

  QObject::connect(model, &QAbstractItemModel::layoutChanged, this, [=]() {
    ui.tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui.tree->resizeColumnToContents(1);
    ui.tree->resizeColumnToContents(2);
  });

  QObject::connect(
      ui.tree->selectionModel(), &QItemSelectionModel::selectionChanged, this,
      [=](const QItemSelection &selection) {
        for (auto child : findChildren<QAction *>()) {
          child->setDisabled(selection.isEmpty());
        }

        if (selection.isEmpty()) {
          ui.path->clear();
          return;
        }

        QModelIndex index = selection.indexes().front();

        bool topLevel = model->isTopLevel(index);
        bool isFolder = model->isFolder(index);

        QDir path;
        if (model->isLoading(index)) {
          ui.refresh->setDisabled(true);
          ui.move->setDisabled(true);
          ui.rename->setDisabled(true);
          ui.purge->setDisabled(true);
          ui.mount->setDisabled(true);
          ui.stream->setDisabled(true);
          ui.upload->setDisabled(true);
          ui.download->setDisabled(true);
          ui.checkBoxShared->setDisabled(true);
          path = model->path(model->parent(index));
        } else {
          ui.refresh->setDisabled(false);
          bool driveShared = ui.checkBoxShared->checkState();
          ui.mkdir->setDisabled(driveShared);
          ui.rename->setDisabled(topLevel || driveShared);
          ui.move->setDisabled(topLevel || driveShared);
          ui.purge->setDisabled(topLevel || driveShared);
          ui.upload->setDisabled(driveShared);

#if defined(Q_OS_WIN32)
          // check if required version
          unsigned int result =
              compareVersion(rcloneVersion.toStdString(), "1.50");
          if (result == 2) {
            ui.mount->setDisabled(true);
          } else {
            ui.mount->setDisabled(!isFolder);
          };
#else
// mount is not supported by rclone on these systems
#if defined(Q_OS_OPENBSD) || defined(Q_OS_NETBSD)
          ui.mount->setDisabled(true);
#else
          ui.mount->setDisabled(!isFolder);
#endif
#endif

          ui.stream->setDisabled(isFolder);
          ui.checkBoxShared->setDisabled(!isGoogle);
          path = model->path(index);
        }

        ui.getSize->setDisabled(!isFolder);
        ui.getTree->setDisabled(!isFolder);
        ui.export_->setDisabled(!isFolder);
        const QString shown =
            isLocal ? QDir::toNativeSeparators(path.path()) : path.path();
        // With nothing selected the path is empty, which collapsed the footer
        // to a blank line and left this tab looking structurally different
        // from the restic one. Fall back to naming the remote.
        ui.path->setText(shown.isEmpty() ? remote + ":" : shown);
      });

  QObject::connect(ui.refresh, &QAction::triggered, this, [=]() {

    QModelIndex index = ui.tree->selectionModel()->selectedRows().front();
    model->refresh(index);
  });

  QObject::connect(ui.mkdir, &QAction::triggered, this, [=]() {

    QModelIndex index = ui.tree->selectionModel()->selectedRows().front();

    if (!model->isFolder(index)) {
      index = index.parent();
    }
    QDir path = model->path(index);
    QString pathMsg =
        isLocal ? QDir::toNativeSeparators(path.path()) : path.path();
    QString name = QInputDialog::getText(
        this, "New Folder", QString("Create folder in %1").arg(pathMsg));
    if (!name.isEmpty()) {
      QString folder = path.filePath(name);
      QString folderMsg = isLocal ? QDir::toNativeSeparators(folder) : folder;

      QProcess process;
      UseRclonePassword(&process);
      process.setProgram(GetRclone());
      process.setArguments(QStringList() << "mkdir" << GetRcloneConf()
                                         << driveSharedArgs()
                                         << GetDefaultRcloneOptionsList()
                                         << remote + ":" + folder);
      process.setProcessChannelMode(QProcess::MergedChannels);

      ProgressDialog progress("New Folder", "Creating...", folderMsg, &process,
                              this);
      if (progress.exec() == QDialog::Accepted) {
        model->refresh(index);
      }
    }
  });

  QObject::connect(ui.rename, &QAction::triggered, this, [=]() {

    QModelIndex index = ui.tree->selectionModel()->selectedRows().front();

    QString path = model->path(index).path();
    QString pathMsg = isLocal ? QDir::toNativeSeparators(path) : path;

    QString name = model->data(index, Qt::DisplayRole).toString();
    name = QInputDialog::getText(this, "Rename",
                                 QString("New name for %1").arg(pathMsg),
                                 QLineEdit::Normal, name);
    if (!name.isEmpty()) {
      QProcess process;
      UseRclonePassword(&process);
      process.setProgram(GetRclone());
      process.setArguments(
          QStringList() << "moveto" << GetRcloneConf() << driveSharedArgs()
                        << GetDefaultRcloneOptionsList() << remote + ":" + path
                        << remote + ":" +
                               model->path(index.parent()).filePath(name));
      process.setProcessChannelMode(QProcess::MergedChannels);

      ProgressDialog progress("Rename", "Renaming...", pathMsg, &process, this);
      if (progress.exec() == QDialog::Accepted) {
        model->rename(index, name);
      }
    }
  });

  QObject::connect(ui.move, &QAction::triggered, this, [=]() {

    QModelIndex index = ui.tree->selectionModel()->selectedRows().front();

    QString path = model->path(index).path();
    QString pathMsg = isLocal ? QDir::toNativeSeparators(path) : path;

    // The prompt asks for the full destination path, and is seeded with the
    // current one, because that is what the command below actually takes.
    QString name = QInputDialog::getText(
        this, "Move", QString("New full path for %1").arg(pathMsg),
        QLineEdit::Normal, path);
    if (!name.isEmpty() && name != path) {
      QProcess process;
      UseRclonePassword(&process);
      process.setProgram(GetRclone());
      // moveto, not move: "rclone move src dst" moves the *contents* of src
      // into dst, so moving a folder scattered its files into the destination
      // and left the folder itself behind, empty. moveto moves the thing
      // named to the name given, for a file or a folder alike.
      process.setArguments(
          QStringList() << "moveto" << GetRcloneConf() << driveSharedArgs()
                        << GetDefaultRcloneOptionsList() << remote + ":" + path
                        << remote + ":" + name);
      process.setProcessChannelMode(QProcess::MergedChannels);

      ProgressDialog progress("Move", "Moving...", pathMsg, &process, this);
      if (progress.exec() == QDialog::Accepted) {
        // The parent, not the item: what was moved is no longer there, and
        // refreshing it listed a path that no longer exists.
        model->refresh(index.parent());
      }
    }
  });

  QObject::connect(ui.purge, &QAction::triggered, this, [=]() {

    QModelIndex index = ui.tree->selectionModel()->selectedRows().front();

    QString path = model->path(index).path();
    QString pathMsg = isLocal ? QDir::toNativeSeparators(path) : path;

    int button = QMessageBox::question(
        this, "Delete",
        QString("Are you sure you want to delete %1 ?").arg(pathMsg),
        QMessageBox::Yes | QMessageBox::No);
    if (button == QMessageBox::Yes) {
      QProcess process;
      UseRclonePassword(&process);
      process.setProgram(GetRclone());
      process.setArguments(QStringList()
                           << (model->isFolder(index) ? "purge" : "delete")
                           << GetRcloneConf() << driveSharedArgs()
                           << GetDefaultRcloneOptionsList()
                           << remote + ":" + path);
      process.setProcessChannelMode(QProcess::MergedChannels);

      ProgressDialog progress("Delete", "Deleting...", pathMsg, &process, this);
      if (progress.exec() == QDialog::Accepted) {
        QModelIndex parent = index.parent();
        QModelIndex next = parent.model()->index(index.row() + 1, 0);
        ui.tree->selectionModel()->select(next.isValid() ? next : parent,
                                          QItemSelectionModel::SelectCurrent);
        model->removeRow(index.row(), parent);
      }
    }
  });

  QObject::connect(ui.mount, &QAction::triggered, this, [=]() {

    QModelIndex index = ui.tree->selectionModel()->selectedRows().front();

    QString path = model->path(index).path();
    QString pathMsg = isLocal ? QDir::toNativeSeparators(path) : path;

#if defined(Q_OS_WIN32)
    QString folder =
        QInputDialog::getText(this, "Mount",
                              QString("(Make sure you have WinFsp-FUSE "
                                      "installed)\n\nDrive to mount %1 to")
                                  .arg(remote),
                              QLineEdit::Normal, "Z:");
#else
        QString folder = QFileDialog::getExistingDirectory(this, QString("Mount %1").arg(remote));
#endif

    if (!folder.isEmpty()) {
      emit addMount(remote + ":" + path, folder,
                    ui.checkBoxShared->isChecked());
    }
  });

  QObject::connect(ui.stream, &QAction::triggered, this, [=]() {
    auto settings = GetSettings();

    QModelIndex index = ui.tree->selectionModel()->selectedRows().front();
    QString path = model->path(index).path();

    bool streamConfirmed =
        settings->value("Settings/streamConfirmed", false).toBool();
    QString stream = settings->value("Settings/stream", "mpv -").toString();
    if (!streamConfirmed) {
      QString result = QInputDialog::getText(
          this, "Stream",
          "Enter stream command (file will be passed in STDIN):",
          QLineEdit::Normal, stream);
      if (result.isEmpty()) {
        return;
      }

      stream = result;

      settings->setValue("Settings/stream", stream);
      settings->setValue("Settings/streamConfirmed", true);
    }

    emit addStream(remote + ":" + path, stream);
  });

  QObject::connect(ui.checkBoxShared, &QCheckBox::toggled, ui.shared,
                   &QAction::toggled);

  QObject::connect(ui.shared, &QAction::toggled, this, [=](const bool checked) {
    // The model does its own listing, so it needs the flag directly. It used
    // to read a global setting that every open tab wrote to, so turning
    // "shared with me" on in one Drive tab silently changed what the others
    // listed -- and which of them won depended on which had last been clicked.
    model->setDriveSharedWithMe(checked);
    ui.checkBoxShared->setChecked(checked);

    QModelIndex index = ui.tree->selectionModel()->selectedRows().front();
    QModelIndex top = index;
    while (!model->isTopLevel(top)) {
      top = top.parent();
    }
    ui.tree->selectionModel()->clear();
    ui.tree->selectionModel()->select(top, QItemSelectionModel::Select |
                                               QItemSelectionModel::Rows);
    model->refresh(top);
  });

  QObject::connect(ui.link, &QAction::triggered, this, [=]() {

    QModelIndex index = ui.tree->selectionModel()->selectedRows().front();

    QString path = model->path(index).path();
    QString pathMsg = isLocal ? QDir::toNativeSeparators(path) : path;

    QProcess process;
    UseRclonePassword(&process);
    process.setProgram(GetRclone());
    process.setArguments(
        QStringList() << "link" << GetRcloneConf() << driveSharedArgs()
                      << GetDefaultRcloneOptionsList() << remote + ":" + path);
    process.setProcessChannelMode(QProcess::MergedChannels);
    ProgressDialog progress("Fetch Public Link", "Fetching link for...",
                            pathMsg, &process, this, false, true);
    progress.expand();
    progress.allowToClose();
    progress.exec();
  });

  QObject::connect(ui.upload, &QAction::triggered, this, [=]() {

    QModelIndex index = ui.tree->selectionModel()->selectedRows().front();

    if (!model->isFolder(index)) {
      index = index.parent();
    }
    QDir path = model->path(index);

    TransferDialog t(false, false, remote, path, true, this, nullptr,
                     false, ui.checkBoxShared->isChecked());
    if (t.exec() == QDialog::Accepted) {
      QString src = t.getSource();
      QString dst = t.getDest();

      QStringList args = t.getOptions();
      emit addTransfer(QString("%1 from %2").arg(t.getMode()).arg(src), src,
                       dst, args);
    }
  });

  QObject::connect(ui.download, &QAction::triggered, this, [=]() {

    QModelIndex index = ui.tree->selectionModel()->selectedRows().front();
    QDir path = model->path(index);

    TransferDialog t(true, false, remote, path, model->isFolder(index), this,
                     nullptr, false, ui.checkBoxShared->isChecked());
    if (t.exec() == QDialog::Accepted) {
      QString src = t.getSource();
      QString dst = t.getDest();

      QStringList args = t.getOptions();
      emit addTransfer(QString("%1 %2").arg(t.getMode()).arg(src), src, dst,
                       args);
    }
  });

  QObject::connect(ui.getTree, &QAction::triggered, this, [=]() {
    QModelIndex index = ui.tree->selectionModel()->selectedRows().front();

    QString path = model->path(index).path();
    QString pathMsg = isLocal ? QDir::toNativeSeparators(path) : path;

    QProcess process;
    UseRclonePassword(&process);
    process.setProgram(GetRclone());
    process.setArguments(
        QStringList() << "tree"
                      << "-d" << GetRcloneConf() << driveSharedArgs()
                      << GetDefaultRcloneOptionsList() << remote + ":" + path);
    process.setProcessChannelMode(QProcess::MergedChannels);
    ProgressDialog progress("Show directories tree", "Processing...", pathMsg,
                            &process, this, false);
    progress.expand();
    progress.allowToClose();
    progress.resize(1000, 600);
    progress.exec();
  });

  QObject::connect(ui.getSize, &QAction::triggered, this, [=]() {
    QModelIndex index = ui.tree->selectionModel()->selectedRows().front();

    QString path = model->path(index).path();
    QString pathMsg = isLocal ? QDir::toNativeSeparators(path) : path;

    QProcess process;
    UseRclonePassword(&process);
    process.setProgram(GetRclone());
    process.setArguments(
        QStringList() << "size" << GetRcloneConf() << driveSharedArgs()
                      << GetDefaultRcloneOptionsList() << remote + ":" + path);
    process.setProcessChannelMode(QProcess::MergedChannels);
    ProgressDialog progress("Get Size", "Calculating...", pathMsg, &process,
                            this, false);
    progress.expand();
    progress.allowToClose();
    progress.exec();
  });

  QObject::connect(ui.export_, &QAction::triggered, this, [=]() {

    QModelIndex index = ui.tree->selectionModel()->selectedRows().front();
    QDir path = model->path(index);
    ExportDialog e(remote, path, this);
    if (e.exec() == QDialog::Accepted) {
      QString dst = e.getDestination();
      bool txt = e.onlyFilenames();

      QFile *file = new QFile(dst);
      if (!file->open(QFile::WriteOnly)) {
        QMessageBox::warning(
            this, "Error",
            QString("Cannot open file '%1' for writing!").arg(dst));
        delete file;
        return;
      }

      QRegularExpression re(QRegularExpression::anchoredPattern(
          R"((\d+) (\d\d\d\d-\d\d-\d\d \d\d:\d\d:\d\d)\.\d+ (.+))"));

      QProcess process;
      UseRclonePassword(&process);
      process.setProgram(GetRclone());
      process.setArguments(QStringList()
                           << GetRcloneConf() << driveSharedArgs()
                           << GetDefaultRcloneOptionsList() << e.getOptions());
      process.setProcessChannelMode(QProcess::MergedChannels);

      ProgressDialog progress("Export", "Exporting...", dst, &process, this);
      file->setParent(&progress);

      QObject::connect(&progress, &ProgressDialog::outputAvailable, this,
                       [=](const QString &output) {
                         QTextStream out(file);
                         out.setEncoding(QStringConverter::Utf8);

                         for (const auto &line : output.split('\n')) {
                           auto match = re.match(line.trimmed());
                           if (match.hasMatch()) {
                             if (txt) {
                               out << match.captured(3) << '\n';
                             } else {
                               QString name = match.captured(3);
                               if (name.contains(' ') || name.contains(',') ||
                                   name.contains('"')) {
                                 name = '"' + name.replace("\"", "\"\"") + '"';
                               }
                               out << name << ',' << '"' << match.captured(2)
                                   << '"' << ','
                                   << match.captured(1).toULongLong() << '\n';
                             }
                           }
                         }
                       });

      progress.exec();
    }
  });

  QObject::connect(
      model, &ItemModel::drop, this,
      [=](const QList<QDir> &paths, const QModelIndex &parent) {
        if (paths.isEmpty()) {
          return;
        }

        this->activateWindow();
        const QDir destPath = model->path(parent);

        // A folder is uploaded into a folder of its own name, so that dropping
        // "photos" produces "photos" at the destination rather than emptying
        // its contents into the target. A file goes straight in.
        auto destinationFor = [](const QDir &base, const QDir &source) {
          return QFileInfo(source.path()).isDir()
                     ? base.filePath(source.dirName())
                     : base.path();
        };

        // One dialog for the whole drop. With several items the destination
        // shown is the folder they land in; with one it is the exact
        // destination, as it has always been.
        const bool single = paths.size() == 1;
        const QString dialogDest =
            single ? destinationFor(destPath, paths.first()) : destPath.path();

        TransferDialog t(false, true, remote, dialogDest, true, this, nullptr,
                         false, ui.checkBoxShared->isChecked());

        QStringList sources;
        sources.reserve(paths.size());
        for (const QDir &path : paths) {
          sources.append(path.path());
        }
        t.setSources(sources);

        if (t.exec() != QDialog::Accepted) {
          return;
        }

        // The options are read once, from the one dialog, and then re-pointed
        // at each dropped item in turn: one job per item, all configured the
        // same way.
        JobOptions options = *t.getJobOptions();
        const QString chosenDest = t.getDest();

        for (const QDir &path : paths) {
          options.source = single ? t.getSource() : path.path();
          options.dest =
              single ? chosenDest
                     : destinationFor(QDir(chosenDest), path);

          emit addTransfer(
              QString("%1 from %2").arg(t.getMode()).arg(options.source),
              options.source, options.dest, options.getOptions());
        }
      });

  QObject::connect(
      ui.tree, &QWidget::customContextMenuRequested, this,
      [=](const QPoint &pos) {

        QMenu menu;
        menu.addAction(ui.refresh);
        menu.addAction(ui.getSize);
        menu.addAction(ui.getTree);
        menu.addAction(ui.export_);
        menu.addSeparator();
        menu.addAction(ui.mkdir);
        menu.addAction(ui.rename);
        menu.addAction(ui.move);
        menu.addAction(ui.purge);
        menu.addSeparator();
        menu.addAction(ui.mount);
        menu.addAction(ui.stream);
        menu.addAction(ui.upload);
        menu.addAction(ui.download);
        menu.addAction(ui.link);

        // Only a folder can be a restic repository, and only on a real remote
        // -- a local path is reachable by restic directly without rclone.
        QAction *openRestic = nullptr;
        QAction *saveRestic = nullptr;
        const auto selected = ui.tree->selectionModel()->selectedRows();
        if (!isLocal && !selected.isEmpty() &&
            model->isFolder(selected.front())) {
          menu.addSeparator();
          openRestic = menu.addAction("Open as Restic Repository");
          saveRestic = menu.addAction("Save as Restic Repository...");
        }

        QAction *chosen = menu.exec(ui.tree->viewport()->mapToGlobal(pos));

        if (chosen != nullptr && chosen == openRestic) {
          emit this->openRestic(remote,
                                model->path(selected.front()).path());
        } else if (chosen != nullptr && chosen == saveRestic) {
          emit this->saveRestic(remote,
                                model->path(selected.front()).path());
        }
      });

  if (isLocal) {
    QHash<QString, QPersistentModelIndex> drives;

    // QDir::drives is fast
    for (const auto &drive : QDir::drives()) {
      QString path = drive.path();
      QModelIndex index = model->addRoot(QDir::toNativeSeparators(path), path);
      drives.insert(path, index);
    }

#if (QT_VERSION >= QT_VERSION_CHECK(5, 4, 0)) && !(defined Q_OS_WIN)
    QThread *thread = new QThread(this);
    thread->start();

    QObject *worker = new QObject();
    worker->moveToThread(thread);

    QTimer::singleShot(0, worker, [=]() {
      QStorageInfo info;
      info.refresh();

      // QStorageInfo::mountedVolumes is slow :(
      for (const auto &volume : info.mountedVolumes()) {
        QString name = volume.name();
        if (!name.isEmpty()) {
          QString path = volume.rootPath();
          QString item =
              QString("%1 (%2)").arg(QDir::toNativeSeparators(path)).arg(name);
          QTimer::singleShot(0, this,
                             [=]() { model->rename(drives[path], item); });
        }
      }

      thread->quit();
      thread->deleteLater();
      worker->deleteLater();
    });
#endif

    ui.tree->selectionModel()->selectionChanged(QItemSelection(),
                                                QItemSelection());
  } else {
    QModelIndex index = model->addRoot("/", root);
    ui.tree->selectionModel()->select(
        index, QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
    ui.tree->expand(index);
  }

  QShortcut *close = new QShortcut(QKeySequence::Close, this);
  QObject::connect(close, &QShortcut::activated, this, [=]() {
    auto tabs = qobject_cast<QTabWidget *>(parent);
    tabs->removeTab(tabs->indexOf(this));
  });
}

RemoteWidget::~RemoteWidget() {}

QStringList RemoteWidget::driveSharedArgs() const {
  return ui.checkBoxShared->isChecked() ? QStringList{"--drive-shared-with-me"}
                                        : QStringList();
}
