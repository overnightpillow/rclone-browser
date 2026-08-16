#include "restic_repo_dialog.h"

ResticRepoDialog::ResticRepoDialog(QWidget *parent, const ResticRepo &initial)
    : QDialog(parent) {
  setWindowTitle(initial.isValid() ? "Edit restic repository"
                                   : "Add restic repository");

  mName = new QLineEdit(initial.name, this);
  mName->setPlaceholderText("Display name (optional)");

  mRepository = new QLineEdit(initial.repository, this);
  mRepository->setPlaceholderText("rclone:remote:path");

  mPasswordCommand = new QLineEdit(initial.passwordCommand, this);
  mPasswordCommand->setPlaceholderText(
      "Optional; leave empty to be prompted for the password");

  auto *form = new QFormLayout();
  form->addRow("Name", mName);
  form->addRow("Repository", mRepository);
  form->addRow("Password command", mPasswordCommand);

  auto *hint = new QLabel(
      "<small>Anything restic accepts after <tt>-r</tt>: a local path, "
      "<tt>s3:https://…</tt>, <tt>b2:bucket:path</tt>, or "
      "<tt>rclone:remote:path</tt> to reach the repository through an rclone "
      "remote already configured here.<br><br>"
      "A password command is run by restic and its output used as the "
      "repository password, so the password itself is never stored by this "
      "application. Without one you are prompted once per session.</small>",
      this);
  hint->setWordWrap(true);
  hint->setTextFormat(Qt::RichText);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

  auto *layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(hint);
  layout->addWidget(buttons);

  QObject::connect(buttons, &QDialogButtonBox::accepted, this,
                   &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, this,
                   &QDialog::reject);

  auto *ok = buttons->button(QDialogButtonBox::Ok);
  auto updateOk = [=]() {
    ok->setEnabled(!mRepository->text().trimmed().isEmpty());
  };
  QObject::connect(mRepository, &QLineEdit::textChanged, this, updateOk);
  updateOk();

  resize(560, sizeHint().height());
}

ResticRepo ResticRepoDialog::repo() const {
  ResticRepo repo;
  repo.repository = mRepository->text().trimmed();
  repo.name = mName->text().trimmed();
  if (repo.name.isEmpty()) {
    repo.name = repo.repository;
  }
  repo.passwordCommand = mPasswordCommand->text().trimmed();
  return repo;
}
