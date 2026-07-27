// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md
#pragma once

#include <QList>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class ProjecteurApplication;
class Settings;
class Spotlight;
struct DeviceId;

class ProjecteurControl : public QObject
{
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.projecteur.Projecteur")
  Q_PROPERTY(bool TrayVisible READ trayVisible CONSTANT)
  Q_PROPERTY(bool OverlayEnabled READ overlayEnabled NOTIFY overlayEnabledChanged)
  Q_PROPERTY(bool SpotlightActive READ spotlightActive NOTIFY spotlightActiveChanged)
  Q_PROPERTY(QStringList ConnectedDevices READ connectedDevices NOTIFY connectedDevicesChanged)
  Q_PROPERTY(QList<int> ConnectedDeviceBatteryLevels READ connectedDeviceBatteryLevels
             NOTIFY connectedDeviceBatteryLevelsChanged)
  Q_PROPERTY(QStringList ConnectedDeviceBatteryStatuses READ connectedDeviceBatteryStatuses
             NOTIFY connectedDeviceBatteryStatusesChanged)
  Q_PROPERTY(QStringList Presets READ presets NOTIFY presetsChanged)
  Q_PROPERTY(QString CurrentPreset READ currentPreset NOTIFY currentPresetChanged)

public:
  static constexpr auto ServiceName = "org.projecteur.Projecteur";
  static constexpr auto ObjectPath = "/org/projecteur/Projecteur";
  static constexpr auto InterfaceName = "org.projecteur.Projecteur";

  ProjecteurControl(ProjecteurApplication* application, Settings* settings, Spotlight* spotlight,
                    bool trayVisible);

  bool registerService();
  void unregisterService();

  bool trayVisible() const { return m_trayVisible; }
  bool overlayEnabled() const;
  bool spotlightActive() const;
  QStringList connectedDevices() const;
  QList<int> connectedDeviceBatteryLevels() const;
  QStringList connectedDeviceBatteryStatuses() const;
  QStringList presets() const;
  QString currentPreset() const { return m_currentPreset; }

public slots:
  void SetOverlayEnabled(bool enabled);
  void SetSpotlightActive(bool active);
  bool LoadPreset(const QString& preset);
  void ShowPreferences();
  void ShowAbout();
  void Quit();

signals:
  void overlayEnabledChanged(bool enabled);
  void spotlightActiveChanged(bool active);
  void connectedDevicesChanged(const QStringList& devices);
  void connectedDeviceBatteryLevelsChanged(const QList<int>& levels);
  void connectedDeviceBatteryStatusesChanged(const QStringList& statuses);
  void presetsChanged(const QStringList& presets);
  void currentPresetChanged(const QString& preset);

private:
  void clearCurrentPreset();
  void emitBatteryPropertiesChanged();
  void requestBatteryUpdates();
  void watchBatteryConnection(const DeviceId& id, const QString& path);
  void emitPropertiesChanged(const QVariantMap& changedProperties);

  ProjecteurApplication* const m_application;
  Settings* const m_settings;
  Spotlight* const m_spotlight;
  const bool m_trayVisible;
  QString m_currentPreset;
  bool m_objectRegistered = false;
  bool m_serviceRegistered = false;
};
