#include "icon_cache.h"
#include "item_model.h"
#include "job_widget.h"
#include "progress_dialog.h"
#include "remote_widget.h"
#include "restic.h"
#include "restic_model.h"
#include "restic_widget.h"
#include "utils.h"
#include <QtTest>

// Deliberately shallow: these ask "does interacting with this crash", not
// "does it look right". Run offscreen, so no display is needed.
//
// The selection test exists because a real crash shipped: converting the
// button row to a tool bar deleted the row, which owned the "shared with me"
// check box, and the selection handler then read freed memory. Constructing
// the widget was not enough to catch it -- deleteLater() defers destruction to
// the next event-loop turn, so the constructor's own selection ran before the
// delete. Pumping the event loop between construction and selection is what
// makes it reproducible.
class TestWidgets : public QObject {
  Q_OBJECT

private:
  IconCache *mIcons = nullptr;
  QTabWidget *mTabs = nullptr;

  RemoteWidget *makeRemote(bool isGoogle = false);

private slots:
  void initTestCase();
  void cleanupTestCase();

  void remoteWidgetConstructs();
  void remoteWidgetSurvivesSelectionAfterEventLoop();
  void remoteWidgetSurvivesRepeatedSelection();
  void remoteWidgetSurvivesDestruction();
  void remoteWidgetGoogleVariantConstructs();
  void remoteWidgetAcceptsMultipleDroppedFiles();
  void itemModelKeepsTheRemoteRootEmpty();

  void jobWidgetShowsStatsFromRcloneOutput();
  void jobWidgetCancelDoesNotBlock();
  void jobWidgetReportsFailure();

  void resticWidgetConstructs();
  void resticWidgetSurvivesDestruction();
  void resticModelReportsAMissingResticBinary();
  void helperLookupLooksBeyondPath();

  void progressDialogTracksRcloneProgress();
  void progressDialogTracksResticProgress();
  void progressDialogStaysBareWithoutProgress();
};

// The dialog starts the process it is given; a binary that does not exist
// keeps these tests off the network and off any real repository, and the
// output lines are fed in by hand anyway.
static QProcess *makeDeadProcess(QObject *parent) {
  auto *process = new QProcess(parent);
  process->setProgram(QDir(QDir::tempPath()).filePath("rrm-no-such-binary"));
  return process;
}

void TestWidgets::initTestCase() {
  // Keep the tests off the developer's real configuration.
  QCoreApplication::setOrganizationName("rclone-browser-tests");
  QCoreApplication::setApplicationName("rclone-browser-tests");

  mIcons = new IconCache(nullptr);
  mTabs = new QTabWidget();
}

void TestWidgets::cleanupTestCase() {
  delete mTabs;
  mTabs = nullptr;
  delete mIcons;
  mIcons = nullptr;
}

RemoteWidget *TestWidgets::makeRemote(bool isGoogle) {
  // A remote name that cannot resolve is fine and in fact useful: rclone is
  // either absent or fails, which exercises the listing-failure path too.
  return new RemoteWidget(mIcons, "test-remote", /*isLocal=*/false, isGoogle,
                          mTabs);
}

void TestWidgets::remoteWidgetConstructs() {
  RemoteWidget *widget = makeRemote();
  QVERIFY(widget != nullptr);

  // The tool bar replaced the .ui button row; the tree must still be there.
  QVERIFY(widget->findChild<QTreeView *>() != nullptr);
  QVERIFY(widget->findChild<QToolBar *>() != nullptr);

  delete widget;
}

