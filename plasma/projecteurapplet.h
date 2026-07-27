// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md
#pragma once

#include <Plasma/Applet>

#include <QList>
#include <QStringList>

class QDBusServiceWatcher;

class ProjecteurApplet : public Plasma::Applet
{
  Q_OBJECT
  Q_PROPERTY(bool serviceAvailable READ serviceAvailable NOTIFY serviceAvailableChanged)
  Q_PROPERTY(bool trayVisible READ trayVisible NOTIFY trayVisibleChanged)
  Q_PROPERTY(bool overlayEnabled READ overlayEnabled NOTIFY overlayEnabledChanged)
  Q_PROPERTY(bool spotlightActive READ spotlightActive NOTIFY spotlightActiveChanged)
  Q_PROPERTY(QStringList connectedDevices READ connectedDevices NOTIFY connectedDevicesChanged)
  Q_PROPERTY(QList<int> connectedDeviceBatteryLevels READ connectedDeviceBatteryLevels
             NOTIFY connectedDeviceBatteryLevelsChanged)
  Q_PROPERTY(QStringList connectedDeviceBatteryStatuses READ connectedDeviceBatteryStatuses
             NOTIFY connectedDeviceBatteryStatusesChanged)
  Q_PROPERTY(QStringList presets READ presets NOTIFY presetsChanged)
  Q_PROPERTY(QString currentPreset READ currentPreset NOTIFY currentPresetChanged)

public:
  ProjecteurApplet(QObject* parent, const KPluginMetaData& data, const QVariantList& args);

  bool serviceAvailable() const { return m_serviceAvailable; }
  bool trayVisible() const { return m_trayVisible; }
  bool overlayEnabled() const { return m_overlayEnabled; }
  bool spotlightActive() const { return m_spotlightActive; }
  QStringList connectedDevices() const { return m_connectedDevices; }
  QList<int> connectedDeviceBatteryLevels() const { return m_connectedDeviceBatteryLevels; }
  QStringList connectedDeviceBatteryStatuses() const { return m_connectedDeviceBatteryStatuses; }
  QStringList presets() const { return m_presets; }
  QString currentPreset() const { return m_currentPreset; }

  Q_INVOKABLE void setOverlayEnabled(bool enabled);
  Q_INVOKABLE void setSpotlightActive(bool active);
  Q_INVOKABLE void loadPreset(const QString& preset);
  Q_INVOKABLE void showPreferences();
  Q_INVOKABLE void showAbout();
  Q_INVOKABLE void quitProjecteur();

signals:
  void serviceAvailableChanged();
  void trayVisibleChanged();
  void overlayEnabledChanged();
  void spotlightActiveChanged();
  void connectedDevicesChanged();
  void connectedDeviceBatteryLevelsChanged();
  void connectedDeviceBatteryStatusesChanged();
  void presetsChanged();
  void currentPresetChanged();

private slots:
  void serviceRegistered(const QString& service);
  void serviceUnregistered(const QString& service);
  void remoteOverlayEnabledChanged(bool enabled);
  void remoteSpotlightActiveChanged(bool active);
  void remoteConnectedDevicesChanged(const QStringList& devices);
  void remoteConnectedDeviceBatteryLevelsChanged(const QList<int>& levels);
  void remoteConnectedDeviceBatteryStatusesChanged(const QStringList& statuses);
  void remotePresetsChanged(const QStringList& presets);
  void remoteCurrentPresetChanged(const QString& preset);

private:
  void refresh();
  void resetState();
  void call(const QString& method, const QVariantList& arguments = {});

  QDBusServiceWatcher* m_serviceWatcher = nullptr;
  bool m_serviceAvailable = false;
  bool m_trayVisible = true;
  bool m_overlayEnabled = true;
  bool m_spotlightActive = false;
  QStringList m_connectedDevices;
  QList<int> m_connectedDeviceBatteryLevels;
  QStringList m_connectedDeviceBatteryStatuses;
  QStringList m_presets;
  QString m_currentPreset;
};
