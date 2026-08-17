#include "parsing.h"
#include "remote_path.h"
#include "restic_model.h"
#include <QtTest>

// The fixtures below are real output, captured from rclone 1.71 against a B2
// remote and from restic 0.18.1 against a local repository -- not invented
// shapes that happen to match the parser.
class TestParsing : public QObject {
  Q_OBJECT

private slots:
  void rcloneListing();
  void rcloneDirectorySizeIsNotNegative();
  void rcloneEmptyListing();
  void rcloneMalformedJson();
  void rcloneNotAnArray();
  void rcloneAwkwardFilenames();
  void rcloneListingCarriesPath();

  void joinsRemotePaths();
  void remotePathNames();
  void childPathComesFromTheListing();

  void statsTotals();
  void statsTotalsBeforeRateIsKnown();
  void statsTotalsSiUnits();
  void statsTotalsBeforeRclone143();
  void statsErrorsWithRetryHint();
  void statsChecksWithListedSuffix();
  void statsFileCount();
  void statsElapsed();
  void statsFileProgress();
  void statsFileProgressBeforeRateIsKnown();
  void statsLogLinesAreNotStats();

  void resticSnapshotsNewestFirst();
  void resticSnapshotsMalformed();

  void resticNodesSkipSnapshotRecord();
  void resticTreeNesting();
  void resticTreeOutOfOrderNodes();
  void resticTreeSortsFoldersFirst();
};

void TestParsing::rcloneListing() {
  const QByteArray json = R"([
{"Path":"db.sqlite3.bin","Name":"db.sqlite3.bin","Size":1048864,"MimeType":"application/octet-stream","ModTime":"2026-08-15T06:15:00.991Z","IsDir":false},
{"Path":"attachments","Name":"attachments","Size":-1,"MimeType":"inode/directory","ModTime":"2000-01-01T00:00:00.000Z","IsDir":true}
])";

  QVector<RcloneEntry> entries;
  QString error;
  QVERIFY(ParseRcloneListing(json, &entries, &error));
  QCOMPARE(entries.size(), 2);

  QCOMPARE(entries[0].name, QString("db.sqlite3.bin"));
  QVERIFY(!entries[0].isFolder);
  QCOMPARE(entries[0].size, quint64(1048864));
  QVERIFY(!entries[0].modified.isEmpty());

  QCOMPARE(entries[1].name, QString("attachments"));
  QVERIFY(entries[1].isFolder);
  // The sentinel timestamp on a synthetic directory renders as nothing.
  QCOMPARE(entries[1].modified, QString());
}

void TestParsing::rcloneDirectorySizeIsNotNegative() {
  // Remote directories report Size -1. Assigned to an unsigned type that
  // becomes 18446744073709551615, which would render as "16777215 P".
  const QByteArray json =
      R"([{"Name":"truenas","Size":-1,"ModTime":"2000-01-01T00:00:00.000Z","IsDir":true}])";

  QVector<RcloneEntry> entries;
  QVERIFY(ParseRcloneListing(json, &entries, nullptr));
  QCOMPARE(entries.size(), 1);
  QCOMPARE(entries[0].size, quint64(0));
}

void TestParsing::rcloneEmptyListing() {
  QVector<RcloneEntry> entries;
  QString error;
  // An empty directory is valid, not an error -- treating it as one would put
  // an error row on every empty folder.
  QVERIFY(ParseRcloneListing("[]", &entries, &error));
  QVERIFY(entries.isEmpty());
  QVERIFY(error.isEmpty());
}

void TestParsing::rcloneMalformedJson() {
  QVector<RcloneEntry> entries;
  QString error;
  QVERIFY(!ParseRcloneListing("[{\"Name\": ", &entries, &error));
  QVERIFY(!error.isEmpty());
}

void TestParsing::rcloneNotAnArray() {
  QVector<RcloneEntry> entries;
  QString error;
  // rclone prints an error object instead of an array in some failure modes.
  QVERIFY(!ParseRcloneListing(R"({"error":"directory not found"})", &entries,
                              &error));
  QVERIFY(!error.isEmpty());
}

