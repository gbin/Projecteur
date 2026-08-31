// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "projecteurcontrol.h"

#include "device-hidpp.h"
#include "presentationtimer.h"
#include "projecteurapp.h"
#include "projecteurcontroladaptor.h"
#include "settings.h"
#include "spotlight.h"

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QQmlPropertyMap>
#include <QTimer>
#include <QVariantMap>

namespace {
QString batteryStatusName(HIDPP::BatteryStatus status)
{
  using BatteryStatus = HIDPP::BatteryStatus;
  switch (status) {
    case BatteryStatus::Discharging: return QStringLiteral("discharging");
    case BatteryStatus::Charging: return QStringLiteral("charging");
    case BatteryStatus::AlmostFull: return QStringLiteral("almost-full");
    case BatteryStatus::Full: return QStringLiteral("full");
    case BatteryStatus::SlowCharging: return QStringLiteral("slow-charging");
    case BatteryStatus::InvalidBattery: return QStringLiteral("invalid-battery");
    case BatteryStatus::ThermalError: return QStringLiteral("thermal-error");
    case BatteryStatus::ChargingError: return QStringLiteral("charging-error");
    case BatteryStatus::Uninitialized: return {};
  }
  return {};
}
}

ProjecteurControl::ProjecteurControl(ProjecteurApplication* application, Settings* settings,
                                     Spotlight* spotlight, PresentationTimer* presentationTimer,
                                     bool trayVisible)
  : QObject(application)
  , m_application(application)
  , m_settings(settings)
  , m_spotlight(spotlight)
  , m_presentationTimer(presentationTimer)
  , m_trayVisible(trayVisible)
{
  new ProjecteurControlAdaptor(this);

  connect(m_settings, &Settings::overlayDisabledChanged, this, [this](bool disabled) {
    const bool enabled = !disabled;
    emit overlayEnabledChanged(enabled);
    emitPropertiesChanged({{QStringLiteral("OverlayEnabled"), enabled}});
  });
  connect(m_spotlight, &Spotlight::spotActiveChanged, this, [this](bool active) {
    emit spotlightActiveChanged(active);
    emitPropertiesChanged({{QStringLiteral("SpotlightActive"), active}});
  });
  connect(m_settings, &Settings::pointerModeChanged, this, [this](const QString& mode) {
    emit pointerModeChanged(mode);
    emitPropertiesChanged({{QStringLiteral("PointerMode"), mode}});
  });
  connect(m_presentationTimer, &PresentationTimer::enabledChanged, this,
          [this](bool enabled) {
    emit timerEnabledChanged(enabled);
    emitPropertiesChanged({{QStringLiteral("TimerEnabled"), enabled}});
  });
  connect(m_presentationTimer, &PresentationTimer::stateChanged, this, [this]() {
    const auto state = timerState();
    emit timerStateChanged(state);
    emitPropertiesChanged({{QStringLiteral("TimerState"), state}});
  });
  connect(m_presentationTimer, &PresentationTimer::durationSecondsChanged, this,
          [this](int seconds) {
    emit timerDurationSecondsChanged(seconds);
    emitPropertiesChanged({{QStringLiteral("TimerDurationSeconds"), seconds}});
  });
  connect(m_presentationTimer, &PresentationTimer::remainingSecondsChanged, this,
          [this](int seconds) {
    emit timerRemainingSecondsChanged(seconds);
    emitPropertiesChanged({{QStringLiteral("TimerRemainingSeconds"), seconds}});
  });
  const auto updateConnectedDevices = [this]() {
    const auto devices = connectedDevices();
    emit connectedDevicesChanged(devices);
    emitPropertiesChanged({{QStringLiteral("ConnectedDevices"), devices}});
    emitBatteryPropertiesChanged();
  };
  connect(m_spotlight, &Spotlight::deviceConnected, this, updateConnectedDevices);
  connect(m_spotlight, &Spotlight::deviceDisconnected, this, updateConnectedDevices);
  connect(m_spotlight, &Spotlight::subDeviceConnected, this,
  [this](const DeviceId& id, const QString& /* name */, const QString& path) {
    watchBatteryConnection(id, path);
  });

  for (const auto& device : m_spotlight->connectedDevices())
  {
    const auto connection = m_spotlight->deviceConnection(device.id);
    if (!connection) { continue; }
    for (const auto& subDevice : connection->subDevices()) {
      watchBatteryConnection(device.id, subDevice.first);
    }
  }
  requestBatteryUpdates();

  auto* batteryTimer = new QTimer(this);
  batteryTimer->setTimerType(Qt::VeryCoarseTimer);
  batteryTimer->setInterval(5 * 60 * 1000);
  connect(batteryTimer, &QTimer::timeout, this, &ProjecteurControl::requestBatteryUpdates);
  batteryTimer->start();

  const auto updatePresets = [this]() {
    if (!m_currentPreset.isEmpty() && !m_settings->presetModel()->hasPreset(m_currentPreset)) {
      clearCurrentPreset();
    }
    const auto presetNames = presets();
    emit presetsChanged(presetNames);
    emitPropertiesChanged({{QStringLiteral("Presets"), presetNames}});
  };
  connect(m_settings->presetModel(), &QAbstractItemModel::rowsInserted, this, updatePresets);
  connect(m_settings->presetModel(), &QAbstractItemModel::rowsRemoved, this, updatePresets);
  connect(m_settings->presetModel(), &QAbstractItemModel::modelReset, this, updatePresets);

  const auto settingsChanged = [this]() { clearCurrentPreset(); };
  connect(m_settings, &Settings::showSpotShadeChanged, this, settingsChanged);
  connect(m_settings, &Settings::spotSizeChanged, this, settingsChanged);
  connect(m_settings, &Settings::showCenterDotChanged, this, settingsChanged);
  connect(m_settings, &Settings::dotSizeChanged, this, settingsChanged);
  connect(m_settings, &Settings::dotColorChanged, this, settingsChanged);
  connect(m_settings, &Settings::dotOpacityChanged, this, settingsChanged);
  connect(m_settings, &Settings::shadeColorChanged, this, settingsChanged);
  connect(m_settings, &Settings::shadeOpacityChanged, this, settingsChanged);
  connect(m_settings, &Settings::cursorChanged, this, settingsChanged);
  connect(m_settings, &Settings::spotShapeChanged, this, settingsChanged);
  connect(m_settings, &Settings::spotRotationChanged, this, settingsChanged);
  connect(m_settings, &Settings::showBorderChanged, this, settingsChanged);
  connect(m_settings, &Settings::borderColorChanged, this, settingsChanged);
  connect(m_settings, &Settings::borderSizeChanged, this, settingsChanged);
  connect(m_settings, &Settings::borderOpacityChanged, this, settingsChanged);
  connect(m_settings, &Settings::zoomEnabledChanged, this, settingsChanged);
  connect(m_settings, &Settings::zoomFactorChanged, this, settingsChanged);
  connect(m_settings, &Settings::zoomModeChanged, this, settingsChanged);
  connect(m_settings, &Settings::pointerModeChanged, this, settingsChanged);
  connect(m_settings, &Settings::laserSizeChanged, this, settingsChanged);
  connect(m_settings, &Settings::laserColorChanged, this, settingsChanged);
  connect(m_settings, &Settings::laserOpacityChanged, this, settingsChanged);
  connect(m_settings, &Settings::laserGlowChanged, this, settingsChanged);
  connect(m_settings, &Settings::laserGlowSizeChanged, this, settingsChanged);
  connect(m_settings, &Settings::laserGlowOpacityChanged, this, settingsChanged);
  connect(m_settings, &Settings::laserGlowColorChanged, this, settingsChanged);
  connect(m_settings, &Settings::multiScreenOverlayEnabledChanged, this, settingsChanged);

  for (const auto& shape : Settings::spotShapes()) {
    if (auto* shapeSettings = m_settings->shapeSettings(shape.name())) {
      connect(shapeSettings, &QQmlPropertyMap::valueChanged, this, settingsChanged);
    }
  }

  connect(m_settings, &Settings::presetLoaded, this, [this](const QString& preset) {
    if (m_currentPreset == preset) { return; }
    m_currentPreset = preset;
    emit currentPresetChanged(m_currentPreset);
    emitPropertiesChanged({{QStringLiteral("CurrentPreset"), m_currentPreset}});
  });
}

