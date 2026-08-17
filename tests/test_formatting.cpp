#include "formatting.h"
#include "theme.h"
#include "restic.h"
#include "utils.h"
#include <QtTest>
#include <cmath>

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
  void shellQuoting();
  void commandLineQuoting();

  void themeAdaptsToPalette();
  void themeKeepsDisabledRowsReadable();
  void themeHasNoHardcodedColours();
  void themeDefinesSelectionColours();
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

void TestFormatting::shellQuoting() {
  // The password command is wrapped in "sh -c <quoted>" because restic execs
  // it without a shell, which would pass "$USER" through literally -- the
  // keychain lookup then fails with security exit 44, "item not found".
  QCOMPARE(ShellQuote("echo hi"), QString("'echo hi'"));

  // The variable must survive quoting intact, for sh to expand rather than
  // this application.
  QCOMPARE(ShellQuote(R"(security find-generic-password -a "$USER" -s k -w)"),
           QString(R"('security find-generic-password -a "$USER" -s k -w')"));

  // A single quote closes, escapes and reopens.
  QCOMPARE(ShellQuote("it's"), QString(R"('it'\''s')"));

  // A command cannot break out of the quoting to run something else.
  const QString hostile = "true'; rm -rf /tmp/x; echo '";
  const QString quoted = ShellQuote(hostile);
  QVERIFY(quoted.startsWith('\''));
  QVERIFY(quoted.endsWith('\''));
  QVERIFY(!quoted.contains(R"(; rm -rf /tmp/x; echo )") ||
          quoted.contains(R"('\'')"));

  QCOMPARE(ShellQuote(QString()), QString("''"));
}

void TestFormatting::commandLineQuoting() {
  // The Jobs tab's copy button produced this by joining argv on spaces, so any
  // path with a space in it -- "/Users/kevin/My Files" -- came out as a
  // command that could not be pasted back into a shell.
  const QStringList args = {"/usr/local/bin/rclone", "copy",
                            "/Users/kevin/My Files", "b2:backup"};

  const QString line = BuildCommandLine(args);

  // Ordinary arguments are left alone: a command full of quotes is harder to
  // read, and reading it is the whole point of copying it.
  QVERIFY(line.startsWith("/usr/local/bin/rclone copy "));
  QVERIFY(!line.contains("'/usr/local/bin/rclone'"));

#if !defined(Q_OS_WIN)
  QCOMPARE(line, QString("/usr/local/bin/rclone copy '/Users/kevin/My Files' "
                         "b2:backup"));

  // Characters the shell would act on, quoted rather than escaped in place.
  QCOMPARE(BuildCommandLine({"--exclude", "*.tmp"}),
           QString("--exclude '*.tmp'"));
  QCOMPARE(BuildCommandLine({"echo", "it's"}), QString(R"(echo 'it'\''s')"));
  QCOMPARE(BuildCommandLine({"--header", "X: $HOME"}),
           QString("--header 'X: $HOME'"));

  // An empty argument has to survive as an empty argument, not vanish.
  QCOMPARE(BuildCommandLine({"rclone", ""}), QString("rclone ''"));
#endif

  // Flags and remote:path arguments are safe as they stand.
  QCOMPARE(BuildCommandLine({"--config", "/home/kevin/.config/rclone.conf"}),
           QString("--config /home/kevin/.config/rclone.conf"));
  QCOMPARE(BuildCommandLine({"b2:bucket/sub-dir_1"}),
           QString("b2:bucket/sub-dir_1"));
}

void TestFormatting::themeAdaptsToPalette() {
  // Qt style sheets cannot reference palette roles, so the sheet is generated
  // from the palette. If it did not actually vary, every colour in it would be
  // fixed at author time and wrong in one of the two schemes.
  QPalette light;
  light.setColor(QPalette::WindowText, QColor(0, 0, 0));
  light.setColor(QPalette::Window, QColor(255, 255, 255));
  light.setColor(QPalette::Text, QColor(0, 0, 0));

  QPalette dark;
  dark.setColor(QPalette::WindowText, QColor(255, 255, 255));
  dark.setColor(QPalette::Window, QColor(30, 30, 30));
  dark.setColor(QPalette::Text, QColor(255, 255, 255));

  const QString lightSheet = ThemeStyleSheet(light);
  const QString darkSheet = ThemeStyleSheet(dark);

  QVERIFY(!lightSheet.isEmpty());
  QVERIFY(lightSheet != darkSheet);

  // The hairline derives from WindowText, so each scheme names its own.
  QVERIFY(lightSheet.contains("rgba(0,0,0"));
  QVERIFY(darkSheet.contains("rgba(255,255,255"));

  // Secondary text must differ too, or muted columns vanish in one scheme.
  QVERIFY(SecondaryTextColor(light) != SecondaryTextColor(dark));
}