void TestParsing::rcloneAwkwardFilenames() {
  // The regex-scraping this replaced could not represent these at all: a
  // newline ended the record and a quote confused nothing but the eye.
  const QByteArray json = R"([
{"Name":"quote\"name.txt","Size":1,"ModTime":"2026-08-15T06:15:00Z","IsDir":false},
{"Name":"line\nbreak.txt","Size":2,"ModTime":"2026-08-15T06:15:00Z","IsDir":false},
{"Name":"  leading and trailing  ","Size":3,"ModTime":"2026-08-15T06:15:00Z","IsDir":false}
])";

  QVector<RcloneEntry> entries;
  QVERIFY(ParseRcloneListing(json, &entries, nullptr));
  QCOMPARE(entries.size(), 3);
  QCOMPARE(entries[0].name, QString("quote\"name.txt"));
  QCOMPARE(entries[1].name, QString("line\nbreak.txt"));
  QCOMPARE(entries[2].name, QString("  leading and trailing  "));
}

void TestParsing::rcloneListingCarriesPath() {
  // lsjson reports a Path as well as a Name. They agree on ordinary backends,
  // and the parser keeps both so the model can build a child path from what
  // rclone said rather than by guessing.
  const QByteArray json = R"([
{"Path":"holiday/beach.jpg","Name":"beach.jpg","Size":12,"ModTime":"2026-08-15T06:15:00Z","IsDir":false},
{"Name":"no-path-field.txt","Size":3,"ModTime":"2026-08-15T06:15:00Z","IsDir":false}
])";

  QVector<RcloneEntry> entries;
  QVERIFY(ParseRcloneListing(json, &entries, nullptr));
  QCOMPARE(entries.size(), 2);

  QCOMPARE(entries[0].name, QString("beach.jpg"));
  QCOMPARE(entries[0].path, QString("holiday/beach.jpg"));

  // A backend that omits Path falls back to the name.
  QCOMPARE(entries[1].path, QString("no-path-field.txt"));
}

void TestParsing::joinsRemotePaths() {
  QCOMPARE(JoinRemotePath("photos", "beach.jpg"),
           QString("photos/beach.jpg"));
  QCOMPARE(JoinRemotePath("photos/2026", "beach.jpg"),
           QString("photos/2026/beach.jpg"));

  // The root of a remote is the empty path, and this is the whole reason the
  // helper exists: QDir turns "" into ".", so the root of every remote listed
  // as "./name" and the destination fields showed "remote:." to the user.
  QCOMPARE(JoinRemotePath("", "beach.jpg"), QString("beach.jpg"));
  QCOMPARE(JoinRemotePath(".", "beach.jpg"), QString("beach.jpg"));

  // No doubled separator when the parent already ends in one.
  QCOMPARE(JoinRemotePath("photos/", "beach.jpg"),
           QString("photos/beach.jpg"));

  // Names are not interpreted. A remote may hold an object literally called
  // "..", and on Windows QDir would have treated the backslash as a separator.
  QCOMPARE(JoinRemotePath("photos", ".."), QString("photos/.."));
  QCOMPARE(JoinRemotePath("photos", "a\\b"), QString("photos/a\\b"));
  QCOMPARE(JoinRemotePath("photos", "a b"), QString("photos/a b"));

  QCOMPARE(JoinRemotePath("photos", QString()), QString("photos"));
}

void TestParsing::remotePathNames() {
  QCOMPARE(RemotePathName("photos/2026/beach.jpg"), QString("beach.jpg"));
  QCOMPARE(RemotePathName("photos"), QString("photos"));
  QCOMPARE(RemotePathName("photos/"), QString("photos"));
  // The root has no name of its own.
  QCOMPARE(RemotePathName(""), QString());
}

void TestParsing::childPathComesFromTheListing() {
  // The ordinary case: Path is the name again, so the child sits under its
  // parent.
  QCOMPARE(ChildRemotePath("holiday", "beach.jpg", "beach.jpg"),
           QString("holiday/beach.jpg"));
  QCOMPARE(ChildRemotePath("", "beach.jpg", "beach.jpg"),
           QString("beach.jpg"));

  // Where a backend reports a Path that already includes the parent -- Google
  // Photos album listings do -- it must not be joined on a second time.
  QCOMPARE(ChildRemotePath("album", "album/beach.jpg", "beach.jpg"),
           QString("album/beach.jpg"));

  // No Path at all: fall back to the name.
  QCOMPARE(ChildRemotePath("holiday", QString(), "beach.jpg"),
           QString("holiday/beach.jpg"));
}

