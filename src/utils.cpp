#include "utils.h"

static QString gRclone;
static QString gRcloneConf;
static QString gRclonePassword;

namespace {

// Leading integer of a version component. The previous implementation used
// std::stoi, which throws on anything non-numeric -- an rclone version such as
// "1.65.0-beta" was enough to terminate the application.
unsigned int versionPart(const QString &part) {
  int end = 0;
  while (end < part.size() && part.at(end).isDigit()) {
    ++end;
  }
  if (end == 0) {
    return 0;
  }
  return part.left(end).toUInt();
}

} // namespace

unsigned int compareVersion(const QString &version1, const QString &version2) {
  const QStringList v1 = version1.split('.');
  const QStringList v2 = version2.split('.');
  const int max = std::max(v1.size(), v2.size());

  for (int i = 0; i < max; i++) {
    // Missing trailing components count as 0, so "1.50" == "1.50.0".
    const unsigned int n1 = i < v1.size() ? versionPart(v1[i]) : 0;
    const unsigned int n2 = i < v2.size() ? versionPart(v2[i]) : 0;
    if (n1 > n2) {
      return 1;
    }
    if (n1 < n2) {
      return 2;
    }
  }
  return 0;
}

#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
// $XDG_CONFIG_HOME, falling back to the spec-mandated ~/.config when it is
// unset or empty. Reading the bare environment variable put the settings file
// at "/rclone-browser/rclone-browser.ini" -- the filesystem root -- on every
// system that does not export it, which is most of them.
static QString XdgConfigHome() {
  const QString fromEnv = qEnvironmentVariable("XDG_CONFIG_HOME");
  if (!fromEnv.isEmpty()) {
    return fromEnv;
  }
  return QDir::homePath() + "/.config";
}
#endif

static QString GetIniFilename() {
#ifdef Q_OS_MACOS
  QFileInfo applicationPath = QFileInfo(qApp->applicationFilePath());
  //  qDebug() << QString(applicationPath.absolutePath());
  // on macOS excecutable file is located in
  // ./rclone-browser.app/Contents/MasOS/ to get actual bundle folder we have to
  // traverse three levels up
  QFileInfo MacOSPath{applicationPath.dir().path()};
  QFileInfo ContentsPath{MacOSPath.dir().path()};
  QFileInfo appBundlePath = QFileInfo(ContentsPath.dir().path());
  //  qDebug() << QString("utils.cpp appBundle.absolutePath: " +
  //                      appBundlePath.absolutePath());
  //  qDebug() << QString(
  //      "utils.cpp ini file:" +
  //      appBundlePath.dir().filePath(appBundlePath.baseName() + ".ini"));
  return appBundlePath.dir().filePath(appBundlePath.baseName() + ".ini");
#else
#ifdef Q_OS_WIN
  QFileInfo applicationPath = QFileInfo(qApp->applicationFilePath());
  return applicationPath.dir().filePath(applicationPath.baseName() + ".ini");
#else
  return XdgConfigHome() + "/rclone-browser/rclone-browser.ini";
#endif
#endif
}

bool IsPortableMode() {
  // AppImage portable mode: the runtime points $XDG_CONFIG_HOME at
  // "<appimage path>.config" when that directory exists next to the AppImage.
  // Stripping the suffix should therefore give back $APPIMAGE exactly.
  static const QString kAppImageConfigSuffix = QStringLiteral(".config");

  const QString xdgConfigHome = qEnvironmentVariable("XDG_CONFIG_HOME");
  const QString appImage = qEnvironmentVariable("APPIMAGE");

  if (!appImage.isEmpty() &&
      xdgConfigHome == appImage + kAppImageConfigSuffix) {
    return true;
  }

  // Everywhere else: an .ini file sitting next to the executable.
  return QFileInfo::exists(GetIniFilename());
}

std::unique_ptr<QSettings> GetSettings() {
  if (IsPortableMode()) {
    return std::unique_ptr<QSettings>(
        new QSettings(GetIniFilename(), QSettings::IniFormat));
  }
  return std::unique_ptr<QSettings>(new QSettings);
}

