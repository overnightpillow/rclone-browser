#include "list_of_job_options.h"
#include <QDataStream>
#include <qdir.h>
#include <qlogging.h>
#include <qstandardpaths.h>
#include "utils.h"

static QDataStream &operator>>(QDataStream &dataStream, JobOptions &jo);
static QDataStream &operator<<(QDataStream &dataStream, JobOptions &jo);
static QDataStream &operator>>(QDataStream &in, JobOptions::Operation &e);
static QDataStream &operator>>(QDataStream &in, JobOptions::SyncTiming &e);
static QDataStream &operator>>(QDataStream &in, JobOptions::CompareOption &e);
static QDataStream &operator>>(QDataStream &in, JobOptions::JobType &e);

ListOfJobOptions *ListOfJobOptions::SavedJobOptions = nullptr;
const QString ListOfJobOptions::persistenceFileName = "tasks.bin";

ListOfJobOptions::ListOfJobOptions() {}

ListOfJobOptions *ListOfJobOptions::getInstance() {
  if (SavedJobOptions == nullptr) {
    SavedJobOptions = new ListOfJobOptions();
    RestoreFromUserData(*SavedJobOptions);
  }
  return SavedJobOptions;
}

bool ListOfJobOptions::Persist(JobOptions *jo) {
  bool isNew = !this->tasks.contains(jo);
  if (isNew)
    this->tasks.append(jo);
  else {
    //    int ix = tasks.indexOf(jo);
    //    JobOptions *old = tasks[ix];
    //    qDebug() << QString("old [%1] New [%2]")
    //                    .arg(old->description)
    //                    .arg(jo->description);
  }
  PersistToUserData();
  return isNew;
}

bool ListOfJobOptions::Forget(JobOptions *jo) {
  bool isKnown = this->tasks.contains(jo);
  if (!isKnown)
    return false;
  int ix = tasks.indexOf(jo);
  tasks.removeAt(ix);
  //  qDebug() << QString("removed [%1]").arg(jo->description);
  PersistToUserData();
  return isKnown;
}

QDir ListOfJobOptions::GetPersistenceDir() {

  QDir outputDir;

  if (IsPortableMode()) {
    // in portable mode tasks' file will be saved in the same folder as
    // excecutable
#ifdef Q_OS_MACOS
    // on macOS excecutable file is located in
    // ./rclone-browser.app/Contents/MasOS/
    // to get actual bundle folder we have
    // to traverse three levels up
    outputDir = QDir(qApp->applicationDirPath() + "/../../..");
#else
#ifdef Q_OS_WIN
    // not macOS
    outputDir = QDir(qApp->applicationDirPath());
#else
    QString xdg_config_home = qgetenv("XDG_CONFIG_HOME");
    outputDir = QDir(xdg_config_home + "/rclone-browser");
#endif
#endif

  } else {

    // get data location folder from Qt  - OS dependend
    outputDir =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
  }

  if (!outputDir.exists()) {
    outputDir.mkpath(".");
  }
  return outputDir;
}

QString ListOfJobOptions::GetPersistenceFilePath() {
  return GetPersistenceDir().absoluteFilePath(persistenceFileName);
}

bool ListOfJobOptions::ReadTasks(const QString &filePath,
                                 QList<JobOptions *> *tasks, QString *error) {
  Q_ASSERT(tasks);

  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    // No file yet is the normal first-run state, not a failure worth
    // reporting.
    return false;
  }

  QDataStream instream(&file);
  instream.setVersion(QDataStream::Qt_5_2);

  QList<JobOptions *> restored;
  QString failure;

  while (!instream.atEnd()) {
    JobOptions *jo = new JobOptions();
    try {
      instream >> *jo;
    } catch (SerializationException &ex) {
      delete jo;
      failure = ex.Message;
      break;
    }
    restored.append(jo);
  }

  if (failure.isEmpty() && instream.status() != QDataStream::Ok) {
    // A truncated file ends part way through a field rather than at a record
    // boundary, which the exceptions above do not catch: QDataStream reports
    // it through its status and hands back zeroed values.
    failure = "the file ends part way through a task";
  }

  if (!failure.isEmpty()) {
    qDeleteAll(restored);
    if (error) {
      *error = failure;
    }
    return false;
  }

  tasks->append(restored);
  return true;
}

bool ListOfJobOptions::WriteTasks(const QString &filePath,
                                  const QList<JobOptions *> &tasks) {
  // QSaveFile writes to a temporary file and renames it into place on commit,
  // so an interrupted write leaves the previous task list intact. Opening the
  // real file truncated it first, which meant a crash, a full disk or a power
  // cut between that truncate and the last record lost every saved task.
  QSaveFile file(filePath);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }

  QDataStream outstream(&file);
  outstream.setVersion(QDataStream::Qt_5_2);

  for (JobOptions *it : tasks) {
    outstream << *it;
  }

  if (outstream.status() != QDataStream::Ok) {
    file.cancelWriting();
    return false;
  }

  return file.commit();
}

bool ListOfJobOptions::RestoreFromUserData(ListOfJobOptions &dataIn) {
  const QString filePath = GetPersistenceFilePath();
  if (!QFile::exists(filePath)) {
    return false;
  }

  QString error;
  if (ReadTasks(filePath, &dataIn.tasks, &error)) {
    return true;
  }
  if (error.isEmpty()) {
    return false;
  }

  // The previous code discarded every saved task here without a word, so one
  // bad byte silently emptied the Tasks tab -- and the next save wrote that
  // empty list back over the only copy. Keep the evidence, and say so.
  const QString corruptPath = filePath + ".corrupt";
  QFile::remove(corruptPath);
  const bool kept = QFile::rename(filePath, corruptPath);

  dataIn.mRestoreError =
      QString("Saved tasks could not be read: %1.").arg(error) +
      (kept ? QString("\n\nThe file has been kept as %1 and the task list "
                      "starts empty.")
                  .arg(QDir::toNativeSeparators(corruptPath))
            : QString("\n\nThe file could not be moved aside, so it will be "
                      "overwritten when a task is next saved."));
  return false;
}

bool ListOfJobOptions::PersistToUserData() {
  if (!WriteTasks(GetPersistenceFilePath(), tasks)) {
    return false;
  }

  emit tasksListUpdated();

  return true;
}

QDataStream &operator<<(QDataStream &stream, JobOptions &jo) {
  stream << jo.myName() << JobOptions::classVersion << jo.description
         << jo.jobType << jo.operation << /* jo.dryRun <<*/ jo.sync
         << jo.syncTiming << jo.skipNewer << jo.skipExisting << jo.compare
         << jo.compareOption << jo.verbose << jo.sameFilesystem
         << jo.dontUpdateModified << jo.transfers << jo.checkers << jo.bandwidth
         << jo.minSize << jo.minAge << jo.maxAge << jo.maxDepth
         << jo.connectTimeout << jo.idleTimeout << jo.retries
         << jo.lowLevelRetries << jo.deleteExcluded << jo.excluded << jo.extra
         << jo.DriveSharedWithMe << jo.source << jo.dest << jo.isFolder
         << jo.uniqueId;

  return stream;
}

QDataStream &operator>>(QDataStream &stream, JobOptions &jo) {
  QString actualName;
  qint32 actualVersion;

  stream >> actualName;
  if (QString::compare(actualName, jo.myName()) != 0)
    throw SerializationException("incorrect class");

  stream >> actualVersion;
  if (actualVersion > JobOptions::classVersion)
    throw SerializationException("stored version is newer");

  stream >> jo.description >> jo.jobType >> jo.operation >>
      /* jo.dryRun >> */ jo.sync >> jo.syncTiming >> jo.skipNewer >>
      jo.skipExisting >> jo.compare >> jo.compareOption >> jo.verbose >>
      jo.sameFilesystem >> jo.dontUpdateModified >> jo.transfers >>
      jo.checkers >> jo.bandwidth >> jo.minSize >> jo.minAge >> jo.maxAge >>
      jo.maxDepth >> jo.connectTimeout >> jo.idleTimeout >> jo.retries >>
      jo.lowLevelRetries >> jo.deleteExcluded >> jo.excluded >> jo.extra >>
      jo.DriveSharedWithMe >> jo.source >> jo.dest;

  // as fields are added in later revisions, check actualVersion here and
  // conditionally extract any new fields iff they are expected based on the
  // stream value
  if (actualVersion >= 2) {
    stream >> jo.isFolder;
    if (actualVersion >= 3) {
      stream >> jo.uniqueId;
    }
  }

  return stream;
}

QDataStream &operator>>(QDataStream &in, JobOptions::Operation &e) {
  in >> (quint32 &)e;
  return in;
}

QDataStream &operator>>(QDataStream &in, JobOptions::SyncTiming &e) {
  in >> (quint32 &)e;
  return in;
}

QDataStream &operator>>(QDataStream &in, JobOptions::CompareOption &e) {
  in >> (quint32 &)e;
  return in;
}

QDataStream &operator>>(QDataStream &in, JobOptions::JobType &e) {
  in >> (quint32 &)e;
  return in;
}