// The stats fixtures below are byte-for-byte lines from rclone 1.71, tab and
// all, captured from a bandwidth-limited local copy and from a copy of an
// unreadable file. The exception is the 1.43-1.55 and pre-1.43 forms, which no
// longer reachable rclone emits; those are the shapes the previous patterns
// were written against, kept so that the parser stays backward compatible.
//
// This is the regression the whole file exists for: rclone 1.56 moved from
// "1.234G / 5.678 GBytes" to "3.027 MiB / 120 MiB", the old pattern matched
// neither, and Size, Total size, Bandwidth and ETA were blank for every
// transfer.
void TestParsing::statsTotals() {
  const RcloneStats stats = ParseRcloneStats(
      "Transferred:   \t    3.027 MiB / 120 MiB, 3%, 3.027 MiB/s, ETA 38s");

  QCOMPARE(stats.kind, RcloneStats::Totals);
  QCOMPARE(stats.size, QString("3.027 MiB"));
  QCOMPARE(stats.totalSize, QString("120 MiB"));
  QCOMPARE(stats.percent, QString("3%"));
  QCOMPARE(stats.bandwidth, QString("3.027 MiB/s"));
  QCOMPARE(stats.eta, QString("38s"));
}

void TestParsing::statsTotalsBeforeRateIsKnown() {
  // rclone prints "-" for the percentage and the ETA until it has both a total
  // and a rate. A pattern demanding digits there blanks the panel for the
  // first seconds of every job, and for the whole of a stalled one.
  const RcloneStats stats = ParseRcloneStats(
      "Transferred:   \t          0 B / 0 B, -, 0 B/s, ETA -");

  QCOMPARE(stats.kind, RcloneStats::Totals);
  QCOMPARE(stats.size, QString("0 B"));
  QCOMPARE(stats.totalSize, QString("0 B"));
  QCOMPARE(stats.percent, QString("-"));
  QCOMPARE(stats.bandwidth, QString("0 B/s"));
  QCOMPARE(stats.eta, QString("-"));
}

void TestParsing::statsTotalsSiUnits() {
  // rclone 1.43 to 1.55: SI units, number and unit run together on the left.
  const RcloneStats stats = ParseRcloneStats(
      "Transferred:   1.234G / 5.678 GBytes, 22%, 1.234 MBytes/s, ETA 1h2m3s");

  QCOMPARE(stats.kind, RcloneStats::Totals);
  QCOMPARE(stats.size, QString("1.234G"));
  QCOMPARE(stats.totalSize, QString("5.678 GBytes"));
  QCOMPARE(stats.percent, QString("22%"));
  QCOMPARE(stats.bandwidth, QString("1.234 MBytes/s"));
  QCOMPARE(stats.eta, QString("1h2m3s"));
}

void TestParsing::statsTotalsBeforeRclone143() {
  // Until 1.42 there was no total and no ETA, only a running count and a rate.
  const RcloneStats stats =
      ParseRcloneStats("Transferred:   100 Bytes (50 Bytes/sec)");

  QCOMPARE(stats.kind, RcloneStats::Totals);
  QCOMPARE(stats.size, QString("100 Bytes"));
  QCOMPARE(stats.bandwidth, QString("50 Bytes/sec"));
  QVERIFY(stats.totalSize.isEmpty());
  QVERIFY(stats.eta.isEmpty());
}

void TestParsing::statsErrorsWithRetryHint() {
  // The suffix is why the error count never appeared: the pattern was anchored
  // to the end of the line, so the only lines it could match were the ones
  // reporting no errors at all.
  const RcloneStats stats =
      ParseRcloneStats("Errors:                 1 (retrying may help)");

  QCOMPARE(stats.kind, RcloneStats::Errors);
  QCOMPARE(stats.text, QString("1"));

  const RcloneStats plain = ParseRcloneStats("Errors:                 0");
  QCOMPARE(plain.kind, RcloneStats::Errors);
  QCOMPARE(plain.text, QString("0"));
}

