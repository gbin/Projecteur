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
class QComboBox;
class QTabWidget;
class QTextEdit;
class Settings;
class Spotlight;
class SubDeviceConnection;
class SubHidppConnection;

// -------------------------------------------------------------------------------------------------
class DevicesWidget : public QWidget
{
  Q_OBJECT

public:
  explicit DevicesWidget(Settings* settings, Spotlight* spotlight, QWidget* parent = nullptr);
  DeviceId currentDeviceId() const;

signals:
  void currentDeviceChanged(const DeviceId&);

private:
  QWidget* createDisconnectedStateWidget();
  void createDeviceComboBox(Spotlight* spotlight);
  QWidget* createDevicesWidget(Settings* settings, Spotlight* spotlight);
  QWidget* createInputMapperWidget(Settings* settings, Spotlight* spotlight);
  QWidget* createDeviceInfoWidget(Spotlight* spotlight);

  QComboBox* m_devicesCombo = nullptr;
  QTabWidget* m_tabWidget = nullptr;
  QWidget* m_deviceDetailsTabWidget = nullptr;

  QPointer<InputMapper> m_inputMapper;
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
