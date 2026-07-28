// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "projecteurapp.h"

#include "device-command-helper.h"
#include "imageitem.h"
#include "linuxdesktop.h"
#include "preferencesdlg.h"
#include "presentationtimer.h"
#include "projecteur_command_debug.h"
#include "projecteur_main_debug.h"
#include "projecteurcontrol.h"
#include "settings.h"
#include "spotlight.h"

#include <KAboutApplicationDialog>
#include <KAboutData>
#include <KActionCollection>
#include <KDBusService>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <KMessageBox>
#include <KNotification>
#include <KWindowSystem>
#include <LayerShellQt/Window>

#include <QAction>
#include <QFontDatabase>
#include <QIcon>
#include <QPointer>
#include <QHash>
#include <QSet>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QWindow>

#include <utility>

namespace {
constexpr auto notificationComponent = "projecteur";

void sendNotification(const QString& eventId, const QString& title, const QString& text,
                      const QString& iconName = QStringLiteral("projecteur"))
{
  KNotification::event(
    eventId, title, text, iconName, KNotification::CloseOnTimeout,
    QString::fromLatin1(notificationComponent));
}
}

// -------------------------------------------------------------------------------------------------
ProjecteurApplication::ProjecteurApplication(int &argc, char **argv, const Options& options)
  : QApplication(argc, argv)
{
  m_dbusService = new KDBusService(
    KDBusService::Unique | KDBusService::NoExitOnFailure, this);
  if (!m_dbusService->isRegistered()) {
    return;
  }
  m_primaryInstance = true;
  setWindowIcon(QIcon::fromTheme(
    QStringLiteral("projecteur"), QIcon(QStringLiteral(":/icons/projecteur-tray.svg"))));

  if (!options.commands.isEmpty()) {
    const auto commands = options.commands.join(QStringLiteral("; "));
    qCWarning(PROJECTEUR_MAIN_LOG).noquote()
      << QStringLiteral("Cannot send commands '%1' - no running application instance found.").arg(commands);
    m_startupExitCode = 43;
    m_dbusService->unregister();
    m_primaryInstance = false;
    return;
  }

  m_linuxDesktop = new LinuxDesktop(this);

  if (screens().empty())
  {
    const auto title = i18n("No Screens detected");
    const auto text = i18n("screens().size() returned a size < 1. Exiting.");
    qCCritical(PROJECTEUR_MAIN_LOG).noquote()
      << "No screens detected; screens().size() returned a size below one. Exiting.";
    KMessageBox::error(nullptr, text, title);
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
  setupControlService(options);
  setupGlobalShortcuts();

  m_dialog = std::make_unique<PreferencesDialog>(m_settings, m_spotlight, m_actionCollection,
                                                  options.dialogMinimizeOnly
                                                  ? PreferencesDialog::Mode::MinimizeOnlyDialog
                                                  : PreferencesDialog::Mode::ClosableDialog);

  connect(&*m_dialog, &PreferencesDialog::testButtonClicked, this, [this](){
    m_spotlight->setSpotActive(true);
  });
  connect(&*m_dialog, &PreferencesDialog::exitApplicationRequested, this, [this]() {
    qCDebug(PROJECTEUR_MAIN_LOG).noquote() << QStringLiteral("Exit request from preferences dialog.");
    quit();
  });

  const QString desktopEnv = m_linuxDesktop->type() == LinuxDesktop::Type::KDE
                               ? QStringLiteral("KDE")
                               : QStringLiteral("Unknown");

  qCDebug(PROJECTEUR_MAIN_LOG).noquote() << QStringLiteral("Qt platform plugin: %1;").arg(QGuiApplication::platformName())
                    << QStringLiteral("Desktop Environment: %1;").arg(desktopEnv)
                    << QStringLiteral("Wayland: %1").arg(m_linuxDesktop->isWayland() ? "true" : "false");

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
    const auto title = i18n("Overlay window error.");
    const auto text = i18n("Qml component has status '%1'. Exiting.",
                           static_cast<int>(m_windowQmlComponent->status()));

    qCCritical(PROJECTEUR_MAIN_LOG).noquote()
      << "Overlay QML component has unexpected status:"
      << static_cast<int>(m_windowQmlComponent->status());
    for (const auto& error : m_windowQmlComponent->errors()) {
      qCCritical(PROJECTEUR_MAIN_LOG).noquote() << error.toString();
    }

    KMessageBox::error(nullptr, text, title);
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

  setupNotifications();

  connect(this, &ProjecteurApplication::aboutToQuit, this, [this](){
    m_linuxDesktop->setShakeCursorEffectSuppressed(false);
    for (const auto window : m_overlayWindows) { delete window; }
    m_overlayWindows.clear();
    m_screenWindowMap.clear();
  });

  // Setup the spotlight connections.
  setupSpotlight();
}

// -------------------------------------------------------------------------------------------------
ProjecteurApplication::~ProjecteurApplication()
{
  if (m_control) { m_control->unregisterObject(); }
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

      QScreen* const cursorScreen = screenAtCursorPos();
      if (!m_settings->multiScreenOverlayEnabled()) {
        updateOverlayWindow(m_overlayWindows.first(), cursorScreen);
      }
      if (cursorScreen) {
        setCurrentSpotScreen(quint64(cursorScreen));
      }

      for (const auto window : m_overlayWindows)
      {
        if (window->screen())
        {
          if (m_settings->zoomEnabled()) {
            auto* stream = window->property("desktopStream").value<QObject*>();
            const auto streamScreenId =
              window->property("desktopStreamScreenId").toULongLong();
            const auto currentScreenId = quint64(window->screen());
            if (stream && streamScreenId != currentScreenId) {
              window->setProperty("desktopStream",
                                  QVariant::fromValue<QObject*>(nullptr));
              stream->deleteLater();
              stream = nullptr;
            }
            if (!stream) {
              stream = m_linuxDesktop->streamScreen(window->screen(), window);
              window->setProperty("desktopStream", QVariant::fromValue(stream));
              window->setProperty("desktopStreamScreenId", currentScreenId);
            }
            if (!stream) {
              window->setProperty("desktopPixmap",
                                  m_linuxDesktop->grabScreen(window->screen()));
            }
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
      showAndActivate(m_dialog.get());
    }
  });
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::setupControlService(Options const& options)
{
  m_control = new ProjecteurControl(this, m_settings, m_spotlight, m_presentationTimer,
                                    !options.hideSysTrayIcon);
  if (!m_control->registerObject()) {
    qCCritical(PROJECTEUR_MAIN_LOG).noquote() << QStringLiteral("Could not register the Projecteur D-Bus control object.");
  }
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::setupGlobalShortcuts()
{
  m_actionCollection = new KActionCollection(this);
  m_actionCollection->setComponentDisplayName(i18n("Projecteur"));

  const auto addAction =
    [this](const QString& id, const QString& text, const QString& iconName, auto callback)
    {
      auto* action = new QAction(QIcon::fromTheme(iconName), text, m_actionCollection);
      m_actionCollection->addAction(id, action);
      connect(action, &QAction::triggered, this, std::move(callback));
      if (!KGlobalAccel::setGlobalShortcut(action, QList<QKeySequence>{})) {
        qCWarning(PROJECTEUR_MAIN_LOG).noquote() << QStringLiteral("Could not register global shortcut action '%1'.").arg(id);
      }
    };

  addAction(
    QStringLiteral("toggle_spotlight"), i18n("Toggle Spotlight"),
    QStringLiteral("view-visible"),
    [this]() {
      if (!m_settings->overlayDisabled()) {
        m_control->SetSpotlightActive(!m_control->spotlightActive());
      }
    });
  addAction(
    QStringLiteral("show_preferences"), i18n("Show Preferences"),
    QStringLiteral("configure"),
    [this]() { m_control->ShowPreferences(); });
  addAction(
    QStringLiteral("start_restart_timer"), i18n("Start or Restart Presentation Timer"),
    QStringLiteral("chronometer"),
    [this]() { m_control->RestartTimer(); });
  addAction(
    QStringLiteral("reset_timer"), i18n("Reset Presentation Timer"),
    QStringLiteral("edit-undo"),
    [this]() { m_control->ResetTimer(); });
  addAction(
    QStringLiteral("next_preset"), i18n("Next Spotlight Preset"),
    QStringLiteral("go-next"),
    [this]() { m_control->loadNextPreset(); });
  addAction(
    QStringLiteral("previous_preset"), i18n("Previous Spotlight Preset"),
    QStringLiteral("go-previous"),
    [this]() { m_control->loadPreviousPreset(); });
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::setupNotifications()
{
  connect(m_presentationTimer, &PresentationTimer::stateChanged, this,
          [this](PresentationTimer::State state) {
    if (state == PresentationTimer::State::Completed) {
      sendNotification(
        QStringLiteral("presentationTimerFinished"),
        i18n("Presentation timer finished"),
        i18n("The configured presentation time has elapsed."),
        QStringLiteral("chronometer"));
    }
  });

  connect(m_spotlight, &Spotlight::deviceConnected, this,
          [this](const DeviceId&, const QString& name) {
    sendNotification(
      QStringLiteral("presenterConnected"),
      i18n("Presenter connected"),
      i18n("%1 is ready.", name),
      QStringLiteral("input-mouse"));
  });
  connect(m_spotlight, &Spotlight::deviceDisconnected, this,
          [this](const DeviceId&, const QString& name) {
    sendNotification(
      QStringLiteral("presenterDisconnected"),
      i18n("Presenter disconnected"),
      i18n("%1 is no longer available.", name),
      QStringLiteral("input-mouse"));
  });

  const auto inaccessiblePaths = std::make_shared<QSet<QString>>();
  connect(m_spotlight, &Spotlight::deviceAccessError, this,
          [this, inaccessiblePaths](const QString& name, const QString& path) {
    if (inaccessiblePaths->contains(path)) { return; }
    inaccessiblePaths->insert(path);
    sendNotification(
      QStringLiteral("deviceAccessError"),
      i18n("Presenter access failed"),
      i18n("%1 cannot access %2. Check the installed udev rules and device permissions.",
           name, path),
      QStringLiteral("dialog-warning"));
  });
  connect(m_spotlight, &Spotlight::subDeviceConnected, this,
          [inaccessiblePaths](const DeviceId&, const QString&, const QString& path) {
    inaccessiblePaths->remove(path);
  });

  const auto batteryWarnings = std::make_shared<QHash<QString, QString>>();
  connect(m_control, &ProjecteurControl::batteryStateChanged, this,
          [this, batteryWarnings](const QString& name, int level, const QString& status) {
    QString warningKey;
    QString eventId;
    QString title;
    QString text;
    QString iconName;

    if (status == QStringLiteral("invalid-battery")
        || status == QStringLiteral("thermal-error")
        || status == QStringLiteral("charging-error")) {
      warningKey = QStringLiteral("error:") + status;
      eventId = QStringLiteral("presenterBatteryError");
      title = i18n("Presenter battery problem");
      text = i18n("%1 reported a battery error: %2.", name, status);
      iconName = QStringLiteral("dialog-warning");
    } else if (level >= 0 && level <= 20 && status == QStringLiteral("discharging")) {
      warningKey = QStringLiteral("low");
      eventId = QStringLiteral("presenterBatteryLow");
      title = i18n("Presenter battery low");
      text = i18n("%1 has %2% battery remaining.", name, level);
      iconName = QStringLiteral("battery-low");
    }

    if (warningKey.isEmpty()) {
      batteryWarnings->remove(name);
      return;
    }
    if (batteryWarnings->value(name) == warningKey) { return; }
    batteryWarnings->insert(name, warningKey);
    sendNotification(eventId, title, text, iconName);
  });
  connect(m_spotlight, &Spotlight::deviceDisconnected, this,
          [batteryWarnings](const DeviceId&, const QString& name) {
    batteryWarnings->remove(name);
  });
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::showAbout()
{
  if (!m_aboutDialog) {
    m_aboutDialog = new KAboutApplicationDialog(KAboutData::applicationData());
    m_aboutDialog->setAttribute(Qt::WA_DeleteOnClose);
  }

  showAndActivate(m_aboutDialog);
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::showAndActivate(QWidget* widget)
{
  if (!widget) { return; }
  widget->show();
  widget->raise();
  if (auto* window = widget->windowHandle()) {
    KWindowSystem::updateStartupId(window);
    KWindowSystem::activateWindow(window);
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
void ProjecteurApplication::activate()
{
  if (m_dialog) {
    showPreferences(true);
  }
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::applyCommands(const QStringList& commands)
{
  for (const auto& command : commands) {
    const auto trimmedCommand = command.trimmed();
    if (!trimmedCommand.isEmpty()) {
      applyCommand(trimmedCommand);
    }
  }
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::applyCommand(const QString& command)
{
  const QString cmdKey = command.section('=', 0, 0).trimmed();
  const QString cmdValue = command.section('=', 1).trimmed();

  if (cmdKey == "quit")
  {
    qCDebug(PROJECTEUR_COMMAND_LOG).noquote() << QStringLiteral("Received quit command.");
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

    qCDebug(PROJECTEUR_COMMAND_LOG).noquote() << QStringLiteral("Received command vibrate = intensity:%1, length:%2").arg(intensity).arg(length);

    m_deviceCommandHelper->sendVibrateCommand(intensity, length);
  }
  else if (cmdKey == "spot.size.adjust")
  {
    bool ok = false;
    int const sizeAdjust = cmdValue.toInt(&ok);
    if (ok) {
      qCDebug(PROJECTEUR_COMMAND_LOG).noquote() << QStringLiteral("Received command spot.size.adjust = %1%2").arg(sizeAdjust > 0 ? "+" : "").arg(sizeAdjust);
      m_settings->setSpotSize(m_settings->spotSize() + sizeAdjust);
    } else {
      qCDebug(PROJECTEUR_COMMAND_LOG).noquote() << QStringLiteral("Received invalid value for command spot.size.adjust");
    }
  }
  else if (cmdKey == "spot")
  {
    if (cmdValue.isEmpty()) {
      qCDebug(PROJECTEUR_COMMAND_LOG).noquote() << QStringLiteral("Received empty command value for command spot");
    } else if (cmdValue.toLower() == "toggle") {
      m_spotlight->setSpotActive(!m_spotlight->spotActive());
    }
    else {
      const bool active = (cmdValue.toLower() == "on"
                            || cmdValue == "1"
                            || cmdValue.toLower() == "true");
      qCDebug(PROJECTEUR_COMMAND_LOG).noquote() << QStringLiteral("Received command spot = %1").arg(active);
      m_spotlight->setSpotActive(active);
    }
  }
  else if (cmdKey == "settings" || cmdKey == "preferences")
  {
    const bool show = !(cmdValue.toLower() == "hide" || cmdValue == "0");
    qCDebug(PROJECTEUR_COMMAND_LOG).noquote() << QStringLiteral("Received command settings = %1").arg(show);
    showPreferences(show);
  }
  else if (cmdKey == "preset")
  {
    qCDebug(PROJECTEUR_COMMAND_LOG).noquote() << QStringLiteral("Received command preset = %1").arg(cmdValue);
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
      qCDebug(PROJECTEUR_COMMAND_LOG).noquote() << QStringLiteral("Received command '%1'='%2'").arg(cmdKey).arg(cmdValue);
      it->second.setFunction(cmdValue);
    }
    else {
      // string property not found...
      qCWarning(PROJECTEUR_COMMAND_LOG).noquote() << QStringLiteral("Received unknown command key (%1)").arg(cmdKey);
    }
  }
}

// -------------------------------------------------------------------------------------------------
void ProjecteurApplication::showPreferences(bool show)
{
  if (show)
  {
    showAndActivate(m_dialog.get());
  }
  else {
    m_dialog->reject();
  }
}
