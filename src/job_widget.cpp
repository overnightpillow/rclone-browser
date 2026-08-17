#include "job_widget.h"
#include "parsing.h"
#include "utils.h"

namespace {
// A file name long enough to push the progress bar off the panel is elided in
// the middle, where a path is least informative.
QString elideName(const QString &name) {
  if (name.length() <= 47) {
    return name;
  }
  return name.left(25) + "..." + name.right(19);
}
} // namespace

JobWidget::JobWidget(QProcess *process, const QString &info,
                     const QStringList &args, const QString &source,
                     const QString &dest, QWidget *parent)
    : QWidget(parent), mProcess(process) {
  ui.setupUi(this);

  mArgs.append(QDir::toNativeSeparators(GetRclone()));
  mArgs.append(GetRcloneConf());
  mArgs.append(args);

  ui.source->setText(source);
  ui.dest->setText(dest);
  ui.info->setText(info);

  ui.details->setVisible(false);

  ui.output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  ui.output->setVisible(false);
  // Bounded, but by dropping the oldest line rather than by emptying the whole
  // log every ten thousand lines -- which is what it did before, so a long
  // job's output vanished exactly when someone went looking for it.
  ui.output->setMaximumBlockCount(10000);

  QObject::connect(
      ui.showDetails, &QToolButton::toggled, this, [=](bool checked) {
        ui.details->setVisible(checked);
        ui.showDetails->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
      });

  QObject::connect(
      ui.showOutput, &QToolButton::toggled, this, [=](bool checked) {
        ui.output->setVisible(checked);
        ui.showOutput->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
      });

  ui.cancel->setIcon(
      QApplication::style()->standardIcon(QStyle::SP_DialogCloseButton));

  QObject::connect(ui.cancel, &QToolButton::clicked, this, [=]() {
    if (mRunning) {
      int button = QMessageBox::question(
          this, "Transfer",
          QString("rclone process is still running. Do you want to cancel it?"),
          QMessageBox::Yes | QMessageBox::No);
      if (button == QMessageBox::Yes) {
        cancel();
      }
    } else {
      emit closed();
    }
  });

  ui.copy->setIcon(
      QApplication::style()->standardIcon(QStyle::SP_FileLinkIcon));

  QObject::connect(ui.copy, &QToolButton::clicked, this, [=]() {
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(BuildCommandLine(mArgs));
  });

  QObject::connect(mProcess, &QProcess::readyRead, this, [=]() {
    while (mProcess->canReadLine()) {
      applyOutputLine(QString(mProcess->readLine()).trimmed());
    }
  });

  QObject::connect(mProcess,
                   &QProcess::finished,
                   this, [=](int status, QProcess::ExitStatus) {
                     mProcess->deleteLater();
                     for (auto label : mActive) {
                       ui.progress->removeWidget(label->buddy());
                       ui.progress->removeWidget(label);
                       delete label->buddy();
                       delete label;
                     }

                     mRunning = false;
                     if (status == 0) {
                       ui.showDetails->setStyleSheet(
                           "QToolButton { border: 0; color: black; }");
                       ui.showDetails->setText("Finished");
                     } else {
                       ui.showDetails->setStyleSheet(
                           "QToolButton { border: 0; color: red; }");
                       ui.showDetails->setText("Error");
                     }

                     ui.cancel->setToolTip("Close");

                     emit finished(ui.info->text());

                     // A cancelled job closes its row once the process is
                     // actually gone, which is what makes cancel() able to
                     // return immediately without leaving the row behind.
                     if (mCancelled) {
                       emit closed();
                     }
                   });

  ui.showDetails->setStyleSheet("QToolButton { border: 0; color: green; }");
  ui.showDetails->setText("Running");
}

// One line of rclone's output, applied to the panel.
//
// Public, and separate from the readyRead handler, so the panel can be driven
// from a test without spawning a process: the test used to run /bin/sh, which
// is a thing Windows does not have.
void JobWidget::applyOutputLine(const QString &line) {
  ui.output->appendPlainText(line);

  if (line.isEmpty()) {
    for (auto it = mActive.begin(), eit = mActive.end(); it != eit;
         /* empty */) {
      auto label = it.value();
      if (mUpdated.contains(label)) {
        ++it;
      } else {
        it = mActive.erase(it);
        ui.progress->removeWidget(label->buddy());
        ui.progress->removeWidget(label);
        delete label->buddy();
        delete label;
      }
    }
    mUpdated.clear();
    return;
  }

  const RcloneStats stats = ParseRcloneStats(line);

  switch (stats.kind) {
  case RcloneStats::Unknown:
    break;

  case RcloneStats::Totals:
    // Before 1.43 rclone reported only a running total and a rate, so the
    // remaining fields keep whatever they last held rather than blinking
    // empty.
    ui.size->setText(stats.percent.isEmpty()
                         ? stats.size
                         : stats.size + ", " + stats.percent);
    ui.bandwidth->setText(stats.bandwidth);
    if (!stats.totalSize.isEmpty()) {
      ui.totalsize->setText(stats.totalSize);
    }
    if (!stats.eta.isEmpty()) {
      ui.eta->setText(stats.eta);
    }
    break;

  case RcloneStats::Errors:
    ui.errors->setText(stats.text);
    break;

  case RcloneStats::Checks:
    ui.checks->setText(stats.text);
    break;

  case RcloneStats::FileCount:
    ui.transferred->setText(stats.text);
    break;

  case RcloneStats::Elapsed:
    ui.elapsed->setText(stats.text);
    break;

  case RcloneStats::FileProgress: {
    auto it = mActive.find(stats.name);

    QLabel *label;
    QProgressBar *bar;
    if (it == mActive.end()) {
      label = new QLabel();
      label->setText(elideName(stats.name));

      bar = new QProgressBar();
      bar->setMinimum(0);
      bar->setMaximum(100);
      bar->setTextVisible(true);

      label->setBuddy(bar);

      ui.progress->addRow(label, bar);

      mActive.insert(stats.name, label);
    } else {
      label = it.value();
      bar = static_cast<QProgressBar *>(label->buddy());
    }

    bar->setValue(stats.filePercent);
    bar->setToolTip("File name: " + stats.name + "\nFile stats: " +
                    stats.fileDetail);

    mUpdated.insert(label);
    break;
  }
  }
}

JobWidget::~JobWidget() {}

void JobWidget::showDetails() { ui.showDetails->setChecked(true); }

void JobWidget::cancel() {
  if (!mRunning) {
    return;
  }

  mCancelled = true;
  ui.showDetails->setStyleSheet("QToolButton { border: 0; color: red; }");
  ui.showDetails->setText("Cancelling");

  // SIGTERM, not SIGKILL: rclone closes its connections, finishes writing the
  // chunk it is on and removes partial files on the way out. Killed outright
  // it leaves all three undone, and a killed mount leaves the mount point
  // behind. Windows has no SIGTERM, so terminate() there asks the process to
  // close its windows, which a console process ignores -- the timer below is
  // what actually stops it.
  mProcess->terminate();

  // Nothing waits on that: a blocking wait here froze the whole window for as
  // long as rclone took to notice. The row stays, showing "Cancelling", until
  // the finished handler closes it.
  QProcess *process = mProcess;
  QTimer::singleShot(5000, process, [process]() {
    if (process->state() != QProcess::NotRunning) {
      process->kill();
    }
  });
}
