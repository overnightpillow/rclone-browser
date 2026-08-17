#pragma once

#include "pch.h"
#include "ui_job_widget.h"

class JobWidget : public QWidget {
  Q_OBJECT

public:
  JobWidget(QProcess *process, const QString &info, const QStringList &args,
            const QString &source, const QString &dest,
            QWidget *parent = nullptr);
  ~JobWidget();

  void showDetails();

  // Applies one line of rclone output to the panel. Public so the panel can be
  // driven from a test without a process behind it.
  void applyOutputLine(const QString &line);

public slots:
  void cancel();

signals:
  void finished(const QString &info);
  void closed();

private:
  Ui::JobWidget ui;

  bool mRunning = true;
  bool mCancelled = false;
  QProcess *mProcess;

  QStringList mArgs;
  QHash<QString, QLabel *> mActive;
  QSet<QLabel *> mUpdated;
};
