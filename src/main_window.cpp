#include "main_window.h"
#include "job_options.h"
#include "job_widget.h"
#include "list_of_job_options.h"
#include "mount_widget.h"
#include "preferences_dialog.h"
#include "remote_widget.h"
#include "restic_repo_dialog.h"
#include "restic_widget.h"
#include "stream_widget.h"
#include "transfer_dialog.h"
#include "utils.h"
#ifdef Q_OS_MACOS
#include "osx_helper.h"
#endif

namespace {

// The hand-built dark palette behind the "dark mode" preference, for the
// platforms and versions where Qt does not follow the desktop's own scheme.
// Two identical copies of this used to sit inline in the constructor, one per
// platform branch, which is how they came to disagree with each other.
void ApplyLegacyDarkPalette() {
  qApp->setStyle(QStyleFactory::create("Fusion"));

  QPalette dark;
  dark.setColor(QPalette::Window, QColor(53, 53, 53));
  dark.setColor(QPalette::WindowText, Qt::white);
  dark.setColor(QPalette::Base, QColor(25, 25, 25));
  dark.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
  dark.setColor(QPalette::Text, Qt::white);
  dark.setColor(QPalette::Button, QColor(53, 53, 53));
  dark.setColor(QPalette::ButtonText, Qt::white);
  dark.setColor(QPalette::BrightText, Qt::red);
  dark.setColor(QPalette::Link, QColor(42, 130, 218));
  dark.setColor(QPalette::Highlight, QColor(42, 130, 218));
  dark.setColor(QPalette::HighlightedText, Qt::black);

  // Both tool tip roles were set to white, which is white text on a white
  // tool tip.
  dark.setColor(QPalette::ToolTipBase, QColor(53, 53, 53));
  dark.setColor(QPalette::ToolTipText, Qt::white);

  // The Disabled group was left at the default light palette, so everything
  // drawn from it -- including the non-selectable section headings in the
  // remotes list -- came out near-black on near-black.
  dark.setColor(QPalette::Disabled, QPalette::WindowText, QColor(130, 130, 130));
  dark.setColor(QPalette::Disabled, QPalette::Text, QColor(130, 130, 130));
  dark.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(130, 130, 130));

  qApp->setPalette(dark);

  // Deliberately no setStyleSheet: it used to install a tool tip rule, which
  // replaced the whole generated theme sheet installed at startup. Turning
  // this preference on threw away every other style rule the application has.
  // The tool tip roles above cover what it was for.
}

} // namespace

MainWindow::MainWindow() {
  ui.setupUi(this);

  if (IsPortableMode()) {
    this->setWindowTitle("rclone-browser - portable mode");
  } else {
    this->setWindowTitle("rclone-browser");
  }

  // Qt::AA_DisableWindowContextHelpButton used to be set here, to suppress the
  // "?" button in dialog title bars on Windows. The attribute was deprecated in
  // Qt 5.15 and removed in Qt 6, which no longer adds that button at all, so
  // there is nothing left to disable -- and referring to it stopped the
  // Windows build compiling.

#if !defined(Q_OS_MACOS)
  // Windows and Linux: an opt-in dark mode, from before Qt followed the
  // desktop's own scheme there.
  if (GetSettings()->value("Settings/darkMode").toBool()) {
    ApplyLegacyDarkPalette();
  }
#else
  // macOS 10.13 and older: same preference, for versions that predate the
  // system-wide dark appearance. Newer macOS follows the system, and this
  // whole path is skipped.
  const QString macVersion = QSysInfo::productVersion();
  if (macVersion == "10.9" || macVersion == "10.10" || macVersion == "10.11" ||
      macVersion == "10.12" || macVersion == "10.13") {
    if (GetSettings()->value("Settings/darkMode").toBool()) {
      ApplyLegacyDarkPalette();
    }
  }
#endif

  mSystemTray.setIcon(qApp->windowIcon());
  {
    auto settings = GetSettings();
    if (settings->contains("MainWindow/geometry")) {
      restoreGeometry(settings->value("MainWindow/geometry").toByteArray());
    }
    SetRclone(settings->value("Settings/rclone").toString());
    SetRcloneConf(settings->value("Settings/rcloneConf").toString());

    mAlwaysShowInTray =
        settings->value("Settings/alwaysShowInTray", false).toBool();
    mCloseToTray = settings->value("Settings/closeToTray", false).toBool();
    mNotifyFinishedTransfers =
        settings->value("Settings/notifyFinishedTransfers", true).toBool();

    mSystemTray.setVisible(mAlwaysShowInTray);

    // during first run the lastUsed keys might not exist
    if (!(settings->contains("Settings/lastUsedSourceFolder"))) {
      // if lastUsedSourceFolder does not exist create new empty key
      settings->setValue("Settings/lastUsedSourceFolder", "");
    };
    if (!(settings->contains("Settings/lastUsedDestFolder"))) {
      // if lastUsedDestFolder does not exist create new empty key
      settings->setValue("Settings/lastUsedDestFolder", "");
    };
    if (!(settings->contains("Settings/defaultDownloadOptions"))) {
      // if defaultDownloadOptions does not exist create new empty key
      settings->setValue("Settings/defaultDownloadOptions", "");
    };
#ifdef Q_OS_MACOS
    // for macOS by default exclude .DS_Store files from uploads
    if (!(settings->contains("Settings/defaultUploadOptions"))) {
      // if defaultDownloadOptions does not exist create new empty key
      settings->setValue("Settings/defaultUploadOptions",
                         "--exclude .DS_Store");
    };
#else
    if (!(settings->contains("Settings/defaultUploadOptions"))) {
      // if defaultDownloadOptions does not exist create new empty key
      settings->setValue("Settings/defaultUploadOptions", "");
    };
#endif
    if (!(settings->contains("Settings/defaultRcloneOptions"))) {
      // if defaultRcloneOptions does not exist create new empty key
      settings->setValue("Settings/defaultRcloneOptions", "--fast-list");
    };
  }

  QObject::connect(ui.preferences, &QAction::triggered, this, [=]() {
    PreferencesDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
      auto settings = GetSettings();
      settings->setValue("Settings/rclone", dialog.getRclone().trimmed());
      settings->setValue("Settings/rcloneConf",
                         dialog.getRcloneConf().trimmed());
      SetRestic(dialog.getRestic());
      settings->setValue("Settings/stream", dialog.getStream());
      settings->setValue("Settings/mount", dialog.getMount());
      settings->setValue("Settings/defaultDownloadDir",
                         dialog.getDefaultDownloadDir().trimmed());
      settings->setValue("Settings/defaultUploadDir",
                         dialog.getDefaultUploadDir().trimmed());
      settings->setValue("Settings/defaultDownloadOptions",
                         dialog.getDefaultDownloadOptions().trimmed());
      settings->setValue("Settings/defaultUploadOptions",
                         dialog.getDefaultUploadOptions().trimmed());
      settings->setValue("Settings/defaultRcloneOptions",
                         dialog.getDefaultRcloneOptions().trimmed());

      settings->setValue("Settings/checkRcloneBrowserUpdates",
                         dialog.getCheckRcloneBrowserUpdates());
      settings->setValue("Settings/checkRcloneUpdates",
                         dialog.getCheckRcloneUpdates());

      settings->setValue("Settings/alwaysShowInTray",
                         dialog.getAlwaysShowInTray());
      settings->setValue("Settings/closeToTray", dialog.getCloseToTray());
      settings->setValue("Settings/notifyFinishedTransfers",
                         dialog.getNotifyFinishedTransfers());

      settings->setValue("Settings/showFolderIcons",
                         dialog.getShowFolderIcons());
      settings->setValue("Settings/showFileIcons", dialog.getShowFileIcons());
      settings->setValue("Settings/rowColors", dialog.getRowColors());
      settings->setValue("Settings/showHidden", dialog.getShowHidden());
      settings->setValue("Settings/darkMode", dialog.getDarkMode());

      settings->setValue("Settings/useProxy", dialog.getUseProxy());
      settings->setValue("Settings/http_proxy",
                         dialog.getHttpProxy().trimmed());
      settings->setValue("Settings/https_proxy",
                         dialog.getHttpsProxy().trimmed());
      settings->setValue("Settings/no_proxy", dialog.getNoProxy().trimmed());

      SetRclone(dialog.getRclone());
      SetRcloneConf(dialog.getRcloneConf());
      mFirstTime = true;
      rcloneGetVersion();

      mAlwaysShowInTray = dialog.getAlwaysShowInTray();
      mCloseToTray = dialog.getCloseToTray();
      mNotifyFinishedTransfers = dialog.getNotifyFinishedTransfers();

      mSystemTray.setVisible(mAlwaysShowInTray);
    }
  });

  QObject::connect(ui.quit, &QAction::triggered, this, [=]() {
    mCloseToTray = false;
    close();
  });

  QObject::connect(ui.about, &QAction::triggered, this, [=]() {
    QMessageBox::about(
        this, "rclone-browser",
        QString(
            R"(<h3>GUI for rclone, v)" RCLONE_BROWSER_VERSION "</h3>"
            R"(<p>Copyright &copy; 2019</p>)"

            R"(<p>Current development and maintenance<br /><a href="https://github.com/kapitainsky/RcloneBrowser">kapitainsky</a></p>)"

            R"(<p>New features and fixes<br /><a href="https://github.com/kapitainsky/RcloneBrowser/graphs/contributors">contributors</a></p>)"

            R"(<p>Original version<br /><a href="https://mmozeiko.github.io/RcloneBrowser">Martins Mozeiko</a></p>)"));
  });
  QObject::connect(ui.aboutQt, &QAction::triggered, qApp,
                   &QApplication::aboutQt);

  QObject::connect(
      ui.remotes, &QListWidget::currentItemChanged, this,
      [=](QListWidgetItem *current) { ui.open->setEnabled(current != NULL); });
  QObject::connect(ui.remotes, &QListWidget::itemActivated, ui.open,
                   &QPushButton::clicked);

  QObject::connect(ui.config, &QPushButton::clicked, this,
                   &MainWindow::rcloneConfig);
  QObject::connect(ui.refresh, &QPushButton::clicked, this,
                   &MainWindow::rcloneListRemotes);

  QObject::connect(ui.open, &QPushButton::clicked, this,
                   &MainWindow::openSelectedRemote);

  // Restic: right-click a remote to browse a restic repository stored on it,
  // and a menu entry for repositories that are not backed by an rclone remote.
  ui.remotes->setContextMenuPolicy(Qt::CustomContextMenu);
  QObject::connect(ui.remotes, &QListWidget::customContextMenuRequested, this,
                   &MainWindow::showRemotesContextMenu);

  QObject::connect(ui.configRestic, &QPushButton::clicked, this,
                   &MainWindow::manageResticRepos);

  // Without document mode the tab bar sits in a tall framed band, separated
  // from the content it labels.
  ui.tabs->setDocumentMode(true);

  QObject::connect(ui.tabs, &QTabWidget::tabCloseRequested, ui.tabs,
                   &QTabWidget::removeTab);

  QObject::connect(ui.tasksListWidget, &QListWidget::currentItemChanged, this,
                   [=](QListWidgetItem *current) {
                     ui.buttonDeleteTask->setEnabled(current != nullptr);
                     ui.buttonEditTask->setEnabled(current != nullptr);
                     ui.buttonRunTask->setEnabled(current != nullptr);
                     ui.buttonDryrunTask->setEnabled(current != nullptr);
                   });

  QObject::connect(ui.buttonNewTask, &QPushButton::clicked, this, [=]() {
    // Until now a task could only be created from the transfer dialog reached
    // via Upload/Download inside a remote, and then only by noticing the
    // "Save task" button. The Tasks tab could run, edit and delete tasks but
    // offered no way to make one.
    //
    // Same dialog, opened with no remote context: it starts blank and the
    // user fills in both sides.
    TransferDialog dialog(false, false, QString(), QString(), true, this);
    dialog.exec();
  });

  QObject::connect(ui.buttonRunTask, &QPushButton::clicked, this, [=]() {
    JobOptionsListWidgetItem *item = static_cast<JobOptionsListWidgetItem *>(
        ui.tasksListWidget->currentItem());
    runItem(item);
  });
  QObject::connect(ui.buttonDryrunTask, &QPushButton::clicked, this, [=]() {
    JobOptionsListWidgetItem *item = static_cast<JobOptionsListWidgetItem *>(
        ui.tasksListWidget->currentItem());
    runItem(item, true);
  });

  //    QObject::connect(ui.tasksListWidget, &QListWidget::itemDoubleClicked,
  //    this, [=]()
  //    {
  //        editSelectedTask();
  //    });

  QObject::connect(ui.buttonEditTask, &QPushButton::clicked, this,
                   [=]() { editSelectedTask(); });

  QObject::connect(ui.buttonDeleteTask, &QPushButton::clicked, this, [=]() {
    JobOptionsListWidgetItem *item = static_cast<JobOptionsListWidgetItem *>(
        ui.tasksListWidget->currentItem());
    JobOptions *jo = item->GetData();
    ListOfJobOptions::getInstance()->Forget(jo);
  });

  QObject::connect(ListOfJobOptions::getInstance(),
                   &ListOfJobOptions::tasksListUpdated, this,
                   &MainWindow::listTasks);

  // Reading the saved tasks happens on that first getInstance(), before this
  // window can show anything, so a failure is reported once the event loop is
  // running rather than swallowed.
  const QString taskRestoreError =
      ListOfJobOptions::getInstance()->takeRestoreError();
  if (!taskRestoreError.isEmpty()) {
    QTimer::singleShot(0, this, [this, taskRestoreError]() {
      QMessageBox::warning(this, "Saved tasks", taskRestoreError);
    });
  }

  QStyle *style = QApplication::style();
  ui.buttonDeleteTask->setIcon(style->standardIcon(QStyle::SP_TrashIcon));
  ui.buttonEditTask->setIcon(style->standardIcon(QStyle::SP_FileIcon));
  ui.buttonRunTask->setIcon(style->standardIcon(QStyle::SP_CommandLink));
  mUploadIcon = style->standardIcon(QStyle::SP_ArrowUp);
  mDownloadIcon = style->standardIcon(QStyle::SP_ArrowDown);

  ui.tabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
  ui.tabs->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);
  ui.tabs->tabBar()->setTabButton(1, QTabBar::RightSide, nullptr);
  ui.tabs->tabBar()->setTabButton(1, QTabBar::LeftSide, nullptr);
  ui.tabs->tabBar()->setTabButton(2, QTabBar::RightSide, nullptr);
  ui.tabs->tabBar()->setTabButton(2, QTabBar::LeftSide, nullptr);
  ui.tabs->setCurrentIndex(0);

  listTasks();

  QObject::connect(&mSystemTray, &QSystemTrayIcon::activated, this,
                   [=](QSystemTrayIcon::ActivationReason reason) {
                     if (reason == QSystemTrayIcon::DoubleClick ||
                         reason == QSystemTrayIcon::Trigger) {
                       showNormal();
                       mSystemTray.setVisible(mAlwaysShowInTray);
#ifdef Q_OS_MACOS
                       osxShowDockIcon();
#endif
                     }
                   });

  QObject::connect(&mSystemTray, &QSystemTrayIcon::messageClicked, this, [=]() {
    showNormal();
    mSystemTray.setVisible(mAlwaysShowInTray);
#ifdef Q_OS_MACOS
    osxShowDockIcon();
#endif

    ui.tabs->setCurrentIndex(1);
    if (mLastFinished) {
      mLastFinished->showDetails();
      ui.jobsArea->ensureWidgetVisible(mLastFinished);
    }
  });

  QMenu *trayMenu = new QMenu(this);
  QObject::connect(
      trayMenu->addAction("&Show"), &QAction::triggered, this, [=]() {
        MainWindow::setWindowState((windowState() & ~Qt::WindowMinimized) |
                                   Qt::WindowActive);
        MainWindow::show();  // bring window to top on macOS
        MainWindow::raise(); // bring window from minimized state on macOS
        MainWindow::activateWindow(); // bring window to front/unminimize on
                                      // windows
        mSystemTray.setVisible(mAlwaysShowInTray);
#ifdef Q_OS_MACOS
        osxShowDockIcon();
#endif
      });
  QObject::connect(trayMenu->addAction("&Quit"), &QAction::triggered, this,
                   &QWidget::close);
  mSystemTray.setContextMenu(trayMenu);

  mStatusMessage = new QLabel();
  ui.statusBar->addWidget(mStatusMessage);
  ui.statusBar->setStyleSheet("QStatusBar::item { border: 0; }");

  QTimer::singleShot(0, ui.remotes, SLOT(setFocus()));

  QString rclone = GetRclone();
  if (rclone.isEmpty()) {
    rclone = FindHelperExecutable("rclone");
    if (rclone.isEmpty()) {
      QMessageBox::information(
          this, "Error",
          "Cannot check rclone version!\nPlease verify rclone location.");
      emit ui.preferences->trigger();
    } else {
      auto settings = GetSettings();
      settings->setValue("Settings/rclone", rclone);
      SetRclone(rclone);
    }
  } else {
    rcloneGetVersion();
  }
}

MainWindow::~MainWindow() {
  auto settings = GetSettings();
  settings->setValue("MainWindow/geometry", saveGeometry());
}

void MainWindow::checkLatestRelease(
    const QString &enabledKey, const QString &dateKey, const QUrl &url,
    std::function<void(const QString &)> onRelease) {
  auto settings = GetSettings();

  // During the first run the preference does not exist yet. Both checks
  // default to on, as they always have.
  if (!settings->contains(enabledKey)) {
    settings->setValue(enabledKey, true);
  }
  if (!settings->value(enabledKey).toBool()) {
    return;
  }

  const QString today = QDate::currentDate().toString();
  if (settings->value(dateKey).toString() == today) {
    return;
  }

  if (!mNetwork) {
    mNetwork = new QNetworkAccessManager(this);
  }

  QNetworkRequest request(url);
  request.setRawHeader("Accept", "application/vnd.github+json");
  QNetworkReply *reply = mNetwork->get(request);

  // Asynchronous, where this used to spin a nested QEventLoop until the
  // request came back: on a slow or unreachable network that froze the window
  // for up to two lots of thirty seconds before it had finished starting.
  QObject::connect(reply, &QNetworkReply::finished, this, [=]() {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
      // Not recorded as a check: an offline launch, or a repository with no
      // releases yet, should try again next time rather than count as done.
      // The date used to be written before the request was even sent, so one
      // failed check meant no check at all for twenty-four hours.
      return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
    if (!document.object().contains("tag_name")) {
      return;
    }

    GetSettings()->setValue(dateKey, QDate::currentDate().toString());

    QString tag = document.object().value("tag_name").toString().trimmed();
    if (tag.startsWith('v')) {
      tag.remove(0, 1);
    }
    tag.remove("-DEV");

    if (!tag.isEmpty()) {
      onRelease(tag);
    }
  });
}