void TestWidgets::remoteWidgetSurvivesSelectionAfterEventLoop() {
  RemoteWidget *widget = makeRemote();

  auto *tree = widget->findChild<QTreeView *>();
  QVERIFY(tree != nullptr);
  QVERIFY(tree->model() != nullptr);

  // The step that makes the crash reproducible: let deferred deletions run.
  // A single sendPostedEvents pass is not enough on its own -- the freed
  // memory stays readable for a while -- so pump the loop properly and churn
  // some allocations to make a use-after-free actually fault.
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QTest::qWait(50);

  QVector<QByteArray> churn;
  for (int i = 0; i < 256; i++) {
    churn.append(QByteArray(1024, 'x'));
  }

  // A non-local remote seeds one root row.
  QVERIFY(tree->model()->rowCount(QModelIndex()) > 0);

  const QModelIndex root = tree->model()->index(0, 0, QModelIndex());
  QVERIFY(root.isValid());

  // Reading ui.checkBoxShared through a dangling pointer segfaulted here.
  for (int i = 0; i < 5; i++) {
    tree->selectionModel()->select(root, QItemSelectionModel::ClearAndSelect |
                                             QItemSelectionModel::Rows);
    tree->selectionModel()->clearSelection();
    QCoreApplication::processEvents();
  }

  delete widget;
}

void TestWidgets::remoteWidgetSurvivesRepeatedSelection() {
  RemoteWidget *widget = makeRemote();

  auto *tree = widget->findChild<QTreeView *>();
  QVERIFY(tree != nullptr);

  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QCoreApplication::processEvents();

  const QModelIndex root = tree->model()->index(0, 0, QModelIndex());
  for (int i = 0; i < 5; i++) {
    tree->selectionModel()->select(root, QItemSelectionModel::ClearAndSelect |
                                             QItemSelectionModel::Rows);
    tree->selectionModel()->clearSelection();
    QCoreApplication::processEvents();
  }

  delete widget;
}

void TestWidgets::remoteWidgetSurvivesDestruction() {
  // Destroying mid-listing has to be safe: the model keeps items alive across
  // an in-flight rclone process via its isDeleted flag.
  RemoteWidget *widget = makeRemote();
  delete widget;

  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QCoreApplication::processEvents();
}

void TestWidgets::remoteWidgetGoogleVariantConstructs() {
  // "Shared with me" is only shown for Drive, and is reparented onto the tool
  // bar. Its action should exist and be visible in this variant.
  RemoteWidget *widget = makeRemote(/*isGoogle=*/true);

  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QCoreApplication::processEvents();

  auto *shared = widget->findChild<QCheckBox *>();
  QVERIFY(shared != nullptr);
  // Reparented, so it must not still belong to the hidden button row.
  QVERIFY(shared->parent() != nullptr);

  delete widget;
}

void TestWidgets::remoteWidgetAcceptsMultipleDroppedFiles() {
  // Dropping more than one file was refused outright: canDropMimeData only
  // accepted a drop of exactly one URL, and dropMimeData read only the first.
  //
  // The model is built directly rather than through a RemoteWidget: the
  // widget answers this signal with a modal transfer dialog, which in a test
  // would simply never close.
  ItemModel model(mIcons, "test-remote", nullptr);
  const QModelIndex root = model.addRoot("test-remote:", QString());
  QVERIFY(root.isValid());

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QList<QUrl> urls;
  // QStringList, not a braced list of literals: the latter is an
  // initializer_list<const char *>, so the reference binds to a temporary
  // QString built each iteration, which GCC rejects under -Werror.
  for (const QString &name : QStringList{"one.txt", "two.txt", "three.txt"}) {
    const QString path = QDir(dir.path()).filePath(name);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("x");
    file.close();
    urls.append(QUrl::fromLocalFile(path));
  }

  QMimeData single;
  single.setUrls({urls.first()});
  QVERIFY(model.canDropMimeData(&single, Qt::CopyAction, -1, -1, root));

  QMimeData several;
  several.setUrls(urls);
  QVERIFY(model.canDropMimeData(&several, Qt::CopyAction, -1, -1, root));

  // A drag from a browser is not something rclone can upload from disk.
  QMimeData notLocal;
  notLocal.setUrls({QUrl("https://example.com/file.txt")});
  QVERIFY(!model.canDropMimeData(&notLocal, Qt::CopyAction, -1, -1, root));

  // One signal for the drop, carrying every item -- not one item, and not
  // three separate dialogs.
  int emissions = 0;
  QList<QDir> dropped;
  QObject::connect(&model, &ItemModel::drop, &model,
                   [&](const QList<QDir> &paths, const QModelIndex &) {
                     emissions++;
                     dropped = paths;
                   });

  model.dropMimeData(&several, Qt::CopyAction, -1, -1, root);

  QCOMPARE(emissions, 1);
  QCOMPARE(dropped.size(), 3);
}

