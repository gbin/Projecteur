// This file is part of Projecteur - https://github.com/jahnf/Projecteur
// - See LICENSE.md and README.md
#pragma once

#include <QLoggingCategory>

#define _NARG__(...)  _NARG_I_(__VA_ARGS__,_RSEQ_N())
#define _NARG_I_(...) _ARG_N(__VA_ARGS__)
#define _ARG_N( \
   _1, _2, _3, _4, _5, _6, _7, _8, _9,_10, \
  _11,_12,_13,_14,_15,_16,_17,_18,_19,_20, \
  _21,_22,_23,_24,_25,_26,_27,_28,_29,_30, \
  _31,_32,_33,_34,_35,_36,_37,_38,_39,_40, \
  _41,_42,_43,_44,_45,_46,_47,_48,_49,_50, \
  _51,_52,_53,_54,_55,_56,_57,_58,_59,_60, \
  _61,_62,_63,N,...) N

#define _RSEQ_N() \
 2,2,2,2,             \
 2,2,2,2,2,2,2,2,2,2, \
 2,2,2,2,2,2,2,2,2,2, \
 2,2,2,2,2,2,2,2,2,2, \
 2,2,2,2,2,2,2,2,2,2, \
 2,2,2,2,2,2,2,2,2,2, \
 2,2,2,2,2,2,2,2,1,0

#define _VLOGFUNC_(name, n) name##n
#define _VLOGFUNC(name, n) _VLOGFUNC_(name, n)
#define VLOGFUNC(func, ...) _VLOGFUNC(func, _NARG__(__VA_ARGS__)) (__VA_ARGS__)

// macro 'overloading':
// - call logDebug1 for one argument, logDebug2 for more than one argument (up to 64)
#define logDebug(...) VLOGFUNC(logDebug, __VA_ARGS__)
#define logDebug1(category) qCDebug(category).noquote()
#define logDebug2(...) qCDebug(__VA_ARGS__)

#define logInfo(...) VLOGFUNC(logInfo, __VA_ARGS__)
#define logInfo1(category) qCInfo(category).noquote()
#define logInfo2(...) qCInfo(__VA_ARGS__)

#define logWarn(...) VLOGFUNC(logWarning, __VA_ARGS__)
#define logWarning(...) VLOGFUNC(logWarning, __VA_ARGS__)
#define logWarning1(category) qCWarning(category).noquote()
#define logWarning2(...) qCWarning(__VA_ARGS__)

#define logCritical(...) VLOGFUNC(logError, __VA_ARGS__)
#define logError(...) VLOGFUNC(logError, __VA_ARGS__)
#define logError1(category) qCCritical(category).noquote()
#define logError2(...) qCCritical(__VA_ARGS__)

#define LOGGING_CATEGORY(cat, name) Q_LOGGING_CATEGORY(cat, "projecteur." name)
#define DECLARE_LOGGING_CATEGORY(name) Q_DECLARE_LOGGING_CATEGORY(name)

class QPlainTextEdit;

namespace logging {
  enum class level {
    unknown = -1,
    custom = 0,
    debug = 1,
    info = 2,
    warning = 3,
    error = 4
  };

  const char* levelToString(level lvl);
  level levelFromName(const QString& name);
  level currentLevel();
  void setCurrentLevel(level lvl);

  void registerTextEdit(QPlainTextEdit* textEdit);

  QString hexId(uint16_t id);
}



