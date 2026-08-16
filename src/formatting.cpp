#include "formatting.h"

QString FormatSize(quint64 size) {
  static const char prefix[] = " KMGTPE";
  for (int i = sizeof(prefix) - 2; i >= 1; i--) {
    const quint64 base = quint64(1) << (i * 10);
    if (size >= 10 * base) {
      return QString("%1 %2").arg(size / base).arg(QChar(prefix[i]));
    }
  }
  // The original loop ran down to i == 0 with the same ">= 10 * base" test, so
  // any size below 10 bytes fell through and rendered as the literal "0".
  return QString::number(size);
}

QString FormatModTime(const QString &rfc3339) {
  if (rfc3339.isEmpty()) {
    return QString();
  }

  // Qt parses at most millisecond precision; rclone and restic emit
  // nanoseconds.
  QString trimmed = rfc3339;
  static const QRegularExpression subSecond(R"(\.(\d{3})\d*)");
  trimmed.replace(subSecond, ".\\1");

  const QDateTime parsed = QDateTime::fromString(trimmed, Qt::ISODateWithMs);
  if (!parsed.isValid()) {
    return rfc3339;
  }

  // Rendered in a zone behind UTC the sentinel reads as "1999-12-31", which
  // looks like a decoding bug rather than missing data.
  static const QDateTime unknownBefore(QDate(2000, 1, 1), QTime(0, 0),
                                       QTimeZone::UTC);
  if (parsed.toUTC() <= unknownBefore) {
    return QString();
  }

  return parsed.toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
}
