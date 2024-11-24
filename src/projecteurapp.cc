// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "projecteurapp.h"

#include "aboutdlg.h"
#include "device-command-helper.h"
#include "imageitem.h"
#include "linuxdesktop.h"
#include "logging.h"
#include "preferencesdlg.h"
#include "settings.h"
#include "spotlight.h"

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
#include <QDesktopWidget>
#endif

#include <QFontDatabase>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickWindow>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QWindow>

LOGGING_CATEGORY(mainapp, "mainapp")
LOGGING_CATEGORY(cmdclient, "cmdclient")
LOGGING_CATEGORY(cmdserver, "cmdserver")

namespace {
  QString localServerName() {
    return QCoreApplication::applicationName() + "_local_socket";
  }
} // end anonymous namespace

// -------------------------------------------------------------------------------------------------
ProjecteurApplication::ProjecteurApplication(int &argc, char **argv, const Options& options)
  : QApplication(argc, argv)
  , m_trayIcon(new QSystemTrayIcon())
  , m_trayMenu(new QMenu())
  , m_localServer(new QLocalServer(this))
  , m_linuxDesktop(new LinuxDesktop(this))
  , m_xcbOnWayland(QGuiApplication::platformName() == "xcb" && m_linuxDesktop->isWayland())
{
  if (screens().empty())
  {
    const auto title = tr("No Screens detected");
    const auto text = tr("screens().size() returned a size < 1. Exiting.");
    logError(mainapp) << title << ";" << text;
    QMessageBox::critical(nullptr, title, text);
    QTimer::singleShot(0, this, [this](){ this->exit(2); });
    return;
  }

  // don't quit application when last windows (usually preferences dialog) is closed
  setQuitOnLastWindowClosed(false);
  QFontDatabase::addApplicationFont(":/icons/projecteur-icons.ttf");

  m_settings = options.configFile.isEmpty() ? new Settings(this)
                                            : new Settings(options.configFile, this);
  m_spotlight = new Spotlight(this, Spotlight::Options{options.enableUInput, options.additionalDevices},
                              m_settings);

  m_deviceCommandHelper = new DeviceCommandHelper(this, m_spotlight);

  m_settings->setOverlayDisabled(options.disableOverlay);
  m_dialog = std::make_unique<PreferencesDialog>(m_settings, m_spotlight,
                                                  options.dialogMinimizeOnly
                                                  ? PreferencesDialog::Mode::MinimizeOnlyDialog
                                                  : PreferencesDialog::Mode::ClosableDialog);

  connect(&*m_dialog, &PreferencesDialog::testButtonClicked, this, [this](){
    m_spotlight->setSpotActive(true);
  });

  const QString desktopEnv = m_linuxDesktop->type() == LinuxDesktop::Type::KDE ? "KDE" :
                             m_linuxDesktop->type() == LinuxDesktop::Type::Gnome ? "Gnome"
                                                                                 : tr("Unknown");

  logDebug(mainapp) << tr("Qt platform plugin: %1;").arg(QGuiApplication::platformName())
                    << tr("Desktop Environment: %1;").arg(desktopEnv)
                    << tr("Wayland: %1").arg(m_linuxDesktop->isWayland() ? "true" : "false");

  if (m_xcbOnWayland) {
    logWarning(mainapp) << tr("Qt 'xcb' platform and Wayland session detected.");
  }

  if (options.showPreferencesOnStart || m_linuxDesktop->isWayland()) {
    QTimer::singleShot(0, this, [this](){ showPreferences(true); });
  }
  else if (options.dialogMinimizeOnly) {
    QTimer::singleShot(0, this, [this](){ m_dialog->show(); m_dialog->showMinimized(); });
  }

  // Create qml engine and register context properties
  m_qmlEngine = new QQmlApplicationEngine(this);
  m_qmlEngine->rootContext()->setContextProperty("Settings", m_settings);
  m_qmlEngine->rootContext()->setContextProperty("PreferencesDialog", &*m_dialog);
  m_qmlEngine->rootContext()->setContextProperty("ProjecteurApp", this);

  // Create qml overlay window component
  m_windowQmlComponent = new QQmlComponent(m_qmlEngine, QUrl(QStringLiteral("qrc:/main.qml")), m_qmlEngine);
  if (m_windowQmlComponent->status() != QQmlComponent::Status::Ready) {
    const auto title = tr("Overlay window error.");
    const auto text = tr("Qml component has status '%1'. Exiting.").arg(m_windowQmlComponent->status());

    logError(mainapp) << title << ";" << text;
    for (const auto& error : m_windowQmlComponent->errors()) {
      logError(mainapp) << error.toString();
    }

    QMessageBox::critical(nullptr, title, text);
    QTimer::singleShot(0, this, [this](){ this->exit(2); });
    return;
  }

  // Setup screen overlay windows
  setupScreenOverlays();

  // React to multi-screen and overlay disabled changes in settings.
  connect(m_settings, &Settings::multiScreenOverlayEnabledChanged, this, [this](){ setupScreenOverlays(); });
  connect(m_settings, &Settings::overlayDisabledChanged, this, [this](bool disabled){
    if (disabled) {
      if (m_spotlight->spotActive()) { m_spotlight->setSpotActive(false); }
      else { emit m_spotlight->spotActiveChanged(false); }
    }
    else {
      QTimer::singleShot(0, this, [this](){
        if (m_spotlight->spotActive()) {
          emit m_spotlight->spotActiveChanged(true);
        } else {
          m_spotlight->setSpotActive(true);
        }
      });
    }
  });

  // Re-setup screen overlay(s) when a screen is added or removed
  connect(this, &ProjecteurApplication::screenAdded, this, [this](){ setupScreenOverlays(); });
  connect(this, &ProjecteurApplication::screenRemoved, this, [this](){ setupScreenOverlays(); });

  // Setup the tray icon and menu
  setupTrayIcon(options);

  connect(this, &ProjecteurApplication::aboutToQuit, this, [this](){
    for (const auto window : m_overlayWindows) { window->close(); }
    m_overlayWindows.clear();
  });

  // Setup the spotlight connections.
  setupSpotlight();

  // Open local server for local IPC commands, e.g. from other command line instances
  QLocalServer::removeServer(localServerName());
  if (m_localServer->listen(localServerName()))
  {
    connect(m_localServer, &QLocalServer::newConnection, this, [this]()
    {
      while(QLocalSocket *clientConnection = m_localServer->nextPendingConnection())
      {
        connect(clientConnection, &QLocalSocket::readyRead, this, [this, clientConnection]() {
          this->readCommand(clientConnection);
        });
        connect(clientConnection, &QLocalSocket::disconnected, this, [this, clientConnection]() {
          const auto it = m_commandConnections.find(clientConnection);
          if (it != m_commandConnections.end())
          {
            quint32& commandSize = it->second;
            while (clientConnection->bytesAvailable() && commandSize <= clientConnection->bytesAvailable()) {
              this->readCommand(clientConnection);
            }
            m_commandConnections.erase(it);
          }
          clientConnection->close();
          clientConnection->deleteLater();
        });

        // Timeout timer - if after 5 seconds the connection is still open just disconnect...
        const auto clientConnPtr = QPointer<QLocalSocket>(clientConnection);
        QTimer::singleShot(5000, clientConnection, [clientConnPtr](){
          if (clientConnPtr) {
            // time out
            clientConnPtr->disconnectFromServer();
          }
        });

        m_commandConnections.emplace(clientConnection, 0);
      }
    });
  }
  else
  {
    logError(cmdserver) << tr("Error starting local socket for inter-process communication.");
  }
}

