#include "restic.h"
#include "utils.h"

namespace {

// Session-only cache of prompted passwords, keyed by repository string. This
// deliberately never reaches QSettings: the repository password is the only
// thing standing between a stolen config file and the backup contents.
QHash<QString, QString> &passwordCache() {
  static QHash<QString, QString> cache;
  return cache;
}

} // namespace

QString ShellQuote(const QString &argument) {
  // Single quotes protect everything except a single quote itself, which is
  // closed, escaped and reopened: it's  ->  'it'\''s'
  QString quoted = argument;
  quoted.replace("'", R"('\'')");
  return "'" + quoted + "'";
}

QString ResticRepoForRemote(const QString &remote, const QString &path) {
  QString trimmed = path;
  while (trimmed.startsWith('/')) {
    trimmed.remove(0, 1);
  }
  return "rclone:" + remote + ":" + trimmed;
}

QList<ResticRepo> GetResticRepos() {
  auto settings = GetSettings();
  QList<ResticRepo> repos;

  const int count = settings->beginReadArray("ResticRepos");
  repos.reserve(count);
  for (int i = 0; i < count; i++) {
    settings->setArrayIndex(i);

    ResticRepo repo;
    repo.name = settings->value("name").toString();
    repo.repository = settings->value("repository").toString();
    repo.passwordCommand = settings->value("passwordCommand").toString();

    if (repo.isValid()) {
      if (repo.name.isEmpty()) {
        repo.name = repo.repository;
      }
      repos.append(repo);
    }
  }
  settings->endArray();

  return repos;
}

void SetResticRepos(const QList<ResticRepo> &repos) {
  auto settings = GetSettings();

  // beginWriteArray only truncates down to the new size, so a stale longer
  // array would keep its tail entries.
  settings->remove("ResticRepos");

  settings->beginWriteArray("ResticRepos", repos.size());
  for (int i = 0; i < repos.size(); i++) {
    settings->setArrayIndex(i);
    settings->setValue("name", repos[i].name);
    settings->setValue("repository", repos[i].repository);
    settings->setValue("passwordCommand", repos[i].passwordCommand);
  }
  settings->endArray();
}

QString GetRestic() {
  auto settings = GetSettings();
  const QString configured = settings->value("Settings/restic").toString().trimmed();
  if (!configured.isEmpty()) {
    return configured;
  }
  // Fall back to PATH lookup so the common case needs no configuration.
  const QString found = QStandardPaths::findExecutable("restic");
  return found.isEmpty() ? QStringLiteral("restic") : found;
}

void SetRestic(const QString &restic) {
  auto settings = GetSettings();
  settings->setValue("Settings/restic", restic.trimmed());
}

bool EnsureResticPassword(const ResticRepo &repo, QWidget *parent) {
  if (!repo.passwordCommand.isEmpty()) {
    return true;
  }
  if (passwordCache().contains(repo.repository)) {
    return true;
  }

  bool ok = false;
  const QString password = QInputDialog::getText(
      parent, "Restic",
      QString("Enter password for restic repository:\n%1").arg(repo.repository),
      QLineEdit::Password, QString(), &ok);

  if (!ok) {
    return false;
  }

  passwordCache().insert(repo.repository, password);
  return true;
}

void ForgetResticPassword(const ResticRepo &repo) {
  passwordCache().remove(repo.repository);
}

void ApplyResticEnvironment(QProcess *process, const ResticRepo &repo) {
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

  env.insert("RESTIC_REPOSITORY", repo.repository);

  if (!repo.passwordCommand.isEmpty()) {
    // restic splits RESTIC_PASSWORD_COMMAND into argv and execs it directly --
    // there is no shell, so "$USER" is passed through literally and pipes are
    // meaningless. The obvious keychain incantation,
    //
    //     security find-generic-password -a "$USER" -s NAME -w
    //
    // therefore looks up an account named "$USER" and fails with exit 44,
    // "item not found". Running it through sh gives the field the semantics
    // anyone would assume it has.
    env.insert("RESTIC_PASSWORD_COMMAND",
               "sh -c " + ShellQuote(repo.passwordCommand));
  } else {
    auto it = passwordCache().find(repo.repository);
    if (it != passwordCache().end()) {
      env.insert("RESTIC_PASSWORD", it.value());
    }
  }

  // An "rclone:" repository shells out to rclone, which needs the same config
  // file and config password this app already uses. GetRcloneConf() returns
  // {"--config", path}; rclone reads the same path from RCLONE_CONFIG.
  const QStringList conf = GetRcloneConf();
  if (conf.size() == 2) {
    env.insert("RCLONE_CONFIG", conf[1]);
  }

  process->setProcessEnvironment(env);

  // Carries RCLONE_CONFIG_PASS when the rclone config is encrypted. Called
  // last because it rebuilds the environment from the current one.
  UseRclonePassword(process);
}

QStringList ResticBaseArgs(const ResticRepo &repo) {
  // Browsing is read-only, so skip the lock: it keeps the repository usable
  // when it is append-only or mounted read-only, and avoids leaving stale
  // locks behind if the app is killed mid-listing.
  QStringList args;
  args << "--no-lock";

  if (repo.repository.startsWith("rclone:")) {
    args << "-o" << "rclone.program=" + GetRclone();
  }

  return args;
}
