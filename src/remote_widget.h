#pragma once

#include "pch.h"
#include "ui_remote_widget.h"

class IconCache;

class RemoteWidget : public QWidget {
  Q_OBJECT

public:
  RemoteWidget(IconCache *icons, const QString &remote, bool isLocal,
               bool isGoogle, QWidget *parent = nullptr);
  ~RemoteWidget();

signals:
  void addTransfer(const QString &message, const QString &source,
                   const QString &remote, const QStringList &args);
  // driveShared travels with the request rather than through a settings key:
  // the mount has to use the same view of the remote as the tab it was
  // started from, whatever the other tabs are showing.
  void addMount(const QString &remote, const QString &folder, bool driveShared);
  void addStream(const QString &remote, const QString &stream);

  // A folder in the tree may itself be a restic repository. MainWindow owns
  // the repository list and password handling, so the request is forwarded
  // rather than handled here.
  void openRestic(const QString &remote, const QString &path);
  void saveRestic(const QString &remote, const QString &path);

private:
  Ui::RemoteWidget ui;

  // "--drive-shared-with-me" when this tab's check box is ticked, empty
  // otherwise. Every rclone command this widget runs takes it from here.
  QStringList driveSharedArgs() const;
};
