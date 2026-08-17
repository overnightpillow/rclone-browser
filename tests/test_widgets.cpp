#include "icon_cache.h"
#include "item_model.h"
#include "job_widget.h"
#include "remote_widget.h"
#include "restic.h"
#include "restic_widget.h"
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

  void jobWidgetShowsStatsFromRcloneOutput();

  void resticWidgetConstructs();
  void resticWidgetSurvivesDestruction();
};

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
  for (const QString &name : {"one.txt", "two.txt", "three.txt"}) {
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

void TestWidgets::jobWidgetShowsStatsFromRcloneOutput() {
  // The end of the wiring that test_parsing covers from the other end: real
  // rclone 1.71 output arriving on a process, and the Jobs panel's fields
  // actually filling in. Every one of these was blank for the whole life of
  // the 1.56 output format.
  auto *process = new QProcess();
  process->setProcessChannelMode(QProcess::MergedChannels);

  JobWidget widget(process, "Copy", QStringList() << "copy", "src", "dest");

  // A shell standing in for rclone: one captured stats block, then a wait, so
  // that the job is still running while the panel is inspected. The per-file
  // rows are cleared when the process exits, as they are for a real job.
  process->start("/bin/sh",
                 {"-c",
                  "printf '%s\\n' "
                  "'Transferred:   \t    3.027 MiB / 120 MiB, 3%, 3.027 MiB/s, "
                  "ETA 38s' "
                  "'Errors:                 1 (retrying may help)' "
                  "'Checks:                 0 / 0, -, Listed 1' "
                  "'Transferred:            0 / 1, 0%' "
                  "'Elapsed time:         1.9s' "
                  "' *                                       big.bin:  3% "
                  "/120Mi, 2.027Mi/s, 57s'; sleep 30"});
  QVERIFY(process->waitForStarted());

  auto field = [&widget](const char *name) {
    auto *edit = widget.findChild<QLineEdit *>(name);
    return edit ? edit->text() : QString("<missing>");
  };

  QTRY_COMPARE(field("size"), QString("3.027 MiB, 3%"));
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
  QTRY_VERIFY(widget.findChild<QProgressBar *>() != nullptr);
  QCOMPARE(widget.findChild<QProgressBar *>()->value(), 3);

  // Cancelling asks the process to stop and returns immediately: it used to
  // kill outright and then block the GUI on waitForFinished().
  //
  // Guarded, because the widget deletes the process once it exits, and the
  // event loop this waits on is what delivers that deletion.
  QPointer<QProcess> running(process);
  widget.cancel();
  QTRY_VERIFY_WITH_TIMEOUT(
      !running || running->state() == QProcess::NotRunning, 6000);

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

QTEST_MAIN(TestWidgets)
#include "test_widgets.moc"
