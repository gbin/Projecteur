// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md
#pragma once

#include "device-defs.h"

#include <QPointer>
#include <QWidget>

#include <map>
#include <vector>
#include <utility>

class DeviceConnection;
class InputMapper;
class MultiTimerWidget;
class QComboBox;
class QShowEvent;
class QTabWidget;
class QTextEdit;
class Settings;
class Spotlight;
class VibrationSettingsWidget;
class SubDeviceConnection;
class SubHidppConnection;
class TimerTabWidget;

// -------------------------------------------------------------------------------------------------
class DevicesWidget : public QWidget
{
  Q_OBJECT

public:
  explicit DevicesWidget(Settings* settings, Spotlight* spotlight, QWidget* parent = nullptr);
  DeviceId currentDeviceId() const;

signals:
  void currentDeviceChanged(const DeviceId&);

protected:
  void showEvent(QShowEvent* event) override;

private:
  QWidget* createDisconnectedStateWidget();
  void createDeviceComboBox(Spotlight* spotlight);
  QWidget* createDevicesWidget(Settings* settings, Spotlight* spotlight);
  QWidget* createInputMapperWidget(Settings* settings, Spotlight* spotlight);
  QWidget* createDeviceInfoWidget(Spotlight* spotlight);
  TimerTabWidget* createTimerTabWidget(Settings* settings, Spotlight* spotlight);
  void updateTimerTab(Spotlight* spotlight);

  QComboBox* m_devicesCombo = nullptr;
  QTabWidget* m_tabWidget = nullptr;
  TimerTabWidget* m_timerTabWidget = nullptr;
  QPointer<QObject> m_timerTabContext;
  QWidget* m_deviceDetailsTabWidget = nullptr;

  QPointer<InputMapper> m_inputMapper;
  QPointer<Spotlight> m_spotlight;
};

// -------------------------------------------------------------------------------------------------
class TimerTabWidget : public QWidget
{
  Q_OBJECT

public:
  TimerTabWidget(Settings* settings, QWidget* parent = nullptr);
  VibrationSettingsWidget* vibrationSettingsWidget();

  void loadSettings(const DeviceId& deviceId);
  void setSubDeviceConnection(SubDeviceConnection* sdc);

private:
  DeviceId m_deviceId;
  Settings* const m_settings = nullptr;
  MultiTimerWidget* m_multiTimerWidget = nullptr;
  VibrationSettingsWidget* m_vibrationSettingsWidget = nullptr;
};

// -------------------------------------------------------------------------------------------------
class DeviceInfoWidget : public QWidget
{
  Q_OBJECT

public:
  DeviceInfoWidget(QWidget* parent = nullptr);
  void setDeviceConnection(DeviceConnection* connection);

private:
  void initSubdeviceInfo();
  void updateSubdeviceInfo(SubDeviceConnection* sdc);
  void connectToSubdeviceUpdates(SubDeviceConnection* sdc);
  void connectToBatteryUpdates(SubHidppConnection* hdc);
  void updateHidppInfo(SubHidppConnection* hdc);
  void updateBatteryInfo(SubHidppConnection* hdc);

  void delayedTextEditUpdate();
  void updateTextEdit();

  QTextEdit* m_textEdit = nullptr;
  QTimer* m_delayedUpdateTimer = nullptr;
  QTimer* m_batteryInfoTimer = nullptr;

  std::vector<std::pair<QString, QString>> m_deviceBaseInfo;

  struct SubDeviceInfo {
    QString info;
    bool isHidpp = false;
    bool hasBatteryInfo = false;
  };

  std::map<QString, SubDeviceInfo> m_subDevices;
  QString m_batteryInfo;

  struct HidppInfo {
    QString receiverState;
    QString presenterState;
    QString protocolVersion;
    QStringList hidppFlags;

    void clear()
    {
      receiverState.clear();
      presenterState.clear();
      protocolVersion.clear();
      hidppFlags.clear();
    }
  };

  HidppInfo m_hidppInfo;

  QPointer<QObject> m_connectionContext;
  QPointer<DeviceConnection> m_connection;
};