void ReadSettings(QSettings *settings, QObject *widget) {
  QString name = widget->objectName();
  if (!name.isEmpty() && settings->contains(name)) {
    if (QRadioButton *obj = qobject_cast<QRadioButton *>(widget)) {
      obj->setChecked(settings->value(name).toBool());
      return;
    }
    if (QCheckBox *obj = qobject_cast<QCheckBox *>(widget)) {
      obj->setChecked(settings->value(name).toBool());
      return;
    }
    if (QComboBox *obj = qobject_cast<QComboBox *>(widget)) {
      obj->setCurrentIndex(settings->value(name).toInt());
      return;
    }
    if (QSpinBox *obj = qobject_cast<QSpinBox *>(widget)) {
      obj->setValue(settings->value(name).toInt());
      return;
    }
    if (QLineEdit *obj = qobject_cast<QLineEdit *>(widget)) {
      obj->setText(settings->value(name).toString());
      return;
    }
    if (QPlainTextEdit *obj = qobject_cast<QPlainTextEdit *>(widget)) {
      int count = settings->beginReadArray(name);
      QStringList lines;
      lines.reserve(count);
      for (int i = 0; i < count; i++) {
        settings->setArrayIndex(i);
        lines.append(settings->value("value").toString());
      }
      settings->endArray();

      obj->setPlainText(lines.join('\n'));
      return;
    }
  }

  for (auto child : widget->children()) {
    ReadSettings(settings, child);
  }
}

void WriteSettings(QSettings *settings, QObject *widget) {
  QString name = widget->objectName();
  if (QCheckBox *obj = qobject_cast<QCheckBox *>(widget)) {
    settings->setValue(name, obj->isChecked());
    return;
  }
  if (QComboBox *obj = qobject_cast<QComboBox *>(widget)) {
    settings->setValue(name, obj->currentIndex());
    return;
  }
  if (QSpinBox *obj = qobject_cast<QSpinBox *>(widget)) {
    settings->setValue(name, obj->value());
    return;
  }
  if (QLineEdit *obj = qobject_cast<QLineEdit *>(widget)) {
    if (obj->text().isEmpty()) {
      settings->remove(name);
    } else {
      settings->setValue(name, obj->text());
    }
    return;
  }
  if (QPlainTextEdit *obj = qobject_cast<QPlainTextEdit *>(widget)) {
    QString text = obj->toPlainText().trimmed();
    if (!text.isEmpty()) {
      QStringList lines = text.split('\n');
      settings->beginWriteArray(name, lines.size());
      for (int i = 0; i < lines.count(); i++) {
        settings->setArrayIndex(i);
        settings->setValue("value", lines[i]);
      }
      settings->endArray();
    }
    return;
  }

  for (auto child : widget->children()) {
    WriteSettings(settings, child);
  }
}

// Directory that relative paths are resolved against in portable mode: the
// folder holding the application. GetRclone() and GetRcloneConf() had identical
// copies of this platform switch.
static QDir PortableBaseDir() {
#ifdef Q_OS_MACOS
  // The executable lives in ./rclone-browser.app/Contents/MacOS/, so the
  // bundle's containing folder is three levels up.
  return QDir(qApp->applicationDirPath() + "/../../..");
#else
#ifdef Q_OS_WIN
  return QDir(qApp->applicationDirPath());
#else
  // On Linux portable mode is the AppImage case, where $XDG_CONFIG_HOME is
  // "<appimage>.config" and its parent is the folder holding the AppImage.
  return QDir(XdgConfigHome() + "/..");
#endif
#endif
}

QStringList GetRcloneConf() {
  if (gRcloneConf.isEmpty()) {
    return QStringList();
  }

  QString conf = gRcloneConf;
  if (IsPortableMode() && QFileInfo(conf).isRelative()) {
    conf = PortableBaseDir().filePath(conf);
  }
  return QStringList() << "--config" << conf;
}

void SetRcloneConf(const QString &rcloneConf) { gRcloneConf = rcloneConf; }

QString GetRclone() {
  QString rclone = gRclone;
  if (IsPortableMode() && QFileInfo(rclone).isRelative()) {
    rclone = PortableBaseDir().filePath(rclone);
  }
  return rclone;
}

