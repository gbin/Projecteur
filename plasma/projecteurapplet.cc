// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "projecteurapplet.h"

#include <KPluginFactory>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusServiceWatcher>
#include <QTimer>

namespace {
constexpr auto serviceName = "org.projecteur.Projecteur";
constexpr auto objectPath = "/org/projecteur/Projecteur";
constexpr auto interfaceName = "org.projecteur.Projecteur";
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

  auto bus = QDBusConnection::sessionBus();
  bus.connect(QString::fromLatin1(serviceName), QString::fromLatin1(objectPath),
              QString::fromLatin1(interfaceName), QStringLiteral("overlayEnabledChanged"),
              this, SLOT(remoteOverlayEnabledChanged(bool)));
  bus.connect(QString::fromLatin1(serviceName), QString::fromLatin1(objectPath),
              QString::fromLatin1(interfaceName), QStringLiteral("spotlightActiveChanged"),
              this, SLOT(remoteSpotlightActiveChanged(bool)));
  bus.connect(QString::fromLatin1(serviceName), QString::fromLatin1(objectPath),
              QString::fromLatin1(interfaceName), QStringLiteral("connectedDevicesChanged"),
              this, SLOT(remoteConnectedDevicesChanged(QStringList)));
  bus.connect(QString::fromLatin1(serviceName), QString::fromLatin1(objectPath),
              QString::fromLatin1(interfaceName),
              QStringLiteral("connectedDeviceBatteryLevelsChanged"),
              this, SLOT(remoteConnectedDeviceBatteryLevelsChanged(QList<int>)));
  bus.connect(QString::fromLatin1(serviceName), QString::fromLatin1(objectPath),
              QString::fromLatin1(interfaceName),
              QStringLiteral("connectedDeviceBatteryStatusesChanged"),
              this, SLOT(remoteConnectedDeviceBatteryStatusesChanged(QStringList)));
  bus.connect(QString::fromLatin1(serviceName), QString::fromLatin1(objectPath),
              QString::fromLatin1(interfaceName), QStringLiteral("presetsChanged"),
              this, SLOT(remotePresetsChanged(QStringList)));
  bus.connect(QString::fromLatin1(serviceName), QString::fromLatin1(objectPath),
              QString::fromLatin1(interfaceName), QStringLiteral("currentPresetChanged"),
              this, SLOT(remoteCurrentPresetChanged(QString)));

  const auto registered = bus.interface()->isServiceRegistered(QString::fromLatin1(serviceName));
  if (registered.isValid() && registered.value()) {
    serviceRegistered(QString::fromLatin1(serviceName));
  }
}

void ProjecteurApplet::setOverlayEnabled(bool enabled)
{
  call(QStringLiteral("SetOverlayEnabled"), {enabled});
}

void ProjecteurApplet::setSpotlightActive(bool active)
{
  call(QStringLiteral("SetSpotlightActive"), {active});
}

void ProjecteurApplet::loadPreset(const QString& preset)
{
  call(QStringLiteral("LoadPreset"), {preset});
}

void ProjecteurApplet::showPreferences()
{
  call(QStringLiteral("ShowPreferences"));
}

void ProjecteurApplet::showAbout()
{
  call(QStringLiteral("ShowAbout"));
}

void ProjecteurApplet::quitProjecteur()
{
  call(QStringLiteral("Quit"));
}

void ProjecteurApplet::serviceRegistered(const QString& service)
{
  if (service != QString::fromLatin1(serviceName)) { return; }
  if (!m_serviceAvailable) {
    m_serviceAvailable = true;
    emit serviceAvailableChanged();
  }
  QTimer::singleShot(0, this, &ProjecteurApplet::refresh);
}

void ProjecteurApplet::serviceUnregistered(const QString& service)
{
  if (service != QString::fromLatin1(serviceName)) { return; }
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

void ProjecteurApplet::refresh()
{
  QDBusInterface interface(QString::fromLatin1(serviceName), QString::fromLatin1(objectPath),
                           QString::fromLatin1(interfaceName), QDBusConnection::sessionBus());
  if (!interface.isValid()) { return; }

  const bool trayVisible = interface.property("TrayVisible").toBool();
  if (m_trayVisible != trayVisible) {
    m_trayVisible = trayVisible;
    emit trayVisibleChanged();
  }
  remoteOverlayEnabledChanged(interface.property("OverlayEnabled").toBool());
  remoteSpotlightActiveChanged(interface.property("SpotlightActive").toBool());
  remoteConnectedDevicesChanged(interface.property("ConnectedDevices").toStringList());
  remoteConnectedDeviceBatteryLevelsChanged(
    qdbus_cast<QList<int>>(interface.property("ConnectedDeviceBatteryLevels")));
  remoteConnectedDeviceBatteryStatusesChanged(
    interface.property("ConnectedDeviceBatteryStatuses").toStringList());
  remotePresetsChanged(interface.property("Presets").toStringList());
  remoteCurrentPresetChanged(interface.property("CurrentPreset").toString());
}

void ProjecteurApplet::resetState()
{
  if (!m_trayVisible) {
    m_trayVisible = true;
    emit trayVisibleChanged();
  }
  remoteOverlayEnabledChanged(true);
  remoteSpotlightActiveChanged(false);
  remoteConnectedDevicesChanged({});
  remoteConnectedDeviceBatteryLevelsChanged({});
  remoteConnectedDeviceBatteryStatusesChanged({});
  remotePresetsChanged({});
  remoteCurrentPresetChanged({});
}

void ProjecteurApplet::call(const QString& method, const QVariantList& arguments)
{
  if (!m_serviceAvailable) { return; }
  QDBusInterface interface(QString::fromLatin1(serviceName), QString::fromLatin1(objectPath),
                           QString::fromLatin1(interfaceName), QDBusConnection::sessionBus());
  if (!interface.isValid()) { return; }
  interface.asyncCallWithArgumentList(method, arguments);
}

K_PLUGIN_CLASS_WITH_JSON(ProjecteurApplet, "metadata.json")

#include "projecteurapplet.moc"