void MainWindow::checkForUpdates(const QString &rcloneVersion) {
  checkLatestRelease(
      "Settings/checkRcloneUpdates", "Settings/lastRcloneUpdateCheck",
      QUrl("https://api.github.com/repos/rclone/rclone/releases/latest"),
      [this, rcloneVersion](const QString &latest) {
        if (compareVersion(latest, rcloneVersion) != 1) {
          return;
        }
        QMessageBox::information(
            this, "",
            QString(R"(<p>New rclone version is available</p>)"
                    R"(<p>You have: v)" +
                    rcloneVersion + "<br />" + R"(New version: v)" + latest +
                    "</p>"
                    R"(<p>Visit rclone <a href="https://rclone.org/downloads/">downloads</a> page to upgrade</p>)"));
      });

  checkLatestRelease(
      "Settings/checkRcloneBrowserUpdates",
      "Settings/lastRcloneBrowserUpdateCheck",
      // Our own releases. This pointed at kapitainsky/rclonebrowser, which has
      // published nothing since 2020 and knows nothing about this fork.
      QUrl("https://api.github.com/repos/overnightpillow/rclone-browser/"
           "releases/latest"),
      [this](const QString &latest) {
        if (compareVersion(latest, RCLONE_BROWSER_VERSION) != 1) {
          return;
        }
        QMessageBox::information(
            this, "",
            QString(R"(<p>New rclone-browser version is available</p>)"
                    R"(<p>You have: v)" RCLONE_BROWSER_VERSION "<br />"
                    R"(New version: v)" +
                    latest +
                    "</p>"
                    R"(<p>Visit the <a href="https://github.com/overnightpillow/rclone-browser/releases/latest">releases</a> page to download it</p>)"));
      });
}

void MainWindow::rcloneGetVersion() {
  bool firstTime = mFirstTime;
  mFirstTime = false;

  QProcess *p = new QProcess();

  QObject::connect(
      p,
      &QProcess::finished,
      this, [=](int code, QProcess::ExitStatus) {
        if (code == 0) {
          QString version = p->readAllStandardOutput().trimmed();

          // extract rclone version - numbers only
          QString rclone_info1 = version;
          QString rclone_version_no;
          int lineBreak = rclone_info1.indexOf('\n');
          if (lineBreak != -1) {
            rclone_info1.remove(lineBreak, rclone_info1.length() - lineBreak);
            rclone_version_no = rclone_info1;
            rclone_version_no.replace("rclone v", "");
            rclone_version_no.replace("-DEV", "");
          } else {
            // for very old rclone versions format was one line only
            rclone_version_no = rclone_info1.trimmed();
            rclone_version_no.replace("rclone v", "");
            rclone_version_no.replace("-DEV", "");
          }
          // save current version no in settings
          auto settings = GetSettings();
          settings->setValue("Settings/rcloneVersion", rclone_version_no);

#if defined(Q_OS_WIN32)
          // check if required version
          unsigned int result =
              compareVersion(rclone_version_no, "1.50");

          if (result == 2) {
            QMessageBox::warning(
                this, "",
                "For mount functionality to work you need "
                "rclone version at least v1.50 "
                "and your current version is v" +
                    rclone_version_no +
                    ". Mount will be disabled. \n\nPlease consider upgrading.");
          };
#endif

          QStringList lines = version.split("\n", Qt::SkipEmptyParts);
          QString rclone_info2;
          QString rclone_info3;

          if (lines.size() > 1) {
            rclone_info2 = lines[1].trimmed().replace("- ", "");
          }
          if (lines.size() > 2) {
            rclone_info3 = lines[2].trimmed().replace("- ", "");
          }

          QFileInfo appBundlePath;
#ifdef Q_OS_MACOS
          if (IsPortableMode()) {

            QFileInfo applicationPath = QFileInfo(qApp->applicationFilePath());
            QFileInfo MacOSPath{applicationPath.dir().path()};
            QFileInfo ContentsPath{MacOSPath.dir().path()};
            appBundlePath = QFileInfo(ContentsPath.dir().path());

            mStatusMessage->setText(
                rclone_info1 + " in " +
                QDir::toNativeSeparators(GetRclone().replace(
                    appBundlePath.fileName() + "/Contents/MacOS/../../../",
                    "")) +
                ", " + rclone_info2 + ", " + rclone_info3);

          } else {

            mStatusMessage->setText(rclone_info1 + " in " +
                                    QDir::toNativeSeparators(GetRclone()) +
                                    ", " + rclone_info2 + ", " + rclone_info3);
          }
#else
#ifdef Q_OS_WIN
          mStatusMessage->setText(rclone_info1 + " in " +
                                  QDir::toNativeSeparators(GetRclone()) + ", " +
                                  rclone_info2 + ", " + rclone_info3);
#else
          if (IsPortableMode()) {
            QString xdg_config_home = qgetenv("XDG_CONFIG_HOME");
            QString appImageConfigFolder = xdg_config_home.right(xdg_config_home.length()-xdg_config_home.lastIndexOf("/"));

            mStatusMessage->setText(rclone_info1 + " in " +
                                  QDir::toNativeSeparators(GetRclone().replace(appImageConfigFolder + "/..",  "")) + ", " +
                                  rclone_info2 + ", " + rclone_info3);
          } else {
            mStatusMessage->setText(rclone_info1 + " in " +
                                  QDir::toNativeSeparators(GetRclone()) + ", " +
                                  rclone_info2 + ", " + rclone_info3);
         }
#endif
#endif

          rcloneListRemotes();
        } else {
          if (p->error() != QProcess::FailedToStart) {
            if (getConfigPassword(p)) {
              rcloneGetVersion();
            } else {
              close();
            }
            p->deleteLater();
            return;
          }

          if (firstTime) {
            if (p->error() == QProcess::FailedToStart) {
              QMessageBox::information(
                  this, "Error",
                  "Wrong rclone executable or rclone not found!\nPlease select "
                  "its location in next dialog.");
            } else {
              QMessageBox::information(this, "Error",
                                       "Cannot check rclone version!\nPlease "
                                       "verify rclone location.");
            }
            emit ui.preferences->trigger();
          }
        }

        auto settings = GetSettings();

        // get already stored rclone version no
        QString rclone_version_no =
            settings->value("Settings/rcloneVersion").toString();

        checkForUpdates(rclone_version_no);

        p->deleteLater();
      });

  UseRclonePassword(p);
  p->start(GetRclone(),
           QStringList() << "version"
                         << "--ask-password=false",
           QIODevice::ReadOnly);
}

void MainWindow::rcloneConfig() {

  // for macOS and Linux we have to take care of possible spaces in rclone and
  // rclone.conf paths by using "" around them
  QString terminalRcloneCmd;
  if (!GetRcloneConf().isEmpty()) {
    terminalRcloneCmd = "\"" + GetRclone() + "\"" + " config" + " --config " +
                        "\"" + GetRcloneConf().at(1) + "\"";
  } else {
    terminalRcloneCmd = "\"" + GetRclone() + "\"" + " config";
  }

#if defined(Q_OS_WIN32) && (QT_VERSION < QT_VERSION_CHECK(5, 7, 0))
  QProcess::startDetached(GetRclone(), QStringList()
                                           << "config" << GetRcloneConf());
  return;
#else

  QProcess *p = new QProcess(this);

  QObject::connect(p,
                   &QProcess::finished,
                   this, [=](int code, QProcess::ExitStatus) {
                     if (code == 0) {
                       emit rcloneListRemotes();
                     }
                     p->deleteLater();
                   });
#endif

#if defined(Q_OS_WIN32)
#if QT_VERSION >= QT_VERSION_CHECK(5, 7, 0)
  p->setCreateProcessArgumentsModifier(
      [](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NEW_CONSOLE;
        args->startupInfo->dwFlags &= ~STARTF_USESTDHANDLES;
      });
  p->setProgram(GetRclone());
  p->setArguments(QStringList() << "config" << GetRcloneConf());
#endif

#elif defined(Q_OS_MACOS)
  // The script is launched via `open`, so it has to outlive this scope, but it
  // must not sit at a predictable world-writable path: another user on the
  // machine could pre-create /tmp/rclone_config.command and have it run.
  // QTemporaryFile picks an unguessable name and creates it 0600.
  auto tmp = new QTemporaryFile(
      QDir(QDir::tempPath()).filePath("rclone_config_XXXXXX.command"), this);
  tmp->setAutoRemove(false);
  if (!tmp->open()) {
    QMessageBox::warning(this, "Error",
                         "Could not create a temporary script to launch rclone "
                         "config:\n" +
                             tmp->errorString());
    delete tmp;
    p->deleteLater();
    return;
  }
  {
    QTextStream out(tmp);
    out << "#!/bin/sh\n" << terminalRcloneCmd << "\n";
  }
  tmp->close();
  // Owner-only: the script's argv can carry the config path.
  tmp->setPermissions(QFileDevice::ReadUser | QFileDevice::WriteUser |
                      QFileDevice::ExeUser);
  p->setProgram("open");
  p->setArguments(QStringList() << tmp->fileName());
#else
  // Which flag introduces the command to run differs by terminal, and
  // gnome-terminal -- the first one the old nested search reached -- dropped
  // -e in 3.38, so the Config button did nothing at all on a current GNOME
  // desktop. The flag belongs with the terminal, not hard-coded after it.
  struct Terminal {
    const char *executable;
    // The flag after which the rest of the arguments are the command to run.
    const char *runFlag;
  };
  static const Terminal terminals[] = {
      {"x-terminal-emulator", "-e"}, // Debian's alternatives symlink
      {"gnome-terminal", "--"},
      {"konsole", "-e"},
      {"xfce4-terminal", "-x"},
      {"mate-terminal", "-x"},
      {"kitty", "--"},
      {"alacritty", "-e"},
      {"foot", "--"},
      {"wezterm", "start"},
      {"lxterminal", "-e"},
      {"urxvt", "-e"},
      {"st", "-e"},
      {"xterm", "-e"},
  };

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  QString terminal = env.value("TERMINAL");
  // No table entry to take a flag from for the user's own override, so keep
  // the -e this has always used.
  QString runFlag = "-e";

  if (terminal.isEmpty()) {
    for (const Terminal &candidate : terminals) {
      terminal = QStandardPaths::findExecutable(candidate.executable);
      if (!terminal.isEmpty()) {
        runFlag = candidate.runFlag;
        break;
      }
    }
  }

  if (terminal.isEmpty()) {
    QMessageBox::critical(this, "Error",
                          "Not sure how to launch terminal!\n"
                          "Please set path to terminal executable in "
                          "$TERMINAL environment variable.",
                          QMessageBox::Ok);
    return;
  }

  // The command is handed to a shell rather than to the terminal directly:
  // terminalRcloneCmd is a shell command line, quotes and all, and the
  // terminals that take the rest of argv would otherwise look for a program
  // named after the whole string.
  p->setArguments(QStringList() << runFlag << "sh"
                                << "-c" << terminalRcloneCmd);
  p->setProgram(terminal);
#endif

#if !defined(Q_OS_WIN32) || (QT_VERSION >= QT_VERSION_CHECK(5, 7, 0))
  UseRclonePassword(p);
  p->start(QIODevice::NotOpen);
#endif
}

// Marks a remotes-list entry as a saved restic repository rather than an
// rclone remote. The rclone entries put their backend type in Qt::UserRole,
// so a distinct role keeps the two apart without colliding.
static const int kResticIndexRole = Qt::UserRole + 1;

// Non-selectable divider separating the rclone remotes from the restic
// repositories in a single list.
static QListWidgetItem *makeSectionHeading(const QString &text,
                                           bool leadingSpace = false) {
  auto *heading = new QListWidgetItem(text);
  heading->setFlags(Qt::NoItemFlags);

  QFont font = heading->font();
  font.setBold(true);
  // Section labels read as chrome, not content.
  font.setPointSizeF(font.pointSizeF() * 0.85);
  font.setCapitalization(QFont::AllUppercase);
  heading->setFont(font);
  // No setForeground here, though it looks like the obvious place for one: a
  // disabled item is painted from the palette's Disabled group and the item's
  // own foreground brush is ignored. The colour comes from the theme sheet's
  // ::item:disabled rule instead, which also follows the scheme when it
  // changes -- a brush captured here would not.

  // Padding above a heading that follows another section, so the groups read
  // as separate rather than as one run of rows.
  QSize hint = heading->sizeHint();
  const int lineHeight = QFontMetrics(font).height();
  hint.setHeight(lineHeight + (leadingSpace ? lineHeight * 2 : 4));
  heading->setSizeHint(hint);
  heading->setTextAlignment(Qt::AlignLeft | Qt::AlignBottom);

  return heading;
}

void MainWindow::appendResticRepos() {
  const QList<ResticRepo> repos = GetResticRepos();
  if (repos.isEmpty()) {
    return;
  }

  // Follows the rclone section, so it gets the extra leading space.
  ui.remotes->addItem(
      makeSectionHeading("Restic repositories", ui.remotes->count() > 0));

  for (int i = 0; i < repos.size(); i++) {
    auto *item = new QListWidgetItem(repos[i].name);
    item->setData(kResticIndexRole, i);
    item->setToolTip(repos[i].repository);
    ui.remotes->addItem(item);
  }
}

void MainWindow::openSelectedRemote() {
  const auto selected = ui.remotes->selectedItems();
  if (selected.isEmpty()) {
    return;
  }
  QListWidgetItem *item = selected.front();

  const QVariant resticIndex = item->data(kResticIndexRole);
  if (resticIndex.isValid()) {
    const QList<ResticRepo> repos = GetResticRepos();
    const int index = resticIndex.toInt();
    if (index >= 0 && index < repos.size()) {
      openResticRepo(repos[index]);
    }
    return;
  }

  const QString type = item->data(Qt::UserRole).toString();
  const QString name = item->text();

  auto remote = new RemoteWidget(&mIcons, name, type == "local",
                                 type == "drive", ui.tabs);
  QObject::connect(remote, &RemoteWidget::addMount, this,
                   &MainWindow::addMount);
  QObject::connect(remote, &RemoteWidget::addStream, this,
                   &MainWindow::addStream);
  QObject::connect(remote, &RemoteWidget::addTransfer, this,
                   &MainWindow::addTransfer);
  QObject::connect(remote, &RemoteWidget::openRestic, this,
                   &MainWindow::openResticRepoAt);
  QObject::connect(remote, &RemoteWidget::saveRestic, this,
                   [=](const QString &r, const QString &p) {
                     addResticRepoFromRemote(r, p);
                   });

  const int index = ui.tabs->addTab(remote, name);
  ui.tabs->setCurrentIndex(index);
}

void MainWindow::openResticRepo(const ResticRepo &repo) {
  if (!EnsureResticPassword(repo, this)) {
    return;
  }

  auto *widget = new ResticWidget(repo, ui.tabs);
  const int index = ui.tabs->addTab(widget, "restic: " + repo.name);
  ui.tabs->setCurrentIndex(index);
}

void MainWindow::openResticRepoAt(const QString &remote, const QString &path) {
  ResticRepo repo;
  repo.repository = ResticRepoForRemote(remote, path);
  repo.name = path.isEmpty() ? remote : remote + "/" + path;

  // Prefer a saved entry for the same repository, so a configured password
  // command applies instead of prompting.
  for (const ResticRepo &known : GetResticRepos()) {
    if (known.repository == repo.repository) {
      repo = known;
      break;
    }
  }

  openResticRepo(repo);
}

void MainWindow::addResticRepoFromRemote(const QString &remote,
                                         const QString &path_) {
  QString path = path_;

  if (path.isEmpty()) {
    bool ok = false;
    path = QInputDialog::getText(
        this, "Add restic repository",
        QString("Path of the restic repository within %1:\n\n"
                "Leave empty if the repository is at the root of the remote.")
            .arg(remote),
        QLineEdit::Normal, QString(), &ok);
    if (!ok) {
      return;
    }
  }

  ResticRepo prefilled;
  prefilled.repository = ResticRepoForRemote(remote, path);
  prefilled.name = path.isEmpty() ? remote : remote + "/" + path;

  // Hand it to the normal dialog so the name and an optional password command
  // can be set in the same pass, rather than inventing a second entry path.
  ResticRepoDialog dialog(this, prefilled);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  QList<ResticRepo> repos = GetResticRepos();

  // Adding the same repository twice would produce two list entries that only
  // differ by name, so update the existing one instead.
  const ResticRepo added = dialog.repo();
  bool replaced = false;
  for (ResticRepo &existing : repos) {
    if (existing.repository == added.repository) {
      existing = added;
      replaced = true;
      break;
    }
  }
  if (!replaced) {
    repos.append(added);
  }

  SetResticRepos(repos);
  rcloneListRemotes();
  openResticRepo(added);
}

void MainWindow::showRemotesContextMenu(const QPoint &pos) {
  QListWidgetItem *item = ui.remotes->itemAt(pos);
  if (!item || item->flags() == Qt::NoItemFlags) {
    return;
  }

  // A right-click does not move the selection on its own, and the actions
  // below read the selected entry.
  ui.remotes->setCurrentItem(item);

  QMenu menu(this);

  const QVariant resticIndex = item->data(kResticIndexRole);
  if (resticIndex.isValid()) {
    QList<ResticRepo> repos = GetResticRepos();
    const int index = resticIndex.toInt();
    if (index < 0 || index >= repos.size()) {
      return;
    }

    QAction *open = menu.addAction("Open");
    QAction *edit = menu.addAction("Edit...");
    menu.addSeparator();
    QAction *remove = menu.addAction("Remove from list");

    QAction *chosen = menu.exec(ui.remotes->mapToGlobal(pos));
    if (chosen == open) {
      openResticRepo(repos[index]);
    } else if (chosen == edit) {
      ResticRepoDialog dialog(this, repos[index]);
      if (dialog.exec() == QDialog::Accepted) {
        ForgetResticPassword(repos[index]);
        repos[index] = dialog.repo();
        SetResticRepos(repos);
        rcloneListRemotes();
      }
    } else if (chosen == remove) {
      if (QMessageBox::question(
              this, "Remove",
              QString("Remove '%1' from the list?\n\nThe repository itself is "
                      "not touched.")
                  .arg(repos[index].name)) == QMessageBox::Yes) {
        ForgetResticPassword(repos[index]);
        repos.removeAt(index);
        SetResticRepos(repos);
        rcloneListRemotes();
      }
    }
    return;
  }

  QAction *open = menu.addAction("Open");
  menu.addSeparator();
  QAction *addRestic = menu.addAction("Add as restic repository...");

  QAction *chosen = menu.exec(ui.remotes->mapToGlobal(pos));
  if (chosen == open) {
    openSelectedRemote();
  } else if (chosen == addRestic) {
    addResticRepoFromRemote(item->text());
  }
}

void MainWindow::manageResticRepos() {
  QDialog dialog(this);
  dialog.setWindowTitle("Restic repositories");
  dialog.resize(620, 320);

  auto *list = new QListWidget(&dialog);
  QList<ResticRepo> repos = GetResticRepos();

  auto reload = [&]() {
    list->clear();
    for (const ResticRepo &repo : repos) {
      auto *entry = new QListWidgetItem(repo.name);
      entry->setToolTip(repo.repository);
      list->addItem(entry);
    }
  };
  reload();

  auto *buttons = new QDialogButtonBox(&dialog);
  QPushButton *add = buttons->addButton("Add...", QDialogButtonBox::ActionRole);
  QPushButton *edit =
      buttons->addButton("Edit...", QDialogButtonBox::ActionRole);
  QPushButton *remove =
      buttons->addButton("Remove", QDialogButtonBox::DestructiveRole);
  QPushButton *open = buttons->addButton("Open", QDialogButtonBox::AcceptRole);
  buttons->addButton(QDialogButtonBox::Close);

  auto updateButtons = [&]() {
    const bool hasSelection = list->currentRow() >= 0;
    edit->setEnabled(hasSelection);
    remove->setEnabled(hasSelection);
    open->setEnabled(hasSelection);
  };
  updateButtons();

  QObject::connect(list, &QListWidget::currentRowChanged, &dialog,
                   [&]() { updateButtons(); });

  QObject::connect(add, &QPushButton::clicked, &dialog, [&]() {
    ResticRepoDialog repoDialog(&dialog);
    if (repoDialog.exec() == QDialog::Accepted) {
      repos.append(repoDialog.repo());
      SetResticRepos(repos);
      reload();
    }
  });

  QObject::connect(edit, &QPushButton::clicked, &dialog, [&]() {
    const int row = list->currentRow();
    if (row < 0) {
      return;
    }
    ResticRepoDialog repoDialog(&dialog, repos[row]);
    if (repoDialog.exec() == QDialog::Accepted) {
      // The password may have been cached against the old repository string.
      ForgetResticPassword(repos[row]);
      repos[row] = repoDialog.repo();
      SetResticRepos(repos);
      reload();
    }
  });

  QObject::connect(remove, &QPushButton::clicked, &dialog, [&]() {
    const int row = list->currentRow();
    if (row < 0) {
      return;
    }
    if (QMessageBox::question(
            &dialog, "Remove",
            QString("Remove '%1' from the list?\n\nThe repository itself is "
                    "not touched.")
                .arg(repos[row].name)) != QMessageBox::Yes) {
      return;
    }
    ForgetResticPassword(repos[row]);
    repos.removeAt(row);
    SetResticRepos(repos);
    reload();
  });

  QObject::connect(open, &QPushButton::clicked, &dialog, &QDialog::accept);
  QObject::connect(list, &QListWidget::itemActivated, &dialog,
                   &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);

  auto *layout = new QVBoxLayout(&dialog);
  layout->addWidget(list, 1);
  layout->addWidget(buttons);

  const int row = dialog.exec() == QDialog::Accepted ? list->currentRow() : -1;

  // The remotes panel lists these repositories, so it has to follow any adds,
  // edits or removals made here.
  rcloneListRemotes();

  if (row >= 0 && row < repos.size()) {
    openResticRepo(repos[row]);
  }
}

void MainWindow::rcloneListRemotes() {
  ui.remotes->clear();

  QProcess *p = new QProcess();

  QObject::connect(
      p,
      &QProcess::finished,
      this, [=](int code, QProcess::ExitStatus) {
        if (code == 0) {
          const QString bytes = p->readAllStandardOutput().trimmed();
          const QStringList items = bytes.split('\n');

          // The remotes list is a plain text list. It used to render a custom
          // 320x320 logo per backend type, with a block of per-platform,
          // per-theme scaling maths to size them -- two visual languages in one
          // window, since every other icon in the app comes from QStyle.
          bool addedHeading = false;

          for (const QString &line : items) {
            if (line.isEmpty()) {
              continue;
            }

            const QStringList parts = line.split(':');
            if (parts.count() != 2) {
              continue;
            }

            const QString name = parts[0].trimmed();
            const QString type = parts[1].trimmed();

            if (!addedHeading) {
              ui.remotes->addItem(makeSectionHeading("rclone remotes"));
              addedHeading = true;
            }

            auto *item = new QListWidgetItem(name);
            item->setData(Qt::UserRole, type);
            item->setToolTip(type);
            ui.remotes->addItem(item);
          }

          appendResticRepos();
        } else {
          if (p->error() != QProcess::FailedToStart) {
            if (getConfigPassword(p)) {
              rcloneListRemotes();
            }
          }
        }
        p->deleteLater();
      });

  UseRclonePassword(p);
  p->start(GetRclone(),
           QStringList() << "listremotes" << GetRcloneConf() << "--long"
                         << "--ask-password=false",
           QIODevice::ReadOnly);
}

bool MainWindow::getConfigPassword(QProcess *p) {
  QString output = p->readAllStandardError().trimmed();
  if (output.indexOf("RCLONE_CONFIG_PASS") > 0) {
    bool ok;
    QString password = QInputDialog::getText(
        this, qApp->applicationDisplayName(),
        "Enter password for .rclone.conf configuration file:",
        QLineEdit::Password, QString(), &ok);
    if (ok) {
      SetRclonePassword(password);
      return true;
    }
  } else if (output.indexOf("unknown command \"listremotes\"") > 0) {
    QMessageBox::critical(this, qApp->applicationDisplayName(),
                          "It seems rclone version you are using is too "
                          "old.\nPlease upgrade to the latest version");
    return false;
  }
  return false;
}

bool MainWindow::canClose() {
  if (mJobCount == 0) {
    return true;
  }

  bool wasVisible = isVisible();

  ui.tabs->setCurrentIndex(1);
  showNormal();

  int button =
      QMessageBox::question(this, "rclone-browser",
                            QString("There are %1 job(s) running.\n"
                                    "Do you want to stop them and quit?")
                                .arg(mJobCount),
                            QMessageBox::Yes | QMessageBox::No);

  if (!wasVisible) {
    hide();
  }

  if (button == QMessageBox::Yes) {
    // Collected first: cancelling a job can close its row, which removes
    // widgets from this very layout, and indices shifted underneath the loop
    // so some jobs were skipped and never asked to stop.
    QVector<QWidget *> widgets;
    for (int i = 0; i < ui.jobs->count(); i++) {
      if (QWidget *widget = ui.jobs->itemAt(i)->widget()) {
        widgets.append(widget);
      }
    }

    for (QWidget *widget : widgets) {
      if (auto mount = qobject_cast<MountWidget *>(widget)) {
        mount->cancel();
      } else if (auto transfer = qobject_cast<JobWidget *>(widget)) {
        transfer->cancel();
      } else if (auto stream = qobject_cast<StreamWidget *>(widget)) {
        stream->cancel();
      }
    }
    return true;
  }

  return false;
}

void MainWindow::closeEvent(QCloseEvent *ev) {
  if (mCloseToTray && isVisible()) {
#ifdef Q_OS_MACOS
    osxHideDockIcon();
#endif
    mSystemTray.show();
    hide();
    ev->ignore();
    return;
  }

  if (canClose()) {
    QApplication::quit();
  } else {
    ev->ignore();
  }
}

void MainWindow::listTasks() {
  ui.tasksListWidget->clear();

  ListOfJobOptions *ljo = ListOfJobOptions::getInstance();

  for (JobOptions *jo : ljo->getTasks()) {
    JobOptionsListWidgetItem *item = new JobOptionsListWidgetItem(
        jo,
        jo->jobType == JobOptions::JobType::Download ? mDownloadIcon
                                                     : mUploadIcon,
        jo->description);
    ui.tasksListWidget->addItem(item);
  }
}

void MainWindow::notify(const QString &title, const QString &message) {
  if (!QSystemTrayIcon::supportsMessages()) {
    return;
  }

  if (!mSystemTray.isVisible()) {
    mSystemTray.setVisible(true);

    // Long enough for the notification to be delivered and read; hiding the
    // icon straight away takes the notification with it.
    QTimer::singleShot(15000, this, [this]() {
      // Not while the window is hidden to the tray: that icon is the only way
      // back to the application.
      if (isVisible()) {
        mSystemTray.setVisible(mAlwaysShowInTray);
      }
    });
  }

  mSystemTray.showMessage(title, message);
}

void MainWindow::runItem(JobOptionsListWidgetItem *item, bool dryrun) {
  if (item == nullptr)
    return;
  JobOptions *jo = item->GetData();
  jo->dryRun = dryrun;
  QStringList args = jo->getOptions();
  addTransfer(QString("%1 %2").arg(jo->operation).arg(jo->source), jo->source,
              jo->dest, args);

  // Running a task left the user looking at the task list, where nothing
  // happens: the job, its progress and its output are all on the Jobs tab,
  // and the only hint was the count in that tab's title.
  ui.tabs->setCurrentIndex(1);
}

void MainWindow::editSelectedTask() {
  auto selection = ui.tasksListWidget->selectionModel()->currentIndex();
  JobOptionsListWidgetItem *item = static_cast<JobOptionsListWidgetItem *>(
      ui.tasksListWidget->currentItem());
  JobOptions *jo = item->GetData();
  bool isDownload = (jo->jobType == JobOptions::Download);
  QString remote = isDownload ? jo->source : jo->dest;
  QString path = isDownload ? jo->dest : jo->source;
  // qDebug() << "remote:" + remote;
  // qDebug() << "path:" + path;
  TransferDialog td(isDownload, false, remote, path, jo->isFolder, this, jo,
                    true);
  td.exec();
  // restore the selection to help user keep track of what s/he was doing
  ui.tasksListWidget->selectionModel()->select(selection,
                                               QItemSelectionModel::Select);
  // edit mode on the TransferDialog suppresses the usual Accept buttons
  // and the Save Task button closes it... so there is nothing more to do here
}

void MainWindow::addTransfer(const QString &message, const QString &source,
                             const QString &dest, const QStringList &args) {
  QProcess *transfer = new QProcess(this);
  transfer->setProcessChannelMode(QProcess::MergedChannels);

  auto widget = new JobWidget(transfer, message, args, source, dest);

  auto line = new QFrame();
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Sunken);

  QObject::connect(
      widget, &JobWidget::finished, this, [=](const QString &info,
                                              bool success) {
        // A cancelled job is not news: the user just asked for it, and the row
        // is about to remove itself.
        if (!widget->wasCancelled()) {
          const QString what = success ? "finished" : "failed";
          ui.statusBar->showMessage(QString("Transfer %1: %2").arg(what, info),
                                    15000);

          if (mNotifyFinishedTransfers) {
            qApp->alert(this);
            mLastFinished = widget;
            notify(success ? "Transfer finished" : "Transfer failed", info);
          }
        }

        if (--mJobCount == 0) {
          ui.tabs->setTabText(1, "Jobs");
        } else {
          ui.tabs->setTabText(1, QString("Jobs (%1)").arg(mJobCount));
        }
      });

  QObject::connect(widget, &JobWidget::closed, this, [=]() {
    if (widget == mLastFinished) {
      mLastFinished = nullptr;
    }
    ui.jobs->removeWidget(widget);
    ui.jobs->removeWidget(line);
    widget->deleteLater();
    delete line;
    if (ui.jobs->count() == 2) {
      ui.noJobsAvailable->show();
    }
  });

  if (ui.jobs->count() == 2) {
    ui.noJobsAvailable->hide();
  }

  ui.jobs->insertWidget(0, widget);
  ui.jobs->insertWidget(1, line);
  ui.tabs->setTabText(1, QString("Jobs (%1)").arg(++mJobCount));

  UseRclonePassword(transfer);
  transfer->start(GetRclone(), GetRcloneConf() + args, QIODevice::ReadOnly);
}

void MainWindow::addMount(const QString &remote, const QString &folder,
                          bool driveShared) {
  QProcess *mount = new QProcess(this);
  mount->setProcessChannelMode(QProcess::MergedChannels);

  auto widget = new MountWidget(mount, remote, folder);

  auto line = new QFrame();
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Sunken);

  QObject::connect(widget, &MountWidget::finished, this, [=]() {
    if (--mJobCount == 0) {
      ui.tabs->setTabText(1, "Jobs");
    } else {
      ui.tabs->setTabText(1, QString("Jobs (%1)").arg(mJobCount));
    }
  });

  QObject::connect(widget, &MountWidget::closed, this, [=]() {
    ui.jobs->removeWidget(widget);
    ui.jobs->removeWidget(line);
    widget->deleteLater();
    delete line;
    if (ui.jobs->count() == 2) {
      ui.noJobsAvailable->show();
    }
  });

  if (ui.jobs->count() == 2) {
    ui.noJobsAvailable->hide();
  }

  ui.jobs->insertWidget(0, widget);
  ui.jobs->insertWidget(1, line);
  ui.tabs->setTabText(1, QString("Jobs (%1)").arg(++mJobCount));

  auto settings = GetSettings();
  QString opt = settings->value("Settings/mount").toString();

  QStringList args;
  args << "mount";

#if defined(Q_OS_WIN32)
  args << "--rc";
  args << "--rc-addr";

  // calculate remote control interface port based on mount drive letter
  // this way every mount will have unique port assigned
  int port_offset = folder[0].toLatin1();
  unsigned short int rclone_rc_port_base = 19000;
  unsigned short int rclone_rc_port = rclone_rc_port_base + port_offset;
  args << "localhost:" + QVariant(rclone_rc_port).toString();
#endif

  // for google drive "shared with me" without --read-only writes go created in
  // main google drive it is more logical to mount it as read only so there is
  // no confusion
  if (driveShared) {
    args << "--drive-shared-with-me";
    args << "--read-only";
  };

  //	 default mount is now more generic. all options can be passed via
  // preferences mount field
  //       args << "--vfs-cache-mode";
  //       args << "writes";

  args.append(GetRcloneConf());
  if (!opt.isEmpty()) {
    args.append(opt.split(' '));
  }
  args << remote << folder;

  UseRclonePassword(mount);
  mount->start(GetRclone(), args, QIODevice::ReadOnly);
}

void MainWindow::addStream(const QString &remote, const QString &stream) {
  // Parented, like the transfer and mount processes: unparented, neither was
  // ever freed if it outlived its widget, and Qt could not clean them up on
  // exit either.
  auto player = new QProcess(this);
  auto rclone = new QProcess(this);
  rclone->setStandardOutputProcess(player);

  QObject::connect(
      player,
      &QProcess::finished,
      this, [=](int status, QProcess::ExitStatus) {
        player->deleteLater();
        if (status != 0 && player->error() == QProcess::FailedToStart) {
          QMessageBox::critical(
              this, "Error",
              QString("Failed to start '%1' player process").arg(stream));
          auto settings = GetSettings();
          settings->remove("Settings/streamConfirmed");
        }
      });

  auto widget = new StreamWidget(rclone, player, remote, stream);

  auto line = new QFrame();
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Sunken);

  QObject::connect(widget, &StreamWidget::finished, this, [=]() {
    if (--mJobCount == 0) {
      ui.tabs->setTabText(1, "Jobs");
    } else {
      ui.tabs->setTabText(1, QString("Jobs (%1)").arg(mJobCount));
    }
  });

  QObject::connect(widget, &StreamWidget::closed, this, [=]() {
    ui.jobs->removeWidget(widget);
    ui.jobs->removeWidget(line);
    widget->deleteLater();
    delete line;
    if (ui.jobs->count() == 2) {
      ui.noJobsAvailable->show();
    }
  });

  if (ui.jobs->count() == 2) {
    ui.noJobsAvailable->hide();
  }

  ui.jobs->insertWidget(0, widget);
  ui.jobs->insertWidget(1, line);
  ui.tabs->setTabText(1, QString("Jobs (%1)").arg(++mJobCount));

  // Qt6 removed the command-splitting start() overload; startCommand() is the
  // direct replacement and keeps the same quote-aware argument splitting.
  player->startCommand(stream, QProcess::ReadOnly);
  UseRclonePassword(rclone);
  rclone->start(GetRclone(),
                QStringList() << "cat" << GetRcloneConf() << remote,
                QProcess::WriteOnly);
}
