// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "projecteurapp.h"

#include "aboutdlg.h"
#include "device-command-helper.h"
#include "imageitem.h"
#include "linuxdesktop.h"
#include "logging.h"
#include "preferencesdlg.h"
#include "presentationtimer.h"
#include "projecteurcontrol.h"
#include "settings.h"
#include "spotlight.h"

#include <LayerShellQt/Window>

#include <QFontDatabase>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickWindow>
#include <QScreen>
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
  , m_localServer(new QLocalServer(this))
  , m_linuxDesktop(new LinuxDesktop(this))
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
  m_presentationTimer =
    new PresentationTimer(m_settings, m_spotlight, m_deviceCommandHelper, this);

  m_settings->setOverlayDisabled(options.disableOverlay);
  m_dialog = std::make_unique<PreferencesDialog>(m_settings, m_spotlight,
                                                  options.dialogMinimizeOnly
                                                  ? PreferencesDialog::Mode::MinimizeOnlyDialog
                                                  : PreferencesDialog::Mode::ClosableDialog);

  connect(&*m_dialog, &PreferencesDialog::testButtonClicked, this, [this](){
    m_spotlight->setSpotActive(true);
  });
  connect(&*m_dialog, &PreferencesDialog::exitApplicationRequested, this, [this]() {
    logDebug(mainapp) << tr("Exit request from preferences dialog.");
    quit();
  });

  const QString desktopEnv = m_linuxDesktop->type() == LinuxDesktop::Type::KDE
                               ? QStringLiteral("KDE")
                               : tr("Unknown");

  logDebug(mainapp) << tr("Qt platform plugin: %1;").arg(QGuiApplication::platformName())
                    << tr("Desktop Environment: %1;").arg(desktopEnv)
                    << tr("Wayland: %1").arg(m_linuxDesktop->isWayland() ? "true" : "false");

  if (options.showPreferencesOnStart) {
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
  });

  // Re-setup screen overlay(s) when a screen is added or removed
  connect(this, &ProjecteurApplication::screenAdded, this, [this](){ setupScreenOverlays(); });
  connect(this, &ProjecteurApplication::screenRemoved, this, [this](){ setupScreenOverlays(); });

  // Expose application state and actions to the native Plasma system tray applet.
  setupControlService(options);

  connect(this, &ProjecteurApplication::aboutToQuit, this, [this](){
    m_linuxDesktop->setShakeCursorEffectSuppressed(false);
    for (const auto window : m_overlayWindows) { delete window; }
    m_overlayWindows.clear();
    m_screenWindowMap.clear();
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
  if (m_control) { m_control->unregisterService(); }
  if (m_localServer) { m_localServer->close(); }
  for (const auto window : m_overlayWindows) { delete window; }
  m_overlayWindows.clear();
  m_screenWindowMap.clear();
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
      m_linuxDesktop->setShakeCursorEffectSuppressed(true);

      if (!m_settings->multiScreenOverlayEnabled()) { setScreenForCursorPos(); }

      for (const auto window : m_overlayWindows)
      {
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
        window->show();
      }
      m_overlayVisible = true;
      emit overlayVisibleChanged(true);
    }
    else
    {
      m_linuxDesktop->setShakeCursorEffectSuppressed(false);

      m_overlayVisible = false;
      emit overlayVisibleChanged(false);
      for (const auto window : m_overlayWindows)
      {
        QTimer::singleShot(200, window, [this, window]() {
          if (!m_spotlight->spotActive()) {
            window->hide();
          }
        });
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
void ProjecteurApplication::setupControlService(Options const& options)
{
  m_control = new ProjecteurControl(this, m_settings, m_spotlight, m_presentationTimer,
                                    !options.hideSysTrayIcon);
  if (!m_control->registerService()) {
    logError(mainapp) << tr("Could not register the Projecteur session D-Bus service.");
  }
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::showAbout()
{
  if (!m_aboutDialog) {
    m_aboutDialog = new AboutDialog();
    connect(m_aboutDialog, &QDialog::finished, this, [this](int /* result */) {
      m_aboutDialog->deleteLater();
    });
  }

  if (m_aboutDialog->isVisible()) {
    m_aboutDialog->show();
    m_aboutDialog->raise();
    m_aboutDialog->activateWindow();
  } else {
    m_aboutDialog->open();
  }
}

// -------------------------------------------------------------------------------------------------
QWindow* ProjecteurApplication::createOverlayWindow()
{
  QObject *object = m_windowQmlComponent->create();
  object->setParent(m_qmlEngine);
  const auto window = qobject_cast<QWindow*>(object);
  auto layerWindow = LayerShellQt::Window::get(window);
  layerWindow->setScope(QStringLiteral("projecteur-overlay"));
  layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
  auto anchors = LayerShellQt::Window::Anchors{LayerShellQt::Window::AnchorTop};
  anchors.setFlag(LayerShellQt::Window::AnchorBottom);
  anchors.setFlag(LayerShellQt::Window::AnchorLeft);
  anchors.setFlag(LayerShellQt::Window::AnchorRight);
  layerWindow->setAnchors(anchors);
  layerWindow->setExclusiveZone(0);
  layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
  layerWindow->setActivateOnShow(false);
  layerWindow->setCloseOnDismissed(false);
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

  const bool wasSpotActive = m_spotlight->spotActive();

  m_overlayVisible = false;
  emit overlayVisibleChanged(false);

  window->hide();

  auto layerWindow = LayerShellQt::Window::get(window);
  layerWindow->setScreen(screen);
  layerWindow->setDesiredSize(QSize(0, 0));
  window->setScreen(screen);

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
void ProjecteurApplication::setScreenForCursorPos()
{
  updateOverlayWindow(m_overlayWindows.first(), screenAtCursorPos());
}

// -------------------------------------------------------------------------------------------------
QScreen* ProjecteurApplication::screenAtCursorPos() const
{
  return this->screenAt(QCursor::pos());
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
    auto const args = cmdValue.split(QLatin1Char(','), Qt::SkipEmptyParts);

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

  connect(localSocket, &QLocalSocket::errorOccurred, this, std::move(socketErrorFunc));

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
