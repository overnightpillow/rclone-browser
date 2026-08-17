#include "stream_widget.h"

StreamWidget::StreamWidget(QProcess *rclone, QProcess *player,
                           const QString &remote, const QString &stream,
                           QWidget *parent)
    : QWidget(parent), mRclone(rclone), mPlayer(player) {
  ui.setupUi(this);

  ui.remote->setText(remote);
  ui.stream->setText(stream);
  ui.info->setText(remote);

  ui.details->setVisible(false);

  ui.output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  ui.output->setVisible(false);
  // A stream can run for hours; nothing bounded this log before.
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
          this, "Stop", QString("Do you want to stop %1 stream?").arg(remote),
          QMessageBox::Yes | QMessageBox::No);
      if (button == QMessageBox::Yes) {
        cancel();
      }
    } else {
      emit closed();
    }
  });

  QObject::connect(mRclone, &QProcess::readyRead, this, [=]() {
    while (mRclone->canReadLine()) {
      ui.output->appendPlainText(mRclone->readLine().trimmed());
    }
  });

  QObject::connect(mRclone,
                   &QProcess::finished,
                   this, [=](int status, QProcess::ExitStatus) {
                     mRclone->deleteLater();
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

                     // Emitting closed() here as well is what made this crash:
                     // MainWindow's closed handler deletes the separator line
                     // and the widget, and it ran a second time when the user
                     // then clicked the close button, deleting both again. A
                     // stream that ends on its own now leaves its row in place,
                     // like a finished transfer does, and the user dismisses it.
                     if (mCancelled) {
                       emit closed();
                     }
                   });

  ui.showDetails->setStyleSheet("QToolButton { border: 0; color: green; }");
  ui.showDetails->setText("Streaming");
}

StreamWidget::~StreamWidget() {}

void StreamWidget::cancel() {
  if (!mRunning) {
    return;
  }

  mCancelled = true;
  ui.showDetails->setStyleSheet("QToolButton { border: 0; color: red; }");
  ui.showDetails->setText("Stopping");

  // The player is asked to close first: killing rclone out from under it can
  // leave a media player sitting on a broken pipe rather than exiting.
  mPlayer->terminate();
  mRclone->terminate();

  // Each timer takes its own process as context, so it is cancelled if that
  // process exits and is deleted first, and survives this widget being closed.
  QProcess *player = mPlayer;
  QTimer::singleShot(2000, player, [player]() {
    if (player->state() != QProcess::NotRunning) {
      player->kill();
    }
  });

  QProcess *rclone = mRclone;
  QTimer::singleShot(2000, rclone, [rclone]() {
    if (rclone->state() != QProcess::NotRunning) {
      rclone->kill();
    }
  });
}