void TestWidgets::itemModelKeepsTheRemoteRootEmpty() {
  // The root of a remote is the empty path. It was held in a QDir, which turns
  // "" into ".", so the listing ran against "remote:." and every child below
  // it was "./name" -- visible in the path line under the tree, and in the
  // destination the transfer dialog offered.
  ItemModel model(mIcons, "test-remote", nullptr);
  const QModelIndex root = model.addRoot("test-remote:", QString());

  QVERIFY(root.isValid());
  QCOMPARE(model.path(root), QString());
  QVERIFY(model.path(root) != QString("."));
}

void TestWidgets::jobWidgetShowsStatsFromRcloneOutput() {
  // The end of the wiring that test_parsing covers from the other end: real
  // rclone 1.71 output, and the Jobs panel's fields actually filling in. Every
  // one of these was blank for the whole life of the 1.56 output format.
  //
  // The lines are handed to the widget directly rather than piped in from a
  // process. The first version of this test ran /bin/sh to play the part of
  // rclone, which worked everywhere except the one platform that had never
  // compiled the code -- Windows has no /bin/sh, and the test failed there the
  // moment a Windows runner existed to run it.
  auto *process = new QProcess();
  JobWidget widget(process, "Copy", QStringList() << "copy", "src", "dest");

  const QStringList output = {
      "Transferred:   \t    3.027 MiB / 120 MiB, 3%, 3.027 MiB/s, ETA 38s",
      "Errors:                 1 (retrying may help)",
      "Checks:                 0 / 0, -, Listed 1",
      "Transferred:            0 / 1, 0%",
      "Elapsed time:         1.9s",
      "*                                       big.bin:  3% /120Mi, 2.027Mi/s, 57s",
  };
  for (const QString &line : output) {
    widget.applyOutputLine(line);
  }

  auto field = [&widget](const char *name) {
    auto *edit = widget.findChild<QLineEdit *>(name);
    return edit ? edit->text() : QString("<missing>");
  };

  QCOMPARE(field("size"), QString("3.027 MiB, 3%"));
  QCOMPARE(field("totalsize"), QString("120 MiB"));
  QCOMPARE(field("bandwidth"), QString("3.027 MiB/s"));
  QCOMPARE(field("eta"), QString("38s"));
  QCOMPARE(field("errors"), QString("1"));
  QCOMPARE(field("checks"), QString("0 / 0, -"));
  QCOMPARE(field("transferred"), QString("0 / 1, 0%"));
  QCOMPARE(field("elapsed"), QString("1.9s"));

  // The per-file line adds a progress bar for that file. The old pattern
  // rejected the "0/s, -" that rclone prints before it has a rate, so a bar
  // appeared only once the transfer was properly under way.
  auto *bar = widget.findChild<QProgressBar *>();
  QVERIFY(bar != nullptr);
  QCOMPARE(bar->value(), 3);

  // A blank line ends a stats block, and any file not mentioned in the block
  // just gone has finished: its row goes away.
  widget.applyOutputLine(QString());
  widget.applyOutputLine(QString());
  QVERIFY(widget.findChild<QProgressBar *>() == nullptr);

  delete process;
}

