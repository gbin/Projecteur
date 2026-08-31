// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "projecteurapplet.h"

#include "projecteurcontrolinterface.h"

#include <KPluginFactory>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusServiceWatcher>
#include <QTimer>

namespace {
constexpr auto serviceName = "org.projecteur.Projecteur";
constexpr auto objectPath = "/org/projecteur/Projecteur/Control";
}

ProjecteurApplet::ProjecteurApplet(QObject* parent, const KPluginMetaData& data,
                                   const QVariantList& args)
  : Plasma::Applet(parent, data, args)
  , m_serviceWatcher(new QDBusServiceWatcher(
      QString::fromLatin1(serviceName), QDBusConnection::sessionBus(),
      QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration,
      this))
{
  connect(m_serviceWatcher, &QDBusServiceWatcher::serviceRegistered,
          this, &ProjecteurApplet::serviceRegistered);
  connect(m_serviceWatcher, &QDBusServiceWatcher::serviceUnregistered,
          this, &ProjecteurApplet::serviceUnregistered);

  const auto bus = QDBusConnection::sessionBus();
  const auto registered = bus.interface()->isServiceRegistered(QString::fromLatin1(serviceName));
  if (registered.isValid() && registered.value()) {
    serviceRegistered(QString::fromLatin1(serviceName));
  }
}

void ProjecteurApplet::setOverlayEnabled(bool enabled)
{
  if (m_interface) { m_interface->SetOverlayEnabled(enabled); }
}

void ProjecteurApplet::setSpotlightActive(bool active)
{
  if (m_interface) { m_interface->SetSpotlightActive(active); }
}

void ProjecteurApplet::setPointerMode(const QString& mode)
{
  if (m_interface) { m_interface->SetPointerMode(mode); }
}

void ProjecteurApplet::loadPreset(const QString& preset)
{
  if (m_interface) { m_interface->LoadPreset(preset); }
}

void ProjecteurApplet::setTimerEnabled(bool enabled)
{
  if (m_interface) { m_interface->SetTimerEnabled(enabled); }
}

void ProjecteurApplet::startTimer()
{
  if (m_interface) { m_interface->StartTimer(); }
}

void ProjecteurApplet::restartTimer()
{
  if (m_interface) { m_interface->RestartTimer(); }
}

void ProjecteurApplet::resetTimer()
{
  if (m_interface) { m_interface->ResetTimer(); }
}

void ProjecteurApplet::setTimerDurationSeconds(int seconds)
{
  if (m_interface) { m_interface->SetTimerDurationSeconds(seconds); }
}

void ProjecteurApplet::showPreferences()
{
  if (m_interface) { m_interface->ShowPreferences(); }
}

void ProjecteurApplet::showAbout()
{
  if (m_interface) { m_interface->ShowAbout(); }
}

void ProjecteurApplet::quitProjecteur()
{
  if (m_interface) { m_interface->Quit(); }
}

void ProjecteurApplet::serviceRegistered(const QString& service)
{
  if (service != QString::fromLatin1(serviceName)) { return; }
  createInterface();
  if (!m_serviceAvailable) {
    m_serviceAvailable = true;
    emit serviceAvailableChanged();
  }
  QTimer::singleShot(0, this, &ProjecteurApplet::refresh);
}

void ProjecteurApplet::serviceUnregistered(const QString& service)
{
  if (service != QString::fromLatin1(serviceName)) { return; }
  delete m_interface;
  m_interface = nullptr;
  if (m_serviceAvailable) {
    m_serviceAvailable = false;
    emit serviceAvailableChanged();
  }
  resetState();
}

void ProjecteurApplet::remoteOverlayEnabledChanged(bool enabled)
{
  if (m_overlayEnabled == enabled) { return; }
  m_overlayEnabled = enabled;
  emit overlayEnabledChanged();
}

void ProjecteurApplet::remoteSpotlightActiveChanged(bool active)
{
  if (m_spotlightActive == active) { return; }
  m_spotlightActive = active;
  emit spotlightActiveChanged();
}

void ProjecteurApplet::remotePointerModeChanged(const QString& mode)
{
  if (m_pointerMode == mode) { return; }
  m_pointerMode = mode;
  emit pointerModeChanged();
}

void ProjecteurApplet::remoteConnectedDevicesChanged(const QStringList& devices)
{
  if (m_connectedDevices == devices) { return; }
  m_connectedDevices = devices;
  emit connectedDevicesChanged();
}

void ProjecteurApplet::remoteConnectedDeviceBatteryLevelsChanged(const QList<int>& levels)
{
  if (m_connectedDeviceBatteryLevels == levels) { return; }
  m_connectedDeviceBatteryLevels = levels;
  emit connectedDeviceBatteryLevelsChanged();
}

void ProjecteurApplet::remoteConnectedDeviceBatteryStatusesChanged(const QStringList& statuses)
{
  if (m_connectedDeviceBatteryStatuses == statuses) { return; }
  m_connectedDeviceBatteryStatuses = statuses;
  emit connectedDeviceBatteryStatusesChanged();
}

void ProjecteurApplet::remotePresetsChanged(const QStringList& presets)
{
  if (m_presets == presets) { return; }
  m_presets = presets;
  emit presetsChanged();
}

void ProjecteurApplet::remoteCurrentPresetChanged(const QString& preset)
{
  if (m_currentPreset == preset) { return; }
  m_currentPreset = preset;
  emit currentPresetChanged();
}

void ProjecteurApplet::remoteTimerEnabledChanged(bool enabled)
{
  if (m_timerEnabled == enabled) { return; }
  m_timerEnabled = enabled;
  emit timerEnabledChanged();
}