void TestFormatting::themeHasNoHardcodedColours() {
  QPalette palette;
  palette.setColor(QPalette::WindowText, QColor(10, 20, 30));

  QString sheet = ThemeStyleSheet(palette);

  // Strip CSS comments first: the assertion is about declarations, and prose
  // explaining why the old header looked grey is not a hardcoded colour.
  static const QRegularExpression comments(R"(/\*.*?\*/)",
                                           QRegularExpression::DotMatchesEverythingOption);
  sheet.remove(comments);

  // Hex literals and named colours are the failure mode this guards against:
  // they survive a theme change and look wrong in the other scheme.
  QVERIFY(!sheet.contains("#"));
  QVERIFY(!sheet.contains("white"));
  QVERIFY(!sheet.contains("black"));
  QVERIFY(!sheet.contains("gray"));
  QVERIFY(!sheet.contains("grey"));

  // What it should contain: colours built from the palette at runtime.
  QVERIFY(sheet.contains("rgba(10,20,30"));
}

void TestFormatting::themeKeepsDisabledRowsReadable() {
  // The section headings in the remotes list are non-selectable rows, which
  // Qt expresses as disabled items -- and a disabled item is painted from the
  // palette's Disabled group, ignoring any foreground the item carries. On the
  // system dark palette that group put the headings at near-black on
  // near-black: "RCLONE REMOTES" and "RESTIC REPOSITORIES" were present, took
  // up space, and could not be read.
  auto contrastRatio = [](QColor foreground, const QColor &background) {
    // Composite first: the muted colour is the text colour at reduced alpha,
    // so the ratio depends on what it sits on.
    const qreal alpha = foreground.alphaF();
    foreground = QColor::fromRgbF(
        foreground.redF() * alpha + background.redF() * (1 - alpha),
        foreground.greenF() * alpha + background.greenF() * (1 - alpha),
        foreground.blueF() * alpha + background.blueF() * (1 - alpha));

    auto luminance = [](const QColor &c) {
      auto channel = [](qreal v) {
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
      };
      return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) +
             0.0722 * channel(c.blueF());
    };

    const qreal light = std::max(luminance(foreground), luminance(background));
    const qreal dark = std::min(luminance(foreground), luminance(background));
    return (light + 0.05) / (dark + 0.05);
  };

  // The real system dark palette this was reported against.
  QPalette darkPalette;
  darkPalette.setColor(QPalette::Base, QColor(0x17, 0x17, 0x17));
  darkPalette.setColor(QPalette::Text, QColor(0xff, 0xff, 0xff));

  QPalette lightPalette;
  lightPalette.setColor(QPalette::Base, QColor(0xff, 0xff, 0xff));
  lightPalette.setColor(QPalette::Text, QColor(0x00, 0x00, 0x00));

  for (const QPalette &palette : {darkPalette, lightPalette}) {
    const QString sheet = ThemeStyleSheet(palette);

    // The rule has to exist, or the palette's own Disabled group decides.
    QVERIFY(sheet.contains("::item:disabled"));

    const int at = sheet.indexOf("::item:disabled");
    QVERIFY(sheet.mid(at, 200).contains("color:"));

    // Muted is the intent -- these are chrome, not content -- but muted still
    // has to be legible against the row it sits on.
    const qreal ratio =
        contrastRatio(SecondaryTextColor(palette), palette.color(QPalette::Base));
    QVERIFY2(ratio >= 4.5,
             qPrintable(QString("contrast ratio %1 is too low").arg(ratio)));
  }
}

void TestFormatting::themeDefinesSelectionColours() {
  QPalette palette;
  palette.setColor(QPalette::Text, QColor(0, 0, 0));
  palette.setColor(QPalette::Active, QPalette::Highlight, QColor(0, 90, 200));
  palette.setColor(QPalette::Active, QPalette::HighlightedText,
                   QColor(255, 255, 255));

  const QString sheet = ThemeStyleSheet(palette);

  // Styling ::item at all moves Qt onto the style sheet drawing path, which
  // stops painting the selection background while the text still switches to
  // HighlightedText. That produced white text on a white row -- selected
  // entries were invisible. If ::item is styled, :selected must be too.
  QVERIFY(sheet.contains("::item"));
  QVERIFY(sheet.contains("::item:selected"));

  // Both halves: a background to sit on, and a colour to read against it.
  const int selectedAt = sheet.indexOf("::item:selected");
  const QString selectedRules = sheet.mid(selectedAt, 400);
  QVERIFY(selectedRules.contains("background:"));
  QVERIFY(selectedRules.contains("color:"));

  // The selection colours must come from the palette, not be invented.
  QVERIFY(sheet.contains("rgba(0,90,200"));
  QVERIFY(sheet.contains("rgba(255,255,255"));
}

QTEST_MAIN(TestFormatting)
#include "test_formatting.moc"
