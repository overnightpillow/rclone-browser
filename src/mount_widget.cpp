#include "mount_widget.h"
#include "utils.h"

MountWidget::MountWidget(QProcess *process, const QString &remote,
                         const QString &folder, QWidget *parent)
    : QWidget(parent), mProcess(process) {
  ui.setupUi(this);

  ui.remote->setText(remote);
  ui.folder->setText(folder);
  ui.info->setText(QString("%1 on %2").arg(remote).arg(folder));

  ui.details->setVisible(false);

  ui.output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  ui.output->setVisible(false);
  // A mount can log for days, and nothing trimmed this at all before.
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
          this, "Unmount",
#if defined(Q_OS_WIN)
          QString("Do you want to unmount %1 drive?").arg(folder),
#else
          QString("Do you want to unmount %1 folder?").arg(folder),
#endif
          QMessageBox::Yes | QMessageBox::No);
      if (button == QMessageBox::Yes) {
        cancel();
      }
    } else {
      emit closed();
    }
  });

  QObject::connect(mProcess, &QProcess::readyRead, this, [=]() {
    while (mProcess->canReadLine()) {
      ui.output->appendPlainText(mProcess->readLine().trimmed());
    }
  });

  QObject::connect(mProcess,
                   &QProcess::finished,
                   this, [=](int status, QProcess::ExitStatus) {
                     mProcess->deleteLater();
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
                     emit finished();

                     // Unmounting is asynchronous: the row closes when rclone
                     // has actually let go of the mount point, not when the
                     // unmount command was merely started.
                     if (mCancelled) {
                       emit closed();
                     }
                   });

  ui.showDetails->setStyleSheet("QToolButton { border: 0; color: green; }");
  ui.showDetails->setText("Mounted");
}

MountWidget::~MountWidget() {}

void MountWidget::cancel() {
  if (!mRunning) {
    return;
  }

  mCancelled = true;
  ui.showDetails->setStyleSheet("QToolButton { border: 0; color: red; }");
  ui.showDetails->setText("Unmounting");

#if defined(Q_OS_MACOS) || defined(Q_OS_FREEBSD)
  QProcess::startDetached("umount", QStringList() << ui.folder->text());
#else
#if defined(Q_OS_WIN32)
  // Parented, so the object is freed with the widget rather than leaked on
  // every unmount.
  QProcess *p = new QProcess(this);
  QStringList args;
  args << "rc";
  // requires rlone version at least 1.50
  args << "core/quit";

  args << "--rc-addr";
  QString folder = ui.folder->text();

  int port_offset = folder[0].toLatin1();
  unsigned short int rclone_rc_port_base = 19000;
  unsigned short int rclone_rc_port = rclone_rc_port_base + port_offset;
  args << "localhost:" + QVariant(rclone_rc_port).toString();

  UseRclonePassword(p);
  p->start(GetRclone(), args, QIODevice::ReadOnly);
#else
  QProcess::startDetached("fusermount", QStringList()
                                            << "-u" << ui.folder->text());
#endif
#endif

  // The old code blocked here on waitForFinished(), so an unmount that could
  // not complete -- a file still open on the mount point is enough -- froze the
  // window with no way to tell what was happening. If rclone has not exited
  // ten seconds after the unmount was requested, escalate rather than wait.
  QProcess *process = mProcess;
  QTimer::singleShot(10000, process, [process]() {
    if (process->state() != QProcess::NotRunning) {
      process->terminate();
      QTimer::singleShot(5000, process, [process]() {
        if (process->state() != QProcess::NotRunning) {
          process->kill();
        }
      });
    }
  });
}