void TestParsing::statsChecksWithListedSuffix() {
  // ", Listed N" arrived in rclone 1.60 and broke this line the same way.
  const RcloneStats stats =
      ParseRcloneStats("Checks:                 0 / 0, -, Listed 1");

  QCOMPARE(stats.kind, RcloneStats::Checks);
  QCOMPARE(stats.text, QString("0 / 0, -"));

  const RcloneStats older = ParseRcloneStats("Checks:                 1 / 2, 50%");
  QCOMPARE(older.kind, RcloneStats::Checks);
  QCOMPARE(older.text, QString("1 / 2, 50%"));
}

void TestParsing::statsFileCount() {
  // Same prefix as the byte totals, so order of matching decides which wins.
  const RcloneStats stats =
      ParseRcloneStats("Transferred:            0 / 1, 0%");

  QCOMPARE(stats.kind, RcloneStats::FileCount);
  QCOMPARE(stats.text, QString("0 / 1, 0%"));
}

void TestParsing::statsElapsed() {
  const RcloneStats stats = ParseRcloneStats("Elapsed time:         1.9s");

  QCOMPARE(stats.kind, RcloneStats::Elapsed);
  QCOMPARE(stats.text, QString("1.9s"));
}

void TestParsing::statsFileProgress() {
  const RcloneStats stats = ParseRcloneStats(
      " *                                       big.bin:  3% /120Mi, 2.027Mi/s, 57s");

  QCOMPARE(stats.kind, RcloneStats::FileProgress);
  QCOMPARE(stats.name, QString("big.bin"));
  QCOMPARE(stats.filePercent, 3);
  QCOMPARE(stats.fileDetail, QString("3% /120Mi, 2.027Mi/s, 57s"));
}

void TestParsing::statsFileProgressBeforeRateIsKnown() {
  // "0/s" and "-" in the first second of a transfer. The old \w+ ETA capture
  // rejected the dash, so a file's bar did not appear until rclone had a rate.
  const RcloneStats stats = ParseRcloneStats(
      " *                                       big.bin:  1% /120Mi, 0/s, -");

  QCOMPARE(stats.kind, RcloneStats::FileProgress);
  QCOMPARE(stats.name, QString("big.bin"));
  QCOMPARE(stats.filePercent, 1);
}

void TestParsing::statsLogLinesAreNotStats() {
  // Most of rclone's output is log lines, and none of them should light up a
  // field. The last one is the trap: it starts like the stats block.
  const QStringList lines = {
      "",
      "2026/08/16 20:12:11 INFO  : big.bin: Copied (server-side copy)",
      "2026/08/16 20:12:11 INFO  : Starting bandwidth limiter at 2Mi Byte/s",
      "Server Side Copies:     1 @ 120 MiB",
      "Transferring:",
  };

  for (const QString &line : lines) {
    QCOMPARE(ParseRcloneStats(line).kind, RcloneStats::Unknown);
  }
}

void TestParsing::resticSnapshotsNewestFirst() {
  const QByteArray json = R"([
{"time":"2026-08-15T13:55:07.1Z","paths":["/mnt/tank0"],"hostname":"nas","id":"873e958f0000","short_id":"873e958f"},
{"time":"2026-08-16T02:15:05.2Z","paths":["/mnt/tank0"],"hostname":"nas","id":"3e762ba70000","short_id":"3e762ba7"}
])";

  QVector<ResticSnapshot> snapshots;
  QVERIFY(ParseResticSnapshots(json, &snapshots, nullptr));
  QCOMPARE(snapshots.size(), 2);

  // restic emits oldest first; the browser shows newest at the top.
  QCOMPARE(snapshots[0].shortId, QString("3e762ba7"));
  QCOMPARE(snapshots[1].shortId, QString("873e958f"));
  QCOMPARE(snapshots[0].hostname, QString("nas"));
  QCOMPARE(snapshots[0].paths, QStringList{"/mnt/tank0"});
}

void TestParsing::resticSnapshotsMalformed() {
  QVector<ResticSnapshot> snapshots;
  QString error;
  QVERIFY(!ParseResticSnapshots("not json", &snapshots, &error));
  QVERIFY(!error.isEmpty());
}

