#pragma once

#include "job_options.h"
#include "pch.h"
#include "ui_transfer_dialog.h"

class TransferDialog : public QDialog {
  Q_OBJECT

public:
  // driveShared is the state of the originating tab's "shared with me" check
  // box. It used to be read from a global setting that any other tab could
  // have changed between opening this dialog and running the job.
  TransferDialog(bool isDownload, bool isDrop, const QString &remote,
                 const QString &path, bool isFolder,
                 QWidget *parent = nullptr,
                 JobOptions *task = nullptr, bool editMode = false,
                 bool driveShared = false);
  ~TransferDialog();

  void setSource(const QString &path);

  // Seeds the dialog for a drop of one or more items. With more than one the
  // source field becomes a read-only summary: the caller runs a transfer per
  // item rather than reading a single source back out.
  void setSources(const QStringList &paths);

  QString getMode() const;
  QString getSource() const;
  QString getDest() const;
  QStringList getOptions();

  JobOptions *getJobOptions();

private:
  Ui::TransferDialog ui;

  bool mIsDownload;
  bool mDryRun = false;
  bool mIsFolder;
  bool mIsEditMode;
  bool mDriveShared;

  JobOptions *mJobOptions;

  void putJobOptions();

  void done(int r) override;

signals:
  void tasksListChanged();
};