bool ProjecteurControl::registerObject()
{
  auto connection = QDBusConnection::sessionBus();
  m_objectRegistered = connection.registerObject(
    QString::fromLatin1(ObjectPath), this,
    QDBusConnection::ExportAdaptors);
  return m_objectRegistered;
}

void ProjecteurControl::unregisterObject()
{
  auto connection = QDBusConnection::sessionBus();
  if (m_objectRegistered) {
    connection.unregisterObject(QString::fromLatin1(ObjectPath));
    m_objectRegistered = false;
  }
}

bool ProjecteurControl::overlayEnabled() const
{
  return !m_settings->overlayDisabled();
}

bool ProjecteurControl::spotlightActive() const
{
  return m_spotlight->spotActive();
}

QString ProjecteurControl::pointerMode() const
{
  return m_settings->pointerMode();
}

QStringList ProjecteurControl::connectedDevices() const
{
  QStringList result;
  for (const auto& device : m_spotlight->connectedDevices()) {
    result.push_back(device.name);
  }
  return result;
}

QList<int> ProjecteurControl::connectedDeviceBatteryLevels() const
{
  QList<int> result;
  for (const auto& device : m_spotlight->connectedDevices())
  {
    int level = -1;
    const auto connection = m_spotlight->deviceConnection(device.id);
    if (connection)
    {
      for (const auto& subDevice : connection->subDevices())
      {
        const auto hidpp = qobject_cast<SubHidppConnection*>(subDevice.second.get());
        if (hidpp && hidpp->hasFlags(DeviceFlag::ReportBattery)
            && hidpp->batteryInfo().status != HIDPP::BatteryStatus::Uninitialized)
        {
          level = hidpp->batteryInfo().currentLevel;
          break;
        }
      }
    }
    result.push_back(level);
  }
  return result;
}

