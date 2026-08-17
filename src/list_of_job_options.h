#pragma once
#include "job_options.h"
#include <QDir>
#include <QSaveFile>
#include <qfile.h>

class ListOfJobOptions : public QObject {
  Q_OBJECT

protected:
  ~ListOfJobOptions() = default;
  ListOfJobOptions();

public:
  static ListOfJobOptions *getInstance();
  bool Persist(JobOptions *jo);
  bool Forget(JobOptions *jo);
  QList<JobOptions *> &getTasks() { return tasks; }

  // Non-empty when the saved tasks could not be read at startup. Taking it
  // clears it, so the warning is shown once rather than on every tab switch.
  QString takeRestoreError() {
    const QString error = mRestoreError;
    mRestoreError.clear();
    return error;
  }

  // Reading and writing the task file, split out from the singleton so both
  // can be exercised against a path of the caller's choosing. ReadTasks
  // returns false with error set when the file exists but cannot be parsed,
  // and false with error empty when there is simply nothing to read.
  static bool ReadTasks(const QString &filePath, QList<JobOptions *> *tasks,
                        QString *error);
  static bool WriteTasks(const QString &filePath,
                         const QList<JobOptions *> &tasks);

signals:
  void tasksListUpdated();

private:
  static ListOfJobOptions *SavedJobOptions;
  static const QString persistenceFileName;
  static bool RestoreFromUserData(ListOfJobOptions &dataIn);
  static QDir GetPersistenceDir();
  static QString GetPersistenceFilePath();

  QList<JobOptions *> tasks;
  QString mRestoreError;
  bool PersistToUserData();
};
