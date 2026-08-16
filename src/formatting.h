#pragma once

#include "pch.h"

// Display formatting shared by the rclone and restic browsers. Both models had
// their own copy of these, in anonymous namespaces where nothing could reach
// them -- including tests.

// Human-readable byte count: "1023", "12 K", "3 M". Sizes below 10 KiB are
// reported exactly.
QString FormatSize(quint64 size);

// RFC3339 (as emitted by "rclone lsjson" and "restic ls") to a fixed-width
// local time, "yyyy-MM-dd HH:mm:ss". Fixed width matters because the Modified
// column is sorted as a string.
//
// Returns an empty string when the backend has no real timestamp to give:
// directories on B2 and S3 are synthetic and rclone reports a sentinel of
// 2000-01-01T00:00:00Z for them. Anything at or before that sentinel is
// treated as unknown.
//
// An unparseable input is returned unchanged rather than discarded.
QString FormatModTime(const QString &rfc3339);