QStringList ProjecteurControl::connectedDeviceBatteryStatuses() const
{
  QStringList result;
  for (const auto& device : m_spotlight->connectedDevices())
  {
    QString status;
    const auto connection = m_spotlight->deviceConnection(device.id);
    if (connection)
    {
      for (const auto& subDevice : connection->subDevices())
      {
        const auto hidpp = qobject_cast<SubHidppConnection*>(subDevice.second.get());
        if (hidpp && hidpp->hasFlags(DeviceFlag::ReportBattery))
        {
          status = batteryStatusName(hidpp->batteryInfo().status);
          if (!status.isEmpty()) { break; }
        }
      }
    }
    result.push_back(status);
  }
  return result;
}

QStringList ProjecteurControl::presets() const
{
  QStringList result;
  for (const auto& preset : m_settings->presets()) {
    result.push_back(preset);
  }
  return result;
}

bool ProjecteurControl::timerEnabled() const
{
  return m_presentationTimer->enabled();
}

QString ProjecteurControl::timerState() const
{
  return m_presentationTimer->stateName();
}

int ProjecteurControl::timerDurationSeconds() const
{
  return m_presentationTimer->durationSeconds();
}

int ProjecteurControl::timerRemainingSeconds() const
{
  return m_presentationTimer->remainingSeconds();
}

void ProjecteurControl::SetOverlayEnabled(bool enabled)
{
  m_settings->setOverlayDisabled(!enabled);
}

void ProjecteurControl::SetSpotlightActive(bool active)
{
  m_spotlight->setSpotActive(active);
}

void ProjecteurControl::SetPointerMode(const QString& mode)
{
  if (Settings::isPointerMode(mode)) {
    m_settings->setPointerMode(mode);
  }
}

void ProjecteurControl::TogglePointerMode()
{
  m_settings->setPointerMode(m_settings->pointerMode() == QStringLiteral("laser")
                               ? QStringLiteral("spotlight")
                               : QStringLiteral("laser"));
}

void ProjecteurControl::ToggleLaserActive()
{
  if (m_settings->pointerMode() != QStringLiteral("laser")) {
    m_settings->setPointerMode(QStringLiteral("laser"));
    if (m_settings->overlayDisabled()) {
      m_settings->setOverlayDisabled(false);
    }
  } else {
    m_settings->setOverlayDisabled(!m_settings->overlayDisabled());
  }
}

bool ProjecteurControl::LoadPreset(const QString& preset)
{
  if (!m_settings->presetModel()->hasPreset(preset)) { return false; }
  m_settings->loadPreset(preset);
  return true;
}

void ProjecteurControl::SetTimerEnabled(bool enabled)
{
  m_presentationTimer->setEnabled(enabled);
}

void ProjecteurControl::StartTimer()
{
  m_presentationTimer->start();
}

void ProjecteurControl::RestartTimer()
{
  m_presentationTimer->restart();
}

void ProjecteurControl::ResetTimer()
{
  m_presentationTimer->reset();
}

void ProjecteurControl::SetTimerDurationSeconds(int seconds)
{
  m_presentationTimer->setDurationSeconds(seconds);
}

void ProjecteurControl::loadNextPreset()
{
  loadRelativePreset(1);
}

void ProjecteurControl::loadPreviousPreset()
{
  loadRelativePreset(-1);
}

