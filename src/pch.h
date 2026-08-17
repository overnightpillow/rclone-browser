#pragma once

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif

#include <memory>

#include <QtCore>
#include <QtDebug>
#include <QtGui>
#include <QtNetwork>
#include <QtWidgets>

#if defined(Q_OS_WIN)
// windows.h defines min and max as macros, which then eat any std::min or
// std::max written anywhere downstream of this header -- the error is a
// syntax error pointing at "(", nowhere near the cause. NOMINMAX is the
// documented way to ask it not to.
#define NOMINMAX
// Trims the parts of the Windows API nothing here uses. Not required, but the
// header is enormous and every translation unit pays for it.
#define WIN32_LEAN_AND_MEAN

// QtWinExtras (removed in Qt6) used to pull these in transitively; icon_cache.cpp
// needs SHGetFileInfoW and CoInitializeEx directly.
#include <windows.h>

#include <objbase.h>
#include <shellapi.h>
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif
