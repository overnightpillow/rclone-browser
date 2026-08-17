#pragma once

#include "parsing.h"
#include "pch.h"
#include "ui_progress_dialog.h"

class ProgressDialog : public QDialog {
  Q_OBJECT

public:
  ProgressDialog(const QString &title, const QString &operation,
                 const QString &message, QProcess *process,
                 QWidget *parent = nullptr, bool close = true,
                 bool trim = false);
  ~ProgressDialog();

  void expand();
  void allowToClose();

  // One line of the command's output. Exposed so the progress rendering can be
  // tested without spawning a process.
  void applyOutputLine(const QString &line);

signals:
  void outputAvailable(const QString &output) const;

private:
  Ui::ProgressDialog ui;

  // Whatever has arrived since the last newline.
  QByteArray mPending;
  bool mTrim = false;

  void flushPending();
  void appendOutput(const QString &text);

  // percent < 0 shows the bar as busy rather than at a position.
  void showProgress(int percent, const QString &detail);
  void applyRcloneProgress(const RcloneStats &stats);
  void applyResticProgress(const ResticProgress &progress);
};