void ProjecteurControl::ShowPreferences()
{
  m_application->showPreferences(true);
}

void ProjecteurControl::ShowAbout()
{
  m_application->showAbout();
}

void ProjecteurControl::ApplyCommands(const QStringList& commands)
{
  m_application->applyCommands(commands);
}

void ProjecteurControl::Quit()
{
  QCoreApplication::quit();
}

void ProjecteurControl::loadRelativePreset(int offset)
{
  const auto presetNames = presets();
  if (presetNames.isEmpty()) { return; }

  const auto currentIndex = presetNames.indexOf(m_currentPreset);
  qsizetype targetIndex = 0;
  if (offset < 0) {
    targetIndex = currentIndex <= 0 ? presetNames.size() - 1 : currentIndex - 1;
  } else {
    targetIndex = currentIndex < 0 || currentIndex == presetNames.size() - 1
                    ? 0
                    : currentIndex + 1;
  }
  LoadPreset(presetNames.at(targetIndex));
}

void ProjecteurControl::clearCurrentPreset()
{
  if (m_currentPreset.isEmpty()) { return; }
  m_currentPreset.clear();
  emit currentPresetChanged(m_currentPreset);
  emitPropertiesChanged({{QStringLiteral("CurrentPreset"), m_currentPreset}});
}

void ProjecteurControl::emitBatteryPropertiesChanged()
{
  const auto levels = connectedDeviceBatteryLevels();
  const auto statuses = connectedDeviceBatteryStatuses();
  emit connectedDeviceBatteryLevelsChanged(levels);
  emit connectedDeviceBatteryStatusesChanged(statuses);
  emitPropertiesChanged({
    {QStringLiteral("ConnectedDeviceBatteryLevels"), QVariant::fromValue(levels)},
    {QStringLiteral("ConnectedDeviceBatteryStatuses"), statuses}
  });
}

void ProjecteurControl::requestBatteryUpdates()
{
  for (const auto& device : m_spotlight->connectedDevices())
  {
    const auto connection = m_spotlight->deviceConnection(device.id);
    if (!connection) { continue; }
    for (const auto& subDevice : connection->subDevices())
    {
      const auto hidpp = qobject_cast<SubHidppConnection*>(subDevice.second.get());
      if (hidpp && hidpp->hasFlags(DeviceFlag::ReportBattery)) {
        hidpp->triggerBattyerInfoUpdate();
      }
    }
  }
}

void ProjecteurControl::watchBatteryConnection(const DeviceId& id, const QString& path)
{
  const auto connection = m_spotlight->deviceConnection(id);
  if (!connection) { return; }
  const auto deviceName = connection->deviceName();
  const auto subDevice = connection->subDevice(path);
  const auto hidpp = qobject_cast<SubHidppConnection*>(subDevice.get());
  if (!hidpp) { return; }
  if (m_watchedBatteryConnections.contains(hidpp)) { return; }

  m_watchedBatteryConnections.insert(hidpp);
  connect(hidpp, &QObject::destroyed, this, [this, hidpp]() {
    m_watchedBatteryConnections.remove(hidpp);
  });

  connect(hidpp, &SubHidppConnection::batteryInfoChanged, this,
          [this, deviceName](const HIDPP::BatteryInfo& info) {
    emitBatteryPropertiesChanged();
    emit batteryStateChanged(deviceName, info.currentLevel, batteryStatusName(info.status));
  });
  connect(hidpp, &SubHidppConnection::featureSetInitialized, this,
          [this, hidpp, deviceName]() {
    emitBatteryPropertiesChanged();
    const auto& info = hidpp->batteryInfo();
    if (info.status != HIDPP::BatteryStatus::Uninitialized) {
      emit batteryStateChanged(deviceName, info.currentLevel, batteryStatusName(info.status));
    }
    if (hidpp->hasFlags(DeviceFlag::ReportBattery)) {
      hidpp->triggerBattyerInfoUpdate();
    }
  });

  if (hidpp->hasFlags(DeviceFlag::ReportBattery)) {
    hidpp->triggerBattyerInfoUpdate();
  }
}

void ProjecteurControl::emitPropertiesChanged(const QVariantMap& changedProperties)
{
  if (!m_objectRegistered) { return; }
  auto message = QDBusMessage::createSignal(
    QString::fromLatin1(ObjectPath), QStringLiteral("org.freedesktop.DBus.Properties"),
    QStringLiteral("PropertiesChanged"));
  message << QString::fromLatin1(InterfaceName) << changedProperties << QStringList{};
  QDBusConnection::sessionBus().send(message);
}
