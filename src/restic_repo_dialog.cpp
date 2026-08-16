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
  // Without this the fields stay at their size hint while the dialog is wide,
  // which leaves a narrow column of controls floating in the middle and
  // truncates the repository string -- the longest and most important value
  // on the form.
  form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
  form->setRowWrapPolicy(QFormLayout::DontWrapRows);
  form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  form->setHorizontalSpacing(12);
  form->setVerticalSpacing(8);

  form->addRow("Name", mName);
  form->addRow("Repository", mRepository);
  form->addRow("Password command", mPasswordCommand);

  // The repository is the value most likely to be long.
  mRepository->setMinimumWidth(320);

  auto *hint = new QLabel(
      "<small>"
      "<b>Repository</b> — anything restic accepts after <tt>-r</tt>: a local "
      "path, <tt>s3:https://…</tt>, <tt>b2:bucket:path</tt>, or "
      "<tt>rclone:remote:path</tt> to reach it through an rclone remote "
      "already configured here."
      "<br><br>"
      "<b>Password command</b> — a shell command printing the password on "
      "stdout. It is run by restic, so the password never passes through this "
      "application and is never written to its settings. Variables, pipes and "
      "<tt>&amp;&amp;</tt> all work. Leave empty to be prompted once per "
      "session (held in memory only)."
      "<br><br>"
      "From the macOS keychain, after storing it once with<br>"
      "<tt>security add-generic-password -a \"$USER\" -s NAME -w</tt>:"
      "<br><tt>security find-generic-password -a \"$USER\" -s NAME -w</tt>"
      "<br><br>"
      "From a file (<tt>chmod 600</tt> it first):"
      "<br><tt>cat ~/.config/restic/NAME.key</tt>"
      "<br><br>"
      "An environment variable set in your shell profile will <b>not</b> "
      "work: this application is launched by the system, so it does not "
      "inherit your shell's environment."
      "</small>",
      this);
  hint->setWordWrap(true);
  hint->setTextFormat(Qt::RichText);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(18, 16, 18, 14);
  layout->setSpacing(14);
  layout->addLayout(form);
  layout->addWidget(hint);
  // Keeps the buttons against the bottom edge rather than letting the help
  // text and the buttons drift apart as the dialog grows.
  layout->addStretch(1);
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

  resize(620, sizeHint().height());
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
