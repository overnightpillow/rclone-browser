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
  void addMount(const QString &remote, const QString &folder);
  void addStream(const QString &remote, const QString &stream);

  // A folder in the tree may itself be a restic repository. MainWindow owns
  // the repository list and password handling, so the request is forwarded
  // rather than handled here.
  void openRestic(const QString &remote, const QString &path);
  void saveRestic(const QString &remote, const QString &path);

private:
  Ui::RemoteWidget ui;
};
