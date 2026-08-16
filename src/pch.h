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
// QtWinExtras (removed in Qt6) used to pull these in transitively; icon_cache.cpp
// needs SHGetFileInfoW and CoInitializeEx directly.
#include <windows.h>

#include <objbase.h>
#include <shellapi.h>
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif
