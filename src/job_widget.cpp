#include "job_widget.h"
#include "utils.h"

namespace {
// Qt6 removed QRegExp. QRegularExpression has no exactMatch(), so patterns are
// wrapped in anchoredPattern() to keep the whole-string matching the parsing
// below depends on.
//
// These are file-scope because they were previously constructed inside the
// readyRead handler, recompiling eleven patterns on every chunk of rclone
// output.
QRegularExpression anchored(const QString &pattern) {
  return QRegularExpression(QRegularExpression::anchoredPattern(pattern));
}

// Until rclone 1.42
const QRegularExpression rxSize(
    anchored(R"(Transferred:\s+(\S+ \S+) \(([^)]+)\))"));
// Starting with rclone 1.43
const QRegularExpression rxSize2(anchored(
    R"(Transferred:\s+([0-9.]+)(\S)? \/ (\S+) (\S+), ([0-9%-]+), (\S+ \S+), (\S+) (\S+))"));
const QRegularExpression rxErrors(anchored(R"(Errors:\s+(\S+))"));
// Until rclone 1.42
const QRegularExpression rxChecks(anchored(R"(Checks:\s+(\S+))"));
// Starting with rclone 1.43
const QRegularExpression rxChecks2(
    anchored(R"(Checks:\s+(\S+) \/ (\S+), ([0-9%-]+))"));
// Until rclone 1.42
const QRegularExpression rxTransferred(anchored(R"(Transferred:\s+(\S+))"));
// Starting with rclone 1.43
const QRegularExpression rxTransferred2(
    anchored(R"(Transferred:\s+(\S+) \/ (\S+), ([0-9%-]+))"));
const QRegularExpression rxTime(anchored(R"(Elapsed time:\s+(\S+))"));
// Until rclone 1.38
const QRegularExpression rxProgress(
    anchored(R"(\*([^:]+):\s*([^%]+)% done.+(ETA: [^)]+))"));
// Starting with rclone 1.39
const QRegularExpression rxProgress2(anchored(
    R"(\*([^:]+):\s*([^%]+)% \/[a-zA-Z0-9.]+, [a-zA-Z0-9.]+\/s, (\w+))"));
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
    clipboard->setText(mArgs.join(" "));
  });

  QObject::connect(mProcess, &QProcess::readyRead, this, [=]() {
    while (mProcess->canReadLine()) {
      QString line = mProcess->readLine().trimmed();
      if (++mLines == 10000) {
        ui.output->clear();
        mLines = 1;
      }
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
        continue;
      }

      QRegularExpressionMatch m;

      if ((m = rxSize.match(line)).hasMatch()) {
        ui.size->setText(m.captured(1));
        ui.bandwidth->setText(m.captured(2));
      } else if ((m = rxSize2.match(line)).hasMatch()) {
        ui.size->setText(m.captured(1) + " " + m.captured(2) + "B" + ", " +
                         m.captured(5));
        ui.bandwidth->setText(m.captured(6));
        ui.eta->setText(m.captured(8));
        ui.totalsize->setText(m.captured(3) + " " + m.captured(4));
      } else if ((m = rxErrors.match(line)).hasMatch()) {
        ui.errors->setText(m.captured(1));
      } else if ((m = rxChecks.match(line)).hasMatch()) {
        ui.checks->setText(m.captured(1));
      } else if ((m = rxChecks2.match(line)).hasMatch()) {
        ui.checks->setText(m.captured(1) + " / " + m.captured(2) + ", " +
                           m.captured(3));
      } else if ((m = rxTransferred.match(line)).hasMatch()) {
        ui.transferred->setText(m.captured(1));
      } else if ((m = rxTransferred2.match(line)).hasMatch()) {
        ui.transferred->setText(m.captured(1) + " / " + m.captured(2) + ", " +
                                m.captured(3));
      } else if ((m = rxTime.match(line)).hasMatch()) {
        ui.elapsed->setText(m.captured(1));
      } else if ((m = rxProgress.match(line)).hasMatch()) {
        QString name = m.captured(1).trimmed();

        auto it = mActive.find(name);

        QLabel *label;
        QProgressBar *bar;
        if (it == mActive.end()) {
          label = new QLabel();
          label->setText(name);

          bar = new QProgressBar();
          bar->setMinimum(0);
          bar->setMaximum(100);
          bar->setTextVisible(true);

          label->setBuddy(bar);

          ui.progress->addRow(label, bar);

          mActive.insert(name, label);
        } else {
          label = it.value();
          bar = static_cast<QProgressBar *>(label->buddy());
        }

        bar->setValue(m.captured(2).toInt());
        bar->setToolTip(m.captured(3));

        mUpdated.insert(label);
      } else if ((m = rxProgress2.match(line)).hasMatch()) {
        QString name = m.captured(1).trimmed();

        auto it = mActive.find(name);

        QLabel *label;
        QProgressBar *bar;
        if (it == mActive.end()) {
          label = new QLabel();

          QString nameTrimmed;

          if (name.length() > 47) {
            nameTrimmed = name.left(25) + "..." + name.right(19);
          } else {
            nameTrimmed = name;
          }

          label->setText(nameTrimmed);

          bar = new QProgressBar();
          bar->setMinimum(0);
          bar->setMaximum(100);
          bar->setTextVisible(true);

          label->setBuddy(bar);

          ui.progress->addRow(label, bar);

          mActive.insert(name, label);
        } else {
          label = it.value();
          bar = static_cast<QProgressBar *>(label->buddy());
        }

        bar->setValue(m.captured(2).toInt());
        bar->setToolTip("File name: " + name + "\nFile stats" +
                        m.captured(0).mid(m.captured(0).indexOf(QLatin1Char(':'))));

        mUpdated.insert(label);
      }
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
                   });

  ui.showDetails->setStyleSheet("QToolButton { border: 0; color: green; }");
  ui.showDetails->setText("Running");
}

JobWidget::~JobWidget() {}

void JobWidget::showDetails() { ui.showDetails->setChecked(true); }

void JobWidget::cancel() {
  if (!mRunning) {
    return;
  }

  mProcess->kill();
  mProcess->waitForFinished();

  emit closed();
}
