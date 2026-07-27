// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "projecteurcontrol.h"

#include "device-hidpp.h"
#include "projecteurapp.h"
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
                                     Spotlight* spotlight, bool trayVisible)
  : QObject(application)
  , m_application(application)
  , m_settings(settings)
  , m_spotlight(spotlight)
  , m_trayVisible(trayVisible)
{
  connect(m_settings, &Settings::overlayDisabledChanged, this, [this](bool disabled) {
    const bool enabled = !disabled;
    emit overlayEnabledChanged(enabled);
    emitPropertiesChanged({{QStringLiteral("OverlayEnabled"), enabled}});
  });
  connect(m_spotlight, &Spotlight::spotActiveChanged, this, [this](bool active) {
    emit spotlightActiveChanged(active);
    emitPropertiesChanged({{QStringLiteral("SpotlightActive"), active}});
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

bool ProjecteurControl::registerService()
{
  auto connection = QDBusConnection::sessionBus();
  m_objectRegistered = connection.registerObject(
    QString::fromLatin1(ObjectPath), this,
    QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSignals |
      QDBusConnection::ExportAllSlots);
  if (!m_objectRegistered) { return false; }

  m_serviceRegistered = connection.registerService(QString::fromLatin1(ServiceName));
  if (!m_serviceRegistered) {
    connection.unregisterObject(QString::fromLatin1(ObjectPath));
    m_objectRegistered = false;
  }
  return m_serviceRegistered;
}

void ProjecteurControl::unregisterService()
{
  auto connection = QDBusConnection::sessionBus();
  if (m_serviceRegistered) {
    connection.unregisterService(QString::fromLatin1(ServiceName));
    m_serviceRegistered = false;
  }
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

void ProjecteurControl::SetOverlayEnabled(bool enabled)
{
  m_settings->setOverlayDisabled(!enabled);
}

void ProjecteurControl::SetSpotlightActive(bool active)
{
  m_spotlight->setSpotActive(active);
}

bool ProjecteurControl::LoadPreset(const QString& preset)
{
  if (!m_settings->presetModel()->hasPreset(preset)) { return false; }
  m_settings->loadPreset(preset);
  return true;
}

void ProjecteurControl::ShowPreferences()
{
  m_application->showPreferences(true);
}

void ProjecteurControl::ShowAbout()
{
  m_application->showAbout();
}

void ProjecteurControl::Quit()
{
  QCoreApplication::quit();
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
  const auto subDevice = connection->subDevice(path);
  const auto hidpp = qobject_cast<SubHidppConnection*>(subDevice.get());
  if (!hidpp) { return; }

  connect(hidpp, &SubHidppConnection::batteryInfoChanged, this,
          [this](const HIDPP::BatteryInfo&) { emitBatteryPropertiesChanged(); });
  connect(hidpp, &SubHidppConnection::featureSetInitialized, this, [this, hidpp]() {
    emitBatteryPropertiesChanged();
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
  if (!m_serviceRegistered) { return; }
  auto message = QDBusMessage::createSignal(
    QString::fromLatin1(ObjectPath), QStringLiteral("org.freedesktop.DBus.Properties"),
    QStringLiteral("PropertiesChanged"));
  message << QString::fromLatin1(InterfaceName) << changedProperties << QStringList{};
  QDBusConnection::sessionBus().send(message);
}