// -------------------------------------------------------------------------------------------------
ProjecteurApplication::~ProjecteurApplication()
{
  if (m_localServer) { m_localServer->close(); }
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::setupSpotlight()
{
  // Handling of spotlight window when mouse move events from spotlight device are detected
  connect(m_spotlight, &Spotlight::spotActiveChanged, this,
  [this](bool active)
  {
    if (active && !m_settings->overlayDisabled())
    {
      if (!m_settings->multiScreenOverlayEnabled()) { setScreenForCursorPos(); }

      for (const auto window : m_overlayWindows)
      {
        window->setFlags(window->flags() | Qt::WindowStaysOnTopHint);
        window->setFlags(window->flags() & ~Qt::SplashScreen);
        window->setFlags(window->flags() | Qt::ToolTip);
        window->setFlags(window->flags() & ~Qt::WindowTransparentForInput);

        if (window->screen())
        {
          if (m_settings->zoomEnabled()) {
            window->setProperty("desktopPixmap", m_linuxDesktop->grabScreen(window->screen()));
          }

          const auto screenGeometry = window->screen()->geometry();
          if (window->geometry() != screenGeometry) {
            window->setGeometry(screenGeometry);
          }
          window->setPosition(screenGeometry.topLeft());
        }
        window->showFullScreen();
        window->raise();
      }
      m_overlayVisible = true;
      emit overlayVisibleChanged(true);
    }
    else
    {
      m_overlayVisible = false;
      emit overlayVisibleChanged(false);
      for (const auto window : m_overlayWindows)
      {
        window->setFlags(window->flags() | Qt::WindowTransparentForInput);
        window->setFlags(window->flags() & ~Qt::WindowStaysOnTopHint);
        // Workaround for 'xcb' on Wayland session (default on Ubuntu)
        // .. the window in that case is not transparent for inputs and cannot be clicked through.
        // --> hide the window, although animations will not be visible
        if (m_xcbOnWayland) { window->hide(); }
      }
      if (m_xcbOnWayland && m_dialog->mode() == PreferencesDialog::Mode::MinimizeOnlyDialog
                         && m_dialog->isMinimized()) { // keep Window minimized...
        //Workaround for QTBUG-76354 (https://bugreports.qt.io/browse/QTBUG-76354)
        m_dialog->showNormal();
        m_dialog->setWindowState(Qt::WindowMinimized);
      }
    }
  });

  connect(m_spotlight, &Spotlight::spotActiveChanged, this, [this](bool active){
    if (!active && m_dialog->isVisible()) {
      m_dialog->raise();
      m_dialog->activateWindow();
    }
  });
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::setupTrayIcon(Options const& options)
{
  // add and connect 'Preferences' tray menu action
  const auto actionPref = m_trayMenu->addAction(tr("&Preferences..."));
  connect(actionPref, &QAction::triggered, this, [this](){
    this->showPreferences(true);
  });

  // add and and connect 'About' tray menu action
  const auto actionAbout = m_trayMenu->addAction(tr("&About"));
  connect(actionAbout, &QAction::triggered, this, [this]()
  {
    if (!m_aboutDialog) {
      m_aboutDialog = new AboutDialog();
      connect(m_aboutDialog, &QDialog::finished, this, [this](int /* result */) {
        m_aboutDialog->deleteLater(); // No need to keep about dialog in memory, not that important
      });
    }

    if (m_aboutDialog->isVisible()) {
      m_aboutDialog->show();
      m_aboutDialog->raise();
      m_aboutDialog->activateWindow();
    } else {
      m_aboutDialog->open();
    }
  });

  m_trayMenu->addSeparator();
  const auto actionQuit = m_trayMenu->addAction(tr("&Quit"));
  connect(actionQuit, &QAction::triggered, this, [this](){
    m_qmlEngine->deleteLater(); // see: https://bugreports.qt.io/browse/QTBUG-81247
    this->quit();
  });
  m_trayIcon->setContextMenu(&*m_trayMenu);

  m_trayIcon->setIcon(QIcon(":/icons/projecteur-tray-64.png"));
  m_trayIcon->show();

  connect(&*m_trayIcon, &QSystemTrayIcon::activated, this,
  [this](QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger)
    {
      const auto trayGeometry = m_trayIcon->geometry();
      // This usually won't give us a valid geometry, since Qt isn't drawing the tray icon itself
      if (trayGeometry.isValid()) {
        m_trayIcon->contextMenu()->popup(m_trayIcon->geometry().center());
      } else {
        // It's tricky to get the same behavior on all desktop environments. While on GNOME3
        // it behaves as one (or most) would expect, it behaves differently on other Desktop
        // environments.
        // QSystemTrayIcon is a wrapper around the StatusNotfierItem on modern (Linux) Desktops
        // see: https://www.freedesktop.org/wiki/Specifications/StatusNotifierItem/
        // Via the Qt API there is not much control over how e.g. KDE or GNOME show the icon
        // and how it behaves.. e.g. setting something like
        // org.freedesktop.StatusNotifierItem.ItemIsMenu to True would be good for KDE Plasma
        // see: https://www.freedesktop.org/wiki/Specifications/StatusNotifierItem/StatusNotifierItem/
        this->showPreferences(true);
      }
    }
  });

  connect(&*m_dialog, &PreferencesDialog::exitApplicationRequested, actionQuit, [actionQuit]() {
    logDebug(mainapp) << tr("Exit request from preferences dialog.");
    actionQuit->trigger();
  });

  m_trayIcon->setVisible(!options.hideSysTrayIcon);
}

// -------------------------------------------------------------------------------------------------
QWindow* ProjecteurApplication::createOverlayWindow()
{
  QObject *object = m_windowQmlComponent->create();
  object->setParent(m_qmlEngine);
  const auto window = qobject_cast<QWindow*>(object);
  window->setFlags(window->flags() | Qt::WindowTransparentForInput | Qt::Tool);
  return window;
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::spotlightWindowClicked()
{
  m_spotlight->setSpotActive(false);
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::cursorExitedWindow()
{
  if (m_spotlight->spotActive() && !m_settings->multiScreenOverlayEnabled()) { setScreenForCursorPos(); }
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::cursorEntered(quint64 screen)
{
  setCurrentSpotScreen(screen);
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::cursorPositionChanged(const QPoint& pos)
{
  setCurrentCursorPos(pos);
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::updateOverlayWindow(QWindow* window, QScreen* screen)
{
  if (screen == nullptr) {
    return;
  }

  if (window->screen() == screen && screen->geometry() == window->geometry()) {
    return;
  }

  window->setProperty("screenId", quint64(screen));

  const bool wasVisible = window->isVisible();
  const bool wasSpotActive = m_spotlight->spotActive();

  m_overlayVisible = false;
  emit overlayVisibleChanged(false);

  window->setFlags(window->flags() | Qt::WindowTransparentForInput);
  window->setFlags(window->flags() & ~Qt::WindowStaysOnTopHint);
  window->hide();

  window->setGeometry(QRect(screen->geometry().topLeft(), QSize(300,200)));
  window->setScreen(screen);
  window->setGeometry(screen->geometry());

  if (m_xcbOnWayland && !wasVisible)
  {
    if (m_dialog->mode() == PreferencesDialog::Mode::MinimizeOnlyDialog
        && m_dialog->isMinimized()) { // keep Window minimized...
      //Workaround for QTBUG-76354 (https://bugreports.qt.io/browse/QTBUG-76354)
      m_dialog->showNormal();
      m_dialog->setWindowState(Qt::WindowMinimized);
    }
  }

  if (wasVisible && wasSpotActive) {
    QTimer::singleShot(0, this, [this](){
      if (m_spotlight->spotActive()) {
        emit m_spotlight->spotActiveChanged(true);
      } else {
        m_spotlight->setSpotActive(true);
      }
    });
  }
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::setScreenForCursorPos()
{
  updateOverlayWindow(m_overlayWindows.first(), screenAtCursorPos());
}

// -------------------------------------------------------------------------------------------------
QScreen* ProjecteurApplication::screenAtCursorPos() const
{
#if (QT_VERSION >= QT_VERSION_CHECK(5, 10, 0))
  return this->screenAt(QCursor::pos());
#else
  const int screenNumber = this->desktop()->screenNumber(QCursor::pos());
  const auto screenList = screens();
  if (screenNumber >= 0 && screenNumber < screenList.size()) {
    return screenList[screenNumber];
  }
  return nullptr;
#endif
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::setupScreenOverlays()
{
  m_screenWindowMap.clear();

  const auto currentScreens = screens();
  if (currentScreens.empty())
  {
    for (const auto window : m_overlayWindows) { window->deleteLater(); }
    m_overlayWindows.clear();
    return;
  }

  // disconnect any connected screen signals previously connected to `this`
  // and connect to geometryChanged signal to update overlay windows on screen geometry changes
  for (const auto screen : currentScreens) {
    disconnect(screen, nullptr, this, nullptr);
    connect(screen, &QScreen::geometryChanged, this, [this, screen]()
    {
      if (m_settings->multiScreenOverlayEnabled())
      {
        const auto it = m_screenWindowMap.find(screen);
        if (it == m_screenWindowMap.cend()) { return; }
        updateOverlayWindow(it->second, it->first);
      }
      else {
        setScreenForCursorPos();
      }
    });
  }

  // Adapt number of overlay windows depending on multiScreenOverlayEnabled() and
  // the number of screens
  const int numOverlayWindows = m_settings->multiScreenOverlayEnabled() ? currentScreens.size() : 1;
  const bool wasSpotActive = m_spotlight->spotActive();

  while (m_overlayWindows.size() > numOverlayWindows) {
    m_overlayWindows.back()->deleteLater();
    m_overlayWindows.pop_back();
 }

  while (m_overlayWindows.size() < numOverlayWindows) {
    m_overlayWindows.push_back(createOverlayWindow());
  }

  // Default behavior - only one overlay window that is moved across sreens
  if (!m_settings->multiScreenOverlayEnabled())
  {
    for (const auto screen : currentScreens) {
      m_screenWindowMap[screen] = m_overlayWindows.front();
    }
  }
  else
  { // multi-screen overlays enabled: assign overlay windows to screens
    auto wit = m_overlayWindows.cbegin();
    for (const auto screen : currentScreens) {
      m_screenWindowMap[screen] = (*wit);
      updateOverlayWindow(*wit, screen);
      ++wit;
    }
  }

  // If the spotlight was active was active when calling the setup function,
  // make sure it will be activated again.
  if (wasSpotActive) {
    QTimer::singleShot(0, this, [this](){
      if (m_spotlight->spotActive()) {
        emit m_spotlight->spotActiveChanged(true);
      } else {
        m_spotlight->setSpotActive(true);
      }
    });
  }
}

// -------------------------------------------------------------------------------------------------
quint64 ProjecteurApplication::currentSpotScreen() const
{
  return m_currentSpotScreen;
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::setCurrentSpotScreen(quint64 screen)
{
  if (m_currentSpotScreen == screen) { return; }
  m_currentSpotScreen = screen;
  emit currentSpotScreenChanged(m_currentSpotScreen);
}

// -------------------------------------------------------------------------------------------------
QPoint ProjecteurApplication::currentCursorPos() const
{
  return m_currentCursorPos;
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::setCurrentCursorPos(const QPoint& pos)
{
  if (pos == m_currentCursorPos) { return; }
  m_currentCursorPos = pos;
  emit currentCursorPosChanged(m_currentCursorPos);
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::readCommand(QLocalSocket* clientConnection)
{
  auto it = m_commandConnections.find(clientConnection);
  if (it == m_commandConnections.end()) {
    return;
  }

  quint32& commandSize = it->second;

  // Read size of command (always quint32) if not already done.
  if (commandSize == 0) {
    if (clientConnection->bytesAvailable() < static_cast<int>(sizeof(quint32))) {
      return;
    }

    QDataStream in(clientConnection);
    in >> commandSize;

    if (commandSize > 256)
    {
      logWarning(cmdserver) << tr("Received invalid command size (%1)").arg(commandSize);
      clientConnection->disconnectFromServer();
      return ;
    }
  }

  if (clientConnection->bytesAvailable() < commandSize || clientConnection->atEnd()) {
    return;
  }

  const auto command = QString::fromLocal8Bit(clientConnection->read(commandSize));
  const QString cmdKey = command.section('=', 0, 0).trimmed();
  const QString cmdValue = command.section('=', 1).trimmed();

  if (cmdKey == "quit")
  {
    logDebug(cmdserver) << tr("Received quit command.");
    this->quit();
  }
  else if (cmdKey == "vibrate") // with args intensity (0-255), length (0-10)
  {
    #if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
      auto const args = cmdValue.split(QLatin1Char(','), Qt::SkipEmptyParts);
    #else
      auto const args = cmdValue.split(QLatin1Char(','), QString::SkipEmptyParts);
    #endif

    std::uint8_t const intensity = [&args]{
      if (args.size() >= 1) {
        bool ok = false;
        auto intensity = args[0].toInt(&ok);
        if (ok) {
          return static_cast<std::uint8_t>(qMin(255, qMax(0, intensity)));
        }
      }
      return std::uint8_t{128};
    }();

    std::uint8_t const length = [&args]{
      if (args.size() >= 2) {
        bool ok = false;
        auto intensity = args[1].toInt(&ok);
        if (ok) {
          return static_cast<std::uint8_t>(qMin(10, qMax(0, intensity)));
        }
      }
      return std::uint8_t{0};
    }();

    logDebug(cmdserver) << tr("Received command vibrate = intensity:%1, length:%2")
                              .arg(intensity)
                              .arg(length);

    m_deviceCommandHelper->sendVibrateCommand(intensity, length);
  }
  else if (cmdKey == "spot.size.adjust")
  {
    bool ok = false;
    int const sizeAdjust = cmdValue.toInt(&ok);
    if (ok) {
      logDebug(cmdserver) << tr("Received command spot.size.adjust = %1%2")
                               .arg(sizeAdjust > 0 ? "+" : "")
                               .arg(sizeAdjust);
      m_settings->setSpotSize(m_settings->spotSize() + sizeAdjust);
    } else {
      logDebug(cmdserver) << tr("Received invalid value for command spot.size.adjust");
    }
  }
  else if (cmdKey == "spot")
  {
    if (cmdValue.isEmpty()) {
      logDebug(cmdserver) << tr("Received empty command value for command spot");
    } else if (cmdValue.toLower() == "toggle") {
      m_spotlight->setSpotActive(!m_spotlight->spotActive());
    }
    else {
      const bool active = (cmdValue.toLower() == "on"
                            || cmdValue == "1"
                            || cmdValue.toLower() == "true");
      logDebug(cmdserver) << tr("Received command spot = %1").arg(active);
      m_spotlight->setSpotActive(active);
    }
  }
  else if (cmdKey == "settings" || cmdKey == "preferences")
  {
    const bool show = !(cmdValue.toLower() == "hide" || cmdValue == "0");
    logDebug(cmdserver) << tr("Received command settings = %1").arg(show);
    showPreferences(show);
  }
  else if (cmdKey == "preset")
  {
    logDebug(cmdserver) << tr("Received command preset = %1").arg(cmdValue);
    if (!cmdValue.isEmpty()) { m_settings->loadPreset(cmdValue); }
  }
  else if (cmdValue.size())
  {
    const auto& properties = m_settings->stringProperties();
    const auto it = std::find_if(properties.cbegin(), properties.cend(),
    [&cmdKey](const auto& pair){
      return (pair.first == cmdKey);
    });
    if (it != m_settings->stringProperties().cend()) {
      logDebug(cmdserver) << tr("Received command '%1'='%2'").arg(cmdKey, cmdValue);
      it->second.setFunction(cmdValue);
    }
    else {
      // string property not found...
      logWarning(cmdserver) << tr("Received unknown command key (%1)").arg(cmdKey);
    }
  }
  // reset command size, for next command
  commandSize = 0;
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::showPreferences(bool show)
{
  if (show)
  {
    m_dialog->show();
    m_dialog->raise();
    static const bool qtPlatformIsWayland = QGuiApplication::platformName().toLower().startsWith("wayland");
    if (!qtPlatformIsWayland) { m_dialog->activateWindow(); }
  }
  else {
    if (m_dialog->mode() == PreferencesDialog::Mode::MinimizeOnlyDialog) {
      m_dialog->showMinimized();
    } else {
      m_dialog->hide();
    }
  }
}

// =================================================================================================
ProjecteurCommandClientApp::ProjecteurCommandClientApp(const QStringList& ipcCommands, int &argc, char **argv)
  : QCoreApplication(argc, argv)
{
  if (ipcCommands.isEmpty())
  {
    QMetaObject::invokeMethod(this, "quit", Qt::QueuedConnection);
    return;
  }

  QLocalSocket* const localSocket = new QLocalSocket(this);

  auto socketErrorFunc = [this, localSocket](QLocalSocket::LocalSocketError /*socketError*/) {
    logError(cmdclient) << tr("Error sending commands: %1", "%1=error message")
                             .arg(localSocket->errorString());
    localSocket->close();
    QMetaObject::invokeMethod(this, "quit", Qt::QueuedConnection);
  };

  #if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
    connect(localSocket, &QLocalSocket::errorOccurred, this, std::move(socketErrorFunc));
  #else
    connect(localSocket,
            static_cast<void (QLocalSocket::*)(QLocalSocket::LocalSocketError)>(&QLocalSocket::error),
            this, std::move(socketErrorFunc));
  #endif

  connect(localSocket, &QLocalSocket::connected, [localSocket, &ipcCommands]()
  {
    for (const auto& ipcCommand : ipcCommands)
    {
      if (ipcCommand.isEmpty()) { continue; }

      const QByteArray commandBlock = [&ipcCommand]()
      {
        const QByteArray ipcBytes = ipcCommand.toLocal8Bit();
        QByteArray block;
        {
          QDataStream out(&block, QIODevice::WriteOnly);
          out << static_cast<quint32>(ipcBytes.size());
        }
        block.append(ipcBytes);
        return block;
      }();

      localSocket->write(commandBlock);
      localSocket->flush();
    }
    localSocket->disconnectFromServer();
  });

  connect(localSocket, &QLocalSocket::disconnected, this, [this, localSocket]() {
    localSocket->close();
    QMetaObject::invokeMethod(this, "quit", Qt::QueuedConnection);
  });

  localSocket->connectToServer(localServerName());
}
