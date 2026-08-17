#include "remote_path.h"

QString JoinRemotePath(const QString &parentPath, const QString &childName) {
  if (childName.isEmpty()) {
    return parentPath;
  }

  // The root of a remote is the empty path, and stays that way: "remote:name"
  // is what rclone wants, not "remote:./name".
  if (parentPath.isEmpty() || parentPath == ".") {
    return childName;
  }

  if (parentPath.endsWith('/')) {
    return parentPath + childName;
  }

  return parentPath + "/" + childName;
}

QString RemotePathName(const QString &path) {
  QString trimmed = path;
  while (trimmed.endsWith('/')) {
    trimmed.chop(1);
  }

  const int slash = trimmed.lastIndexOf('/');
  return slash < 0 ? trimmed : trimmed.mid(slash + 1);
}

QString ChildRemotePath(const QString &parentPath, const QString &entryPath,
                        const QString &entryName) {
  const QString child = entryPath.isEmpty() ? entryName : entryPath;

  // lsjson reports Path relative to the directory that was listed, so for a
  // plain backend it is just the name again. Where it is not -- a Google
  // Photos album entry names a file that lives further down -- it already
  // carries the part below the parent, and must not be joined twice.
  if (child == parentPath || child.startsWith(parentPath + "/")) {
    return child;
  }

  return JoinRemotePath(parentPath, child);
}
