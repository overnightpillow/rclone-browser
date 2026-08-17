#pragma once

#include "pch.h"

// Paths on a remote are not filesystem paths, and QDir is the wrong tool for
// them even though it looks like the right one.
//
// QDir in Qt 6 leaves most of a path alone -- "a//b" and "a/../b" survive
// setPath() intact -- but three of its habits do damage here:
//
//   * an empty path becomes ".", so the root of a remote listed as "" turns
//     into "." and every child below it into "./name";
//   * a trailing separator is dropped, which is harmless but not ours to do;
//   * on Windows a backslash is a separator, so an object legitimately named
//     "a\b" on S3 or B2 would be split into a directory "a" and a child "b".
//
// The joins below do only what rclone means by a path: components separated
// by "/", with no interpretation of the names themselves.

// Joins a child onto a parent remote path. An empty parent means the root of
// the remote, and yields the child unchanged rather than "./child".
QString JoinRemotePath(const QString &parentPath, const QString &childName);

// The path of a child from an "rclone lsjson" entry, given its parent's path.
//
// lsjson reports a Path for every entry, which is what rclone itself would
// accept back, so it is preferred over rebuilding parent + Name. They agree
// on ordinary backends; they do not on ones where an entry does not live
// where its name suggests, such as Google Photos albums.
QString ChildRemotePath(const QString &parentPath, const QString &entryPath,
                        const QString &entryName);

// The last component of a remote path -- QDir::dirName() for something that is
// not a directory on this machine. Empty for the root of a remote.
QString RemotePathName(const QString &path);