void ProjecteurApplet::remoteTimerStateChanged(const QString& state)
{
  if (m_timerState == state) { return; }
  m_timerState = state;
  emit timerStateChanged();
}

void ProjecteurApplet::remoteTimerDurationSecondsChanged(int seconds)
{
  if (m_timerDurationSeconds == seconds) { return; }
  m_timerDurationSeconds = seconds;
  emit timerDurationSecondsChanged();
}

void ProjecteurApplet::remoteTimerRemainingSecondsChanged(int seconds)
{
  if (m_timerRemainingSeconds == seconds) { return; }
  m_timerRemainingSeconds = seconds;
  emit timerRemainingSecondsChanged();
}

void ProjecteurApplet::createInterface()
{
  delete m_interface;
  m_interface = new OrgProjecteurProjecteurInterface(
    QString::fromLatin1(serviceName), QString::fromLatin1(objectPath),
    QDBusConnection::sessionBus(), this);

  connect(m_interface, &OrgProjecteurProjecteurInterface::overlayEnabledChanged,
          this, &ProjecteurApplet::remoteOverlayEnabledChanged);
  connect(m_interface, &OrgProjecteurProjecteurInterface::spotlightActiveChanged,
          this, &ProjecteurApplet::remoteSpotlightActiveChanged);
  connect(m_interface, &OrgProjecteurProjecteurInterface::pointerModeChanged,
          this, &ProjecteurApplet::remotePointerModeChanged);
  connect(m_interface, &OrgProjecteurProjecteurInterface::connectedDevicesChanged,
          this, &ProjecteurApplet::remoteConnectedDevicesChanged);
  connect(m_interface,
          &OrgProjecteurProjecteurInterface::connectedDeviceBatteryLevelsChanged,
          this, &ProjecteurApplet::remoteConnectedDeviceBatteryLevelsChanged);
  connect(m_interface,
          &OrgProjecteurProjecteurInterface::connectedDeviceBatteryStatusesChanged,
          this, &ProjecteurApplet::remoteConnectedDeviceBatteryStatusesChanged);
  connect(m_interface, &OrgProjecteurProjecteurInterface::presetsChanged,
          this, &ProjecteurApplet::remotePresetsChanged);
  connect(m_interface, &OrgProjecteurProjecteurInterface::currentPresetChanged,
          this, &ProjecteurApplet::remoteCurrentPresetChanged);
  connect(m_interface, &OrgProjecteurProjecteurInterface::timerEnabledChanged,
          this, &ProjecteurApplet::remoteTimerEnabledChanged);
  connect(m_interface, &OrgProjecteurProjecteurInterface::timerStateChanged,
          this, &ProjecteurApplet::remoteTimerStateChanged);
  connect(m_interface, &OrgProjecteurProjecteurInterface::timerDurationSecondsChanged,
          this, &ProjecteurApplet::remoteTimerDurationSecondsChanged);
  connect(m_interface, &OrgProjecteurProjecteurInterface::timerRemainingSecondsChanged,
          this, &ProjecteurApplet::remoteTimerRemainingSecondsChanged);
}

void ProjecteurApplet::refresh()
{
  if (!m_interface || !m_interface->isValid()) { return; }

  const bool trayVisible = m_interface->trayVisible();
  if (m_trayVisible != trayVisible) {
    m_trayVisible = trayVisible;
    emit trayVisibleChanged();
  }
  remoteOverlayEnabledChanged(m_interface->overlayEnabled());
  remoteSpotlightActiveChanged(m_interface->spotlightActive());
  remotePointerModeChanged(m_interface->pointerMode());
  remoteConnectedDevicesChanged(m_interface->connectedDevices());
  remoteConnectedDeviceBatteryLevelsChanged(m_interface->connectedDeviceBatteryLevels());
  remoteConnectedDeviceBatteryStatusesChanged(m_interface->connectedDeviceBatteryStatuses());
  remotePresetsChanged(m_interface->presets());
  remoteCurrentPresetChanged(m_interface->currentPreset());
  if (!m_timerAvailable) {
    m_timerAvailable = true;
    emit timerAvailableChanged();
  }
  remoteTimerEnabledChanged(m_interface->timerEnabled());
  remoteTimerStateChanged(m_interface->timerState());
  remoteTimerDurationSecondsChanged(m_interface->timerDurationSeconds());
  remoteTimerRemainingSecondsChanged(m_interface->timerRemainingSeconds());
}

void ProjecteurApplet::resetState()
{
  if (!m_trayVisible) {
    m_trayVisible = true;
    emit trayVisibleChanged();
  }
  remoteOverlayEnabledChanged(true);
  remoteSpotlightActiveChanged(false);
  remotePointerModeChanged(QStringLiteral("spotlight"));
  remoteConnectedDevicesChanged({});
  remoteConnectedDeviceBatteryLevelsChanged({});
  remoteConnectedDeviceBatteryStatusesChanged({});
  remotePresetsChanged({});
  remoteCurrentPresetChanged({});
  if (m_timerAvailable) {
    m_timerAvailable = false;
    emit timerAvailableChanged();
  }
  remoteTimerEnabledChanged(false);
  remoteTimerStateChanged(QStringLiteral("idle"));
  remoteTimerDurationSecondsChanged(15 * 60);
  remoteTimerRemainingSecondsChanged(15 * 60);
}

K_PLUGIN_CLASS_WITH_JSON(ProjecteurApplet, "metadata.json")

#include "projecteurapplet.moc"