void SetRclone(const QString &rclone) { gRclone = rclone.trimmed(); }

void UseRclonePassword(QProcess *process) {
  if (gRclonePassword.isEmpty()) {
    return;
  }

  // Start from whatever the caller already configured rather than always from
  // systemEnvironment(), so this composes with callers that set their own
  // variables first instead of silently discarding them.
  QProcessEnvironment env = process->processEnvironment();
  if (env.isEmpty()) {
    env = QProcessEnvironment::systemEnvironment();
  }

  env.insert("RCLONE_CONFIG_PASS", gRclonePassword);
  process->setProcessEnvironment(env);
}

void SetRclonePassword(const QString &rclonePassword) {
  gRclonePassword = rclonePassword;
}

void StyleTreeView(QTreeView *tree) {
  auto settings = GetSettings();

  // Off by default: Qt stripes the entire viewport, not just the rows that
  // exist, so a two-row listing renders thirty rows of empty zebra beneath it.
  tree->setAlternatingRowColors(
      settings->value("Settings/rowColors", false).toBool());

  // Uniform row heights let the view skip per-row size hints, which matters on
  // large listings.
  tree->setUniformRowHeights(true);
  tree->setAllColumnsShowFocus(true);
  tree->setExpandsOnDoubleClick(true);
  tree->setAnimated(true);

  // A frame around a view that already fills its tab is redundant chrome.
  tree->setFrameShape(QFrame::NoFrame);

  QHeaderView *header = tree->header();
  header->setSectionsMovable(false);
  header->setHighlightSections(false);
  header->setStretchLastSection(false);
}

void StyleToolBar(QToolBar *toolBar) {
  toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  toolBar->setMovable(false);
  toolBar->setFloatable(false);
  toolBar->setIconSize(QSize(16, 16));
}

QStringList GetDefaultRcloneOptionsList() {
  auto settings = GetSettings();
  QString defaultRcloneOptions =
      settings->value("Settings/defaultRcloneOptions").toString();
  QStringList defaultRcloneOptionsList;
  if (!defaultRcloneOptions.isEmpty()) {
    // Blank entries would reach rclone as empty arguments; a settings field
    // with a stray double space is enough to produce them.
    for (const QString &arg : defaultRcloneOptions.split(' ')) {
      if (!arg.trimmed().isEmpty()) {
        defaultRcloneOptionsList << arg;
      }
    }
  }
  return defaultRcloneOptionsList;
}

QString BuildCommandLine(const QStringList &args) {
  // Joining on spaces was enough to break every copied command that touched a
  // path with a space in it -- which, on macOS and Windows, is most of them.
  //
  // The quoting is the destination shell's, not rclone's: this string exists
  // to be pasted into a terminal. Anything outside the safe set is quoted
  // whole rather than escaped character by character, which keeps the result
  // readable and correct for the awkward cases (spaces, quotes, globs, $).
  static const QRegularExpression safe(R"(^[A-Za-z0-9_@%+=:,./-]+$)");

  QStringList quoted;
  quoted.reserve(args.size());

  for (const QString &arg : args) {
    if (!arg.isEmpty() && safe.match(arg).hasMatch()) {
      quoted << arg;
      continue;
    }

#if defined(Q_OS_WIN)
    // cmd.exe and PowerShell both take double quotes; a literal double quote
    // inside is escaped with a backslash, which both accept for a program
    // argument.
    QString escaped = arg;
    escaped.replace("\"", "\\\"");
    quoted << "\"" + escaped + "\"";
#else
    // Single quotes protect everything except a single quote itself, which is
    // closed, escaped and reopened: it's  ->  'it'\''s'
    QString escaped = arg;
    escaped.replace("'", R"('\'')");
    quoted << "'" + escaped + "'";
#endif
  }

  return quoted.join(' ');
}

QStringList GetShowHidden() {
  auto settings = GetSettings();
  bool showHidden = settings->value("Settings/showHidden", true).toBool();
  QStringList showHiddenOption;
  if (!showHidden) {
    showHiddenOption << "--exclude"
                     << ".*/**"
                     << "--exclude"
                     << ".*";
  }
  return showHiddenOption;
}
