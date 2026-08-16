#pragma once

#include "pch.h"
#include "restic.h"

// Add or edit a restic repository entry.
class ResticRepoDialog : public QDialog {
  Q_OBJECT

public:
  explicit ResticRepoDialog(QWidget *parent,
                            const ResticRepo &initial = ResticRepo());

  ResticRepo repo() const;

private:
  QLineEdit *mName;
  QLineEdit *mRepository;
  QLineEdit *mPasswordCommand;
};
