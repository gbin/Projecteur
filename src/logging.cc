// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "logging.h"

#include <QDateTime>
#include <QList>
#include <QMetaMethod>
#include <QPlainTextEdit>
#include <QPointer>
#include <QString>

#include <iostream>

namespace {
  // -----------------------------------------------------------------------------------------------
  void projecteurLogHandler(QtMsgType type, const QMessageLogContext &context, const QString &msgQString);
  void categoryFilterInfo(QLoggingCategory *category);

  // Install our custom message handler, store previous message handler
  const QtMessageHandler defaultMessageHandler = qInstallMessageHandler(projecteurLogHandler);
  QLoggingCategory::CategoryFilter defaultCategoryFilter = nullptr;
  QLoggingCategory::CategoryFilter currentCategoryFilter = categoryFilterInfo;

  constexpr char categoryPrefix[] = "projecteur.";
  inline bool isAppCategory(QLoggingCategory* category) {
    return (qstrncmp(categoryPrefix, category->categoryName(), sizeof(categoryPrefix)-1) == 0);
  }

  void initLogging()
  {
    if (defaultCategoryFilter == nullptr)
    {
      defaultCategoryFilter = QLoggingCategory::installFilter(categoryFilterInfo);
    }
  }

  void categoryFilterDebug(QLoggingCategory *category)
  {
    initLogging();
    if (isAppCategory(category))
    {
      category->setEnabled(QtDebugMsg, true);
      category->setEnabled(QtInfoMsg, true);
      category->setEnabled(QtWarningMsg, true);
      category->setEnabled(QtCriticalMsg, true);
    } else {
      defaultCategoryFilter(category);
    }
  }

  void categoryFilterInfo(QLoggingCategory *category)
  {
    initLogging();
    if (isAppCategory(category)) {
      category->setEnabled(QtDebugMsg, false);
      category->setEnabled(QtInfoMsg, true);
      category->setEnabled(QtWarningMsg, true);
      category->setEnabled(QtCriticalMsg, true);
    } else {
      defaultCategoryFilter(category);
    }
  }

  void categoryFilterWarning(QLoggingCategory *category)
  {
    initLogging();
    if (isAppCategory(category)) {
      category->setEnabled(QtDebugMsg, false);
      category->setEnabled(QtInfoMsg, false);
      category->setEnabled(QtWarningMsg, true);
      category->setEnabled(QtCriticalMsg, true);
    } else {
      defaultCategoryFilter(category);
    }
  }

  void categoryFilterError(QLoggingCategory *category)
  {
    initLogging();
    if (isAppCategory(category))
    {
      category->setEnabled(QtDebugMsg, false);
      category->setEnabled(QtInfoMsg, false);
      category->setEnabled(QtWarningMsg, false);
      category->setEnabled(QtCriticalMsg, true);
    } else {
      defaultCategoryFilter(category);
    }
  }

  // -----------------------------------------------------------------------------------------------
  QPointer<QPlainTextEdit> logPlainTextEdit;
  QMetaMethod logAppendMetaMethod;
  QList<QString> logPlainTextCache; // log messages are stored here until a text edit is registered
  constexpr int logPlainTextCacheMax = 1000;

  // -----------------------------------------------------------------------------------------------
  void logToTextEdit(const QString& logMsg)
  {
    if (logPlainTextEdit) {
      logAppendMetaMethod.invoke(logPlainTextEdit, Qt::QueuedConnection, Q_ARG(QString, logMsg));
    } else if (logPlainTextCache.size() < logPlainTextCacheMax) {
      logPlainTextCache.push_back(logMsg);
    }
  }

  // -----------------------------------------------------------------------------------------------
  inline const char* typeToShortString(QtMsgType type) {
    switch (type) {
      case QtDebugMsg: return "dbg";
      case QtInfoMsg: return "inf";
      case QtWarningMsg: return "wrn";
      case QtCriticalMsg: return "err";
      case QtFatalMsg: return "fat";
    }
    return "";
  }

  // -----------------------------------------------------------------------------------------------
  // Currently all logging is done from within the Qt Gui thread
  // - if that changes and multiple threads will log, this needs a serious overhaul - NOT thread safe
  void projecteurLogHandler(QtMsgType type, const QMessageLogContext &context, const QString &msgQString)
  {
    const char *category = context.category ? context.category : "";

    #if (QT_VERSION >= QT_VERSION_CHECK(5, 8, 0))
      constexpr auto dateFormat = Qt::ISODateWithMs;
    #else
      constexpr auto dateFormat = Qt::ISODate;
    #endif

    const auto logMsg = QString("[%1][%2][%3] %4").arg(QDateTime::currentDateTime().toString(dateFormat),
                                                       typeToShortString(type), category, msgQString);

    if (type == QtDebugMsg || type == QtInfoMsg) {
      std::cout << qUtf8Printable(logMsg) << std::endl;
    } else {
      std::cerr << qUtf8Printable(logMsg) << std::endl;
    }

    logToTextEdit(logMsg);
  }
} // end anonymous namespace

namespace logging {
  void registerTextEdit(QPlainTextEdit* textEdit)
  {
    logPlainTextEdit = textEdit;
    if (!logPlainTextEdit) { return; }

    const auto index = logPlainTextEdit->metaObject()->indexOfMethod("appendPlainText(QString)");
    logAppendMetaMethod = logPlainTextEdit->metaObject()->method(index);

    for (const auto& logMsg : logPlainTextCache) {
      logAppendMetaMethod.invoke(logPlainTextEdit, Qt::QueuedConnection, Q_ARG(QString, logMsg));
    }

    logPlainTextCache.clear();
  }

  const char* levelToString(level lvl)
  {
    switch (lvl) {
      case level::debug: return "debug";
      case level::info: return "info";
      case level::warning: return "warning";
      case level::error: return "error";
      case level::custom: return "default/custom";
      case level::unknown: return "unknown";
    }
    return "";
  }

  level levelFromName(const QString& name)
  {
    const auto lvlName = name.toLower();
    if (lvlName == "dbg" || lvlName == "debug") { return level::debug; }
    if (lvlName == "inf" || lvlName == "info") { return level::info; }
    if (lvlName == "wrn" || lvlName == "warning") { return level::warning; }
    if (lvlName == "err" || lvlName == "error") { return level::error; }
    return level::unknown;
  }

  level currentLevel()
  {
    initLogging();
    if (currentCategoryFilter == defaultCategoryFilter) { return level::custom; }
    if (currentCategoryFilter == categoryFilterDebug) { return level::debug; }
    if (currentCategoryFilter == categoryFilterInfo) { return level::info; }
    if (currentCategoryFilter == categoryFilterWarning) { return level::warning; }
    if (currentCategoryFilter == categoryFilterError) { return level::error; }
    return level::unknown;
  }

  void setCurrentLevel(level lvl)
  {
    initLogging();

    QLoggingCategory::CategoryFilter newFilter = currentCategoryFilter;

    if (lvl == level::debug) {
      newFilter = categoryFilterDebug;
    } else if (lvl == level::info) {
      newFilter = categoryFilterInfo;
    } else if (lvl == level::warning) {
      newFilter = categoryFilterWarning;
    } else if (lvl == level::error) {
      newFilter = categoryFilterError;
    } else if (lvl == level::custom) {
      newFilter = defaultCategoryFilter;
    }

    if (newFilter != currentCategoryFilter) {
      QLoggingCategory::installFilter(newFilter);
      currentCategoryFilter = newFilter;
    }
  }

  QString hexId(uint16_t id) {
    return QString("%1").arg(id, 4, 16, QChar('0'));
  }
} // end namespace logging
