#include "parsing.h"
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
