#include "icon_cache.h"
#include "item_model.h"
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