void TestParsing::resticNodesSkipSnapshotRecord() {
  // "restic ls --json" is JSON Lines: the first record describes the snapshot
  // and must not become a file row.
  const QByteArray lines = R"({"time":"2026-08-16T00:46:22Z","id":"abc","struct_type":"snapshot"}
{"name":"data","type":"dir","path":"/data","mtime":"2026-08-16T00:46:19Z","struct_type":"node"}
{"name":"a.txt","type":"file","path":"/data/a.txt","size":11,"mtime":"2026-08-16T00:46:19Z","struct_type":"node"}
)";

  QVector<ResticNode> nodes;
  QVERIFY(ParseResticNodes(lines, &nodes, nullptr));
  QCOMPARE(nodes.size(), 2);
  QCOMPARE(nodes[0].name, QString("data"));
  QVERIFY(nodes[0].isDir);
  QCOMPARE(nodes[1].name, QString("a.txt"));
  QVERIFY(!nodes[1].isDir);
  QCOMPARE(nodes[1].size, quint64(11));
}

void TestParsing::resticTreeNesting() {
  // restic reports absolute paths flat; the tree has to be rebuilt from them.
  QVector<ResticNode> nodes;
  nodes.append({"private", "/private", true, 0, {}});
  nodes.append({"data", "/private/data", true, 0, {}});
  nodes.append({"a.txt", "/private/data/a.txt", false, 11, {}});
  nodes.append({"sub", "/private/data/sub", true, 0, {}});
  nodes.append({"b.bin", "/private/data/sub/b.bin", false, 1, {}});

  ResticItem *root = BuildResticTree(nodes);

  QCOMPARE(root->children.size(), 1);
  ResticItem *priv = root->children[0];
  QCOMPARE(priv->name, QString("private"));
  QCOMPARE(priv->children.size(), 1);

  ResticItem *data = priv->children[0];
  QCOMPARE(data->name, QString("data"));
  QCOMPARE(data->children.size(), 2);

  // Folders sort before files.
  QCOMPARE(data->children[0]->name, QString("sub"));
  QCOMPARE(data->children[1]->name, QString("a.txt"));
  QCOMPARE(data->children[1]->size, quint64(11));

  // Restore uses the absolute path, so it must survive the rebuild.
  QCOMPARE(data->children[1]->path, QString("/private/data/a.txt"));

  delete root;
}

void TestParsing::resticTreeOutOfOrderNodes() {
  // A file arriving before its parent directory must still land in the right
  // place, with the intermediate directories created on demand.
  QVector<ResticNode> nodes;
  nodes.append({"deep.txt", "/a/b/c/deep.txt", false, 7, {}});
  nodes.append({"a", "/a", true, 0, {}});

  ResticItem *root = BuildResticTree(nodes);

  QCOMPARE(root->children.size(), 1);
  ResticItem *a = root->children[0];
  QCOMPARE(a->name, QString("a"));
  QCOMPARE(a->children.size(), 1);
  QCOMPARE(a->children[0]->name, QString("b"));
  QVERIFY(a->children[0]->isFolder);

  ResticItem *c = a->children[0]->children[0];
  QCOMPARE(c->name, QString("c"));
  QCOMPARE(c->children.size(), 1);
  QCOMPARE(c->children[0]->name, QString("deep.txt"));

  delete root;
}

void TestParsing::resticTreeSortsFoldersFirst() {
  QVector<ResticNode> nodes;
  nodes.append({"zebra.txt", "/zebra.txt", false, 1, {}});
  nodes.append({"Alpha", "/Alpha", true, 0, {}});
  nodes.append({"apple.txt", "/apple.txt", false, 1, {}});
  nodes.append({"beta", "/beta", true, 0, {}});

  ResticItem *root = BuildResticTree(nodes);

  QCOMPARE(root->children.size(), 4);
  // Folders first, then case-insensitive by name.
  QCOMPARE(root->children[0]->name, QString("Alpha"));
  QCOMPARE(root->children[1]->name, QString("beta"));
  QCOMPARE(root->children[2]->name, QString("apple.txt"));
  QCOMPARE(root->children[3]->name, QString("zebra.txt"));

  delete root;
}

QTEST_MAIN(TestParsing)
#include "test_parsing.moc"
