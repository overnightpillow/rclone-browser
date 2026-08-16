#include "formatting.h"
#include "restic.h"
#include "utils.h"
#include <QtTest>

// Every case here is a regression test: each one corresponds to a bug that was
// actually present in the code at some point, not a hypothetical.
class TestFormatting : public QObject {
  Q_OBJECT

private slots:
  void sizeBelowTenBytes();
  void sizeBoundaries();
  void modTimeSentinelIsBlank();
  void modTimeRealTimestamp();
  void modTimeNanosecondPrecision();
  void modTimeIsFixedWidthAndSortable();
  void modTimeGarbagePassesThrough();
  void versionComparison();
  void versionWithNonNumericComponent();
  void resticRepoForRemote();
};

void TestFormatting::sizeBelowTenBytes() {
  // The original loop ran to i == 0 with a ">= 10 * base" test, so everything
  // under ten bytes rendered as the literal string "0".
  QCOMPARE(FormatSize(0), QString("0"));
  QCOMPARE(FormatSize(1), QString("1"));
  QCOMPARE(FormatSize(5), QString("5"));
  QCOMPARE(FormatSize(9), QString("9"));
}

void TestFormatting::sizeBoundaries() {
  QCOMPARE(FormatSize(10), QString("10"));
  QCOMPARE(FormatSize(1023), QString("1023"));
  // Below 10 KiB stays exact rather than rounding to "1 K".
  QCOMPARE(FormatSize(1024), QString("1024"));
  QCOMPARE(FormatSize(10 * 1024), QString("10 K"));
  QCOMPARE(FormatSize(10 * 1024 * 1024), QString("10 M"));
  QCOMPARE(FormatSize(quint64(10) * 1024 * 1024 * 1024), QString("10 G"));
}

void TestFormatting::modTimeSentinelIsBlank() {
  // B2 and S3 directories are synthetic; rclone reports this sentinel. Shown
  // in a zone behind UTC it rendered as "1999-12-31", which reads as a bug.
  QCOMPARE(FormatModTime("2000-01-01T00:00:00.000Z"), QString());
  QCOMPARE(FormatModTime("2000-01-01T00:00:00Z"), QString());
  // Anything older is equally unknown.
  QCOMPARE(FormatModTime("1970-01-01T00:00:00Z"), QString());
}

void TestFormatting::modTimeRealTimestamp() {
  // A real timestamp must survive. Compare against the same conversion rather
  // than a hardcoded string, so the test is not tied to the machine's zone.
  const QString input = "2026-08-16T09:41:32.477Z";
  const QDateTime expected =
      QDateTime::fromString(input, Qt::ISODateWithMs).toLocalTime();

  QVERIFY(expected.isValid());
  QCOMPARE(FormatModTime(input),
           expected.toString("yyyy-MM-dd HH:mm:ss"));
}

void TestFormatting::modTimeNanosecondPrecision() {
  // rclone and restic emit nanoseconds; Qt parses at most milliseconds, so
  // unhandled extra digits made the whole parse fail and fall through.
  QVERIFY(!FormatModTime("2026-08-16T00:14:33.617734546-07:00").isEmpty());
  QVERIFY(!FormatModTime("2026-08-16T00:46:22.307099-07:00").isEmpty());
}

void TestFormatting::modTimeIsFixedWidthAndSortable() {
  // The Modified column is sorted as a string, so the rendering has to be
  // fixed width and lexicographically ordered.
  const QString earlier = FormatModTime("2026-08-15T13:55:07Z");
  const QString later = FormatModTime("2026-08-16T02:15:05Z");

  QCOMPARE(earlier.length(), 19);
  QCOMPARE(later.length(), 19);
  QVERIFY(earlier < later);
}

void TestFormatting::modTimeGarbagePassesThrough() {
  // Unparseable input is returned unchanged rather than silently dropped.
  QCOMPARE(FormatModTime("not a timestamp"), QString("not a timestamp"));
  QCOMPARE(FormatModTime(QString()), QString());
}

void TestFormatting::versionComparison() {
  QCOMPARE(compareVersion("1.50", "1.50"), 0u);
  QCOMPARE(compareVersion("1.51", "1.50"), 1u);
  QCOMPARE(compareVersion("1.49", "1.50"), 2u);
  // Missing trailing components count as zero.
  QCOMPARE(compareVersion("1.50", "1.50.0"), 0u);
  QCOMPARE(compareVersion("1.50.1", "1.50"), 1u);
  QCOMPARE(compareVersion("2.0", "1.71"), 1u);
}

void TestFormatting::versionWithNonNumericComponent() {
  // std::stoi threw std::invalid_argument here, terminating the application
  // on startup against an rclone beta.
  QCOMPARE(compareVersion("1.65.0-beta", "1.65.0"), 0u);
  QCOMPARE(compareVersion("1.66.0-beta", "1.65.0"), 1u);
  QCOMPARE(compareVersion("1.71.0-DEV", "1.71.0"), 0u);

  // Only a *leading* integer is read, so a component starting with a letter
  // is zero: "v1.50" compares as "0.50". Call sites strip the "rclone v"
  // prefix before getting here, so this does not arise in practice -- pinned
  // because it is a deliberate contract, not an accident.
  QCOMPARE(compareVersion("v1.50", "0.50"), 0u);
  QCOMPARE(compareVersion("v1.50", "1.50"), 2u);

  // Garbage compares as zero rather than throwing, which is the whole point:
  // std::stoi used to terminate the application here.
  QCOMPARE(compareVersion("", ""), 0u);
  QCOMPARE(compareVersion("not.a.version", "0.0.0"), 0u);
}

void TestFormatting::resticRepoForRemote() {
  QCOMPARE(ResticRepoForRemote("storj", "tank0/kevin"),
           QString("rclone:storj:tank0/kevin"));
  // A leading slash on the path would produce "rclone:remote://path".
  QCOMPARE(ResticRepoForRemote("storj", "/tank0/kevin"),
           QString("rclone:storj:tank0/kevin"));
  QCOMPARE(ResticRepoForRemote("storj", QString()),
           QString("rclone:storj:"));
}

QTEST_MAIN(TestFormatting)
#include "test_formatting.moc"
