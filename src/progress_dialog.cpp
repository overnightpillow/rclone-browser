#include "progress_dialog.h"
#include "formatting.h"
#include "parsing.h"

namespace {

// rclone reports its percentage as "15%", or "-" before it knows the total.
// Returns -1 when there is no number to show.
int ParsePercent(const QString &text) {
  if (text.isEmpty()) {
    return -1;
  }
  QString digits = text;
  digits.remove('%');

  bool ok = false;
  const int percent = digits.trimmed().toInt(&ok);
  if (!ok) {
    return -1;
  }
  return qBound(0, percent, 100);
}

// Joins the parts of the line under the bar, skipping the ones the command
// has not reported. Written out rather than assembled with a separator so an
// absent field leaves no stray dash behind.
QString JoinDetail(const QStringList &parts) {
  QStringList present;
  for (const QString &part : parts) {
    if (!part.isEmpty() && part != "-") {
      present << part;
    }
  }
  return present.join("  -  ");
}

} // namespace

ProgressDialog::ProgressDialog(const QString &title, const QString &operation,
                               const QString &message, QProcess *process,
                               QWidget *parent, bool close, bool trim)
    : QDialog(parent), mTrim(trim) {
  ui.setupUi(this);
  resize(width(), 0);

  setWindowTitle(title);
  ui.labelOperation->setText(operation);
  ui.labelInfo->setText(message);

  ui.output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  ui.output->setVisible(false);

  // Both stay hidden until the command actually reports progress, so the
  // dialogs for instant operations -- New Folder, Rename -- look as they did.
  ui.progressBar->setVisible(false);
  ui.progressDetail->setVisible(false);

  QObject::connect(ui.buttonBox, &QDialogButtonBox::rejected, this,
                   &QDialog::reject);

  QObject::connect(ui.buttonShowOutput, &QPushButton::toggled, this,
                   [=](bool checked) {
                     ui.output->setVisible(checked);
                     ui.buttonShowOutput->setArrowType(
                         checked ? Qt::DownArrow : Qt::RightArrow);
                     if (!checked) {
                       adjustSize();
                     }
                   });

  QObject::connect(process,
                   &QProcess::finished,
                   this, [=](int code, QProcess::ExitStatus status) {
                     // Whatever the command printed without a trailing
                     // newline is still worth showing.
                     flushPending();

                     if (status == QProcess::NormalExit && code == 0) {
                       if (ui.progressBar->isVisible()) {
                         ui.progressBar->setRange(0, 100);
                         ui.progressBar->setValue(100);
                       }
                       if (close) {
                         emit accept();
                       }
                     } else {
                       ui.buttonShowOutput->setChecked(true);
                       ui.buttonBox->setEnabled(true);
                     }
                   });

  // A process that never starts never emits finished(), so without this the
  // dialog sits there with its buttons disabled and no explanation. The usual
  // cause is a helper binary that is not where the settings say it is.
  QObject::connect(process, &QProcess::errorOccurred, this,
                   [=](QProcess::ProcessError error) {
                     if (error != QProcess::FailedToStart) {
                       return;
                     }
                     ui.output->appendPlainText(
                         QString("Cannot run %1: %2")
                             .arg(process->program(), process->errorString()));
                     ui.buttonShowOutput->setChecked(true);
                     ui.buttonBox->setEnabled(true);
                   });

  // Read by line rather than in whole chunks: a progress record only means
  // anything once it is complete, and a chunk boundary lands mid-line often
  // enough to matter.
  QObject::connect(process, &QProcess::readyRead, this, [=]() {
    mPending += process->readAll();

    int newline;
    while ((newline = mPending.indexOf('\n')) >= 0) {
      const QByteArray line = mPending.left(newline);
      mPending.remove(0, newline + 1);
      applyOutputLine(QString::fromUtf8(line));
    }
  });

  process->setProcessChannelMode(QProcess::MergedChannels);
  process->start(QIODevice::ReadOnly);
}

ProgressDialog::~ProgressDialog() {}

void ProgressDialog::flushPending() {
  if (mPending.isEmpty()) {
    return;
  }
  const QByteArray line = mPending;
  mPending.clear();
  applyOutputLine(QString::fromUtf8(line));
}

// One line of the command's output, applied to the dialog.
//
// Public, and separate from the readyRead handler, so the dialog can be driven
// from a test without spawning a process.
void ProgressDialog::applyOutputLine(const QString &line) {
  ResticProgress restic;
  if (ParseResticProgress(line.toUtf8(), &restic)) {
    applyResticProgress(restic);
    // The status records are machine chatter and there is one a second; the
    // output pane would be nothing else. The summary is worth keeping, in the
    // human form built below.
    return;
  }

  const QString message = ResticMessageText(line.toUtf8());
  if (!message.isEmpty()) {
    appendOutput(message);
    return;
  }

  const RcloneStats stats = ParseRcloneStats(line);
  if (stats.kind == RcloneStats::Totals) {
    applyRcloneProgress(stats);
  }

  appendOutput(mTrim ? line.trimmed() : line);
}

void ProgressDialog::appendOutput(const QString &text) {
  ui.output->appendPlainText(text);
  emit outputAvailable(text);
}

void ProgressDialog::showProgress(int percent, const QString &detail) {
  if (!ui.progressBar->isVisible()) {
    ui.progressBar->setVisible(true);
    ui.progressDetail->setVisible(true);
    adjustSize();
  }

  if (percent < 0) {
    // Range 0-0 is Qt's busy indicator: the command is working but cannot say
    // how far along it is, which is the honest thing to show for a delete or
    // a transfer whose total is not known yet.
    ui.progressBar->setRange(0, 0);
  } else {
    ui.progressBar->setRange(0, 100);
    ui.progressBar->setValue(percent);
  }

  // The percentage goes in the text as well as the bar: the native macOS bar
  // draws no text of its own, so the bar alone gives no number to read.
  ui.progressDetail->setText(percent < 0
                                 ? detail
                                 : JoinDetail({QString("%1%").arg(percent),
                                               detail}));
}

void ProgressDialog::applyRcloneProgress(const RcloneStats &stats) {
  QString transferred = stats.size;
  if (!stats.totalSize.isEmpty()) {
    transferred += " of " + stats.totalSize;
  }

  showProgress(ParsePercent(stats.percent),
               JoinDetail({transferred, stats.bandwidth,
                           stats.eta.isEmpty() || stats.eta == "-"
                               ? QString()
                               : "ETA " + stats.eta}));
}

void ProgressDialog::applyResticProgress(const ResticProgress &progress) {
  QString transferred = FormatSize(progress.bytesDone);
  if (progress.totalBytes > 0) {
    transferred += " of " + FormatSize(progress.totalBytes);
  }

  QString files;
  if (progress.totalFiles > 0) {
    files = QString("%1 of %2 files")
                .arg(progress.filesDone)
                .arg(progress.totalFiles);
  }

  const int percent =
      progress.percent < 0 ? -1 : qBound(0, int(progress.percent + 0.5), 100);
  showProgress(percent, JoinDetail({transferred, files}));

  if (progress.kind == ResticProgress::Summary) {
    appendOutput(QString("Restored %1 in %2.")
                     .arg(files.isEmpty() ? QString("everything") : files,
                          FormatSize(progress.bytesDone)));
  }
}

void ProgressDialog::expand() { ui.buttonShowOutput->setChecked(true); }

void ProgressDialog::allowToClose() { ui.buttonBox->setEnabled(true); }
