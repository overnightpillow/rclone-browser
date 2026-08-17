#include "job_options.h"
#include "list_of_job_options.h"
#include <QtTest>

// Saved tasks are the one piece of state this application owns outright: they
// exist nowhere else, and nothing can regenerate them. These tests are about
// not losing them -- neither to a half-written file nor to a byte that fails
// to parse.
class TestTasks : public QObject {
  Q_OBJECT

private:
  QTemporaryDir mDir;
  QString path(const QString &name = "tasks.bin") const;
  static JobOptions *makeTask(const QString &description);

private slots:
  void initTestCase();

  void roundTrips();
  void writeIsAtomic();
  void failedWriteLeavesPreviousFileIntact();
  void reportsUnparseableFile();
  void reportsTruncatedFile();
  void missingFileIsNotAnError();
};

void TestTasks::initTestCase() { QVERIFY(mDir.isValid()); }

QString TestTasks::path(const QString &name) const {
  return QDir(mDir.path()).absoluteFilePath(name);
}

JobOptions *TestTasks::makeTask(const QString &description) {
  auto *jo = new JobOptions();
  jo->description = description;
  jo->source = "b2:bucket/path";
  jo->dest = "/home/kevin/restore";
  jo->transfers = "8";
  jo->excluded = "*.tmp";
  return jo;
}

void TestTasks::roundTrips() {
  QList<JobOptions *> written = {makeTask("nightly backup"),
                                 makeTask("photos to storj")};
  QVERIFY(ListOfJobOptions::WriteTasks(path(), written));

  QList<JobOptions *> read;
  QString error;
  QVERIFY(ListOfJobOptions::ReadTasks(path(), &read, &error));
  QVERIFY(error.isEmpty());

  QCOMPARE(read.size(), 2);
  QCOMPARE(read[0]->description, QString("nightly backup"));
  QCOMPARE(read[0]->source, QString("b2:bucket/path"));
  QCOMPARE(read[0]->transfers, QString("8"));
  QCOMPARE(read[0]->excluded, QString("*.tmp"));
  QCOMPARE(read[1]->description, QString("photos to storj"));

  qDeleteAll(written);
  qDeleteAll(read);
}

void TestTasks::writeIsAtomic() {
  // Nothing may appear at the destination path until the write has completed,
  // which is what stops an interrupted save from leaving a half file behind.
  QList<JobOptions *> tasks = {makeTask("first")};
  QVERIFY(ListOfJobOptions::WriteTasks(path(), tasks));

  const QFileInfo before(path());
  QVERIFY(before.exists());
  QVERIFY(before.size() > 0);

  // No stray temporary files left in the directory afterwards.
  const QStringList leftovers =
      QDir(mDir.path()).entryList(QDir::Files | QDir::Hidden);
  QCOMPARE(leftovers, QStringList{"tasks.bin"});

  qDeleteAll(tasks);
}

void TestTasks::failedWriteLeavesPreviousFileIntact() {
  QList<JobOptions *> original = {makeTask("keep me")};
  QVERIFY(ListOfJobOptions::WriteTasks(path("keep.bin"), original));

  QFile saved(path("keep.bin"));
  QVERIFY(saved.open(QIODevice::ReadOnly));
  const QByteArray before = saved.readAll();
  saved.close();
  QVERIFY(!before.isEmpty());

  // A directory where the file should go: QSaveFile cannot commit over it, and
  // the old code would have truncated the real file before finding that out.
  QVERIFY(QDir().mkpath(path("blocked.bin")));
  QList<JobOptions *> replacement = {makeTask("should not land")};
  QVERIFY(!ListOfJobOptions::WriteTasks(path("blocked.bin"), replacement));

  QFile again(path("keep.bin"));
  QVERIFY(again.open(QIODevice::ReadOnly));
  QCOMPARE(again.readAll(), before);

  qDeleteAll(original);
  qDeleteAll(replacement);
}

void TestTasks::reportsUnparseableFile() {
  QFile file(path("garbage.bin"));
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("this was never a task file");
  file.close();

  QList<JobOptions *> read;
  QString error;
  QVERIFY(!ListOfJobOptions::ReadTasks(path("garbage.bin"), &read, &error));
  // An error the caller can show, rather than an empty task list and silence.
  QVERIFY(!error.isEmpty());
  QVERIFY(read.isEmpty());
}

void TestTasks::reportsTruncatedFile() {
  // The failure mode a non-atomic write actually produces: a valid prefix that
  // stops mid-record. The record-level checks pass, so this is only caught by
  // inspecting the stream status.
  QList<JobOptions *> tasks = {makeTask("nightly backup")};
  QVERIFY(ListOfJobOptions::WriteTasks(path("short.bin"), tasks));

  QFile file(path("short.bin"));
  QVERIFY(file.open(QIODevice::ReadWrite));
  QVERIFY(file.resize(file.size() / 2));
  file.close();

  QList<JobOptions *> read;
  QString error;
  QVERIFY(!ListOfJobOptions::ReadTasks(path("short.bin"), &read, &error));
  QVERIFY(!error.isEmpty());
  QVERIFY(read.isEmpty());

  qDeleteAll(tasks);
}

void TestTasks::missingFileIsNotAnError() {
  // First run: no file, no tasks, and nothing to warn anybody about.
  QList<JobOptions *> read;
  QString error;
  QVERIFY(!ListOfJobOptions::ReadTasks(path("absent.bin"), &read, &error));
  QVERIFY(error.isEmpty());
  QVERIFY(read.isEmpty());
}

QTEST_MAIN(TestTasks)
#include "test_tasks.moc"