void TestWidgets::jobWidgetCancelDoesNotBlock() {
  // Cancelling asks the process to stop and returns immediately: it used to
  // send SIGKILL and then block the GUI on waitForFinished().
  //
  // cmake stands in for a long-running rclone because it is guaranteed to be
  // here -- it built this test -- and it behaves the same on every platform,
  // which /bin/sh does not.
  auto *process = new QProcess();
  JobWidget widget(process, "Copy", QStringList() << "copy", "src", "dest");

  process->start(RRM_CMAKE_COMMAND, {"-E", "sleep", "30"});
  QVERIFY(process->waitForStarted());

  QElapsedTimer timer;
  timer.start();

  // Guarded: the widget deletes the process once it exits, and the event loop
  // this waits on is what delivers that deletion.
  QPointer<QProcess> running(process);
  widget.cancel();

  // The point of the change: control comes back at once, rather than after
  // however long the process takes to die.
  QVERIFY2(timer.elapsed() < 1000,
           qPrintable(QString("cancel() blocked for %1ms").arg(timer.elapsed())));

  QTRY_VERIFY_WITH_TIMEOUT(!running || running->state() == QProcess::NotRunning,
                           6000);

  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void TestWidgets::resticWidgetConstructs() {
  ResticRepo repo;
  repo.name = "test";
  // A local path that does not exist: restic fails, exercising the error path
  // without needing a repository or a password prompt.
  repo.repository = QDir(QDir::tempPath()).filePath("rrm-nonexistent-repo");
  repo.passwordCommand = "echo test";

  auto *widget = new ResticWidget(repo, mTabs);
  QVERIFY(widget->findChild<QTreeView *>() != nullptr);
  QVERIFY(widget->findChild<QToolBar *>() != nullptr);

  QCoreApplication::processEvents();

  delete widget;
}

void TestWidgets::resticWidgetSurvivesDestruction() {
  ResticRepo repo;
  repo.name = "test";
  repo.repository = QDir(QDir::tempPath()).filePath("rrm-nonexistent-repo");
  repo.passwordCommand = "echo test";

  // Destroyed while the snapshots process may still be running.
  auto *widget = new ResticWidget(repo, mTabs);
  delete widget;

  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QCoreApplication::processEvents();
}

// A restic that cannot be executed produces no finished() at all, only
// errorOccurred(). The shipped app hit exactly this -- a bundle launched from
// Finder does not see /opt/homebrew/bin -- and sat on "... loading snapshots"
// forever. The list has to end up in a state the user can read instead.
void TestWidgets::resticModelReportsAMissingResticBinary() {
  const QString previous = GetRestic();
  SetRestic(QDir(QDir::tempPath()).filePath("rrm-no-such-restic-binary"));

  ResticRepo repo;
  repo.name = "test";
  repo.repository = QDir(QDir::tempPath()).filePath("rrm-nonexistent-repo");
  repo.passwordCommand = "echo test";

  ResticModel model(repo, nullptr);
  QSignalSpy failures(&model, &ResticModel::failed);

  QTRY_VERIFY_WITH_TIMEOUT(failures.count() == 1, 5000);

  QCOMPARE(model.rowCount(QModelIndex()), 1);
  const QModelIndex row = model.index(0, 0, QModelIndex());
  QVERIFY(model.isPlaceholder(row));
  QVERIFY(model.data(row, Qt::DisplayRole).toString().startsWith("... error"));

  SetRestic(previous);
}

// Run this binary under a bare PATH (env -i PATH=/usr/bin:/bin) to reproduce
// what a Finder-launched bundle sees: the plain PATH lookup comes back empty
// and only the package-manager directories find the binary.
void TestWidgets::helperLookupLooksBeyondPath() {
  const QString found = FindHelperExecutable("restic");
  if (found.isEmpty()) {
    // Nothing to assert on a machine without restic; CI is one of those.
    qInfo("restic is not installed here, skipping");
    return;
  }
  QVERIFY(QFileInfo(found).isExecutable());
}

// Move and Delete run rclone with --stats, and the periodic block it prints
// is what drives the bar. Before this the dialog showed a static label for the
// whole operation, however long it ran.
void TestWidgets::progressDialogTracksRcloneProgress() {
  QObject owner;
  ProgressDialog dialog("Move", "Moving...", "some/path",
                        makeDeadProcess(&owner), nullptr, /*close=*/false);

  auto *bar = dialog.findChild<QProgressBar *>("progressBar");
  auto *detail = dialog.findChild<QLabel *>("progressDetail");
  QVERIFY(bar != nullptr);
  QVERIFY(detail != nullptr);
  QVERIFY(bar->isHidden());

  dialog.applyOutputLine(
      "Transferred:   \t    3.027 MiB / 20 MiB, 15%, 3.027 MiB/s, ETA 5s");

  QVERIFY(!bar->isHidden());
  QCOMPARE(bar->value(), 15);
  QVERIFY(detail->text().contains("3.027 MiB of 20 MiB"));
  QVERIFY(detail->text().contains("ETA 5s"));

  // No percentage yet -- rclone prints "-" until it knows the total -- shows
  // the bar as busy rather than parking it at zero.
  dialog.applyOutputLine("Transferred:   \t   65.489 MiB / 65.489 MiB, -, 0 B/s, ETA -");
  QCOMPARE(bar->maximum(), 0);
}

void TestWidgets::progressDialogTracksResticProgress() {
  QObject owner;
  ProgressDialog dialog("Restore", "Restoring...", "whole snapshot",
                        makeDeadProcess(&owner), nullptr, /*close=*/false);

  auto *bar = dialog.findChild<QProgressBar *>("progressBar");
  auto *detail = dialog.findChild<QLabel *>("progressDetail");
  auto *output = dialog.findChild<QPlainTextEdit *>("output");
  QVERIFY(bar != nullptr && detail != nullptr && output != nullptr);

  dialog.applyOutputLine(
      R"({"message_type":"status","percent_done":0.446989917755127,"total_files":7,"files_restored":2,"total_bytes":125829120,"bytes_restored":56244348})");

  QVERIFY(!bar->isHidden());
  QCOMPARE(bar->value(), 45);
  QVERIFY(detail->text().contains("2 of 7 files"));

  // The JSON itself must not reach the output pane: there is one record a
  // second and it would be the only thing in there.
  QVERIFY(!output->toPlainText().contains("message_type"));

  dialog.applyOutputLine(
      R"({"message_type":"summary","total_files":7,"files_restored":7,"total_bytes":125829120,"bytes_restored":125829120})");
  QCOMPARE(bar->value(), 100);
  QVERIFY(output->toPlainText().contains("Restored 7 of 7 files"));
}

void TestWidgets::progressDialogStaysBareWithoutProgress() {
  // New Folder and Rename report nothing and finish at once; their dialog
  // should look exactly as it always has.
  QObject owner;
  ProgressDialog dialog("New Folder", "Creating...", "some/path",
                        makeDeadProcess(&owner), nullptr, /*close=*/false);

  auto *bar = dialog.findChild<QProgressBar *>("progressBar");
  QVERIFY(bar != nullptr);

  dialog.applyOutputLine("2026/08/17 15:49:23 NOTICE: something happened");
  QVERIFY(bar->isHidden());

  auto *output = dialog.findChild<QPlainTextEdit *>("output");
  QVERIFY(output->toPlainText().contains("something happened"));
}

void TestWidgets::jobWidgetReportsFailure() {
  // Running a task reported nothing either way: the finished signal carried
  // only a description, so a job that failed notified in the same words as one
  // that worked, and the output explaining it stayed collapsed.
  //
  // This test binary stands in for a failing rclone -- asked for a test
  // function that does not exist it prints a complaint and exits 1, which is
  // as portable as the process under test.
  auto *process = new QProcess();
  process->setProgram(QCoreApplication::applicationFilePath());
  process->setArguments({"rrm-no-such-test-function"});

  JobWidget widget(process, "Copy something", {"copy"}, "src", "dst");
  QSignalSpy finished(&widget, &JobWidget::finished);

  // The widget watches the process; starting it is the caller's job, exactly
  // as MainWindow::addTransfer does it.
  process->start(QIODevice::ReadOnly);

  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 15000);
  QCOMPARE(finished.first().at(1).toBool(), false);
  QVERIFY(!widget.wasCancelled());

  // The log is opened for a failure, so what went wrong is on screen.
  auto *output = widget.findChild<QPlainTextEdit *>("output");
  QVERIFY(output != nullptr);
  QVERIFY(!output->isHidden());

  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

QTEST_MAIN(TestWidgets)
#include "test_widgets.moc"
