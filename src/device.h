// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md
#pragma once

#include "asynchronous.h"
#include "enum-helper.h"

#include "devicescan.h"

#include <array>
#include <memory>
#include <vector>

#include <QObject>

#include <linux/input.h>


// -------------------------------------------------------------------------------------------------
class InputMapper;
class QSocketNotifier;
class SubDeviceConnection;
class VirtualDevice;

// -------------------------------------------------------------------------------------------------
/// The main device connection class, which usually consists of one or multiple sub devices.
class DeviceConnection : public QObject
{
  Q_OBJECT

public:
  DeviceConnection(const DeviceId& id, const QString& name,
    std::shared_ptr<VirtualDevice> vmouse, std::shared_ptr<VirtualDevice> vkeyboard);

  ~DeviceConnection();

  const auto& deviceName() const { return m_deviceName; }
  const auto& deviceId() const { return m_deviceId; }
  const auto& inputMapper() const { return m_inputMapper; }
  bool hasHidppSupport() const;

  auto subDeviceCount() const { return m_subDeviceConnections.size(); }
  bool hasSubDevice(const QString& path) const;
  void addSubDevice(std::shared_ptr<SubDeviceConnection>);
  bool removeSubDevice(const QString& path);
  const auto& subDevices() { return m_subDeviceConnections; }
  std::shared_ptr<SubDeviceConnection> subDevice(const QString& devicePath) const;

signals:
  void subDeviceConnected(const DeviceId& id, const QString& path);
  void subDeviceDisconnected(const DeviceId& id, const QString& path);
  void subDeviceFlagsChanged(const DeviceId& id, const QString& path);

protected:
  using DevicePath = QString;
  using ConnectionMap = std::map<DevicePath, std::shared_ptr<SubDeviceConnection>>;

  DeviceId m_deviceId;
  QString m_deviceName;
  std::shared_ptr<InputMapper> m_inputMapper;
  ConnectionMap m_subDeviceConnections;
};

// -------------------------------------------------------------------------------------------------
enum class DeviceFlag : uint32_t {
  NoFlags = 0,
  NonBlocking    = 1 << 0,
  SynEvents      = 1 << 1,
  RepEvents      = 1 << 2,
  RelativeEvents = 1 << 3,
  KeyEvents      = 1 << 4,

  Hidpp          = 1 << 15, ///< Device supports hidpp requests
  Vibrate        = 1 << 16, ///< Device supports vibrate commands
  ReportBattery  = 1 << 17, ///< Device can report battery status
  NextHold       = 1 << 18, ///< Device can be configured to send 'Next Hold' event.
  BackHold       = 1 << 19, ///< Device can be configured to send 'Back Hold' event.
  PointerSpeed   = 1 << 20, ///< Device allows changing pointer speed.
  ActionButton   = 1 << 21, ///< Device can be configured to send 'Action Button' event.
};
ENUM(DeviceFlag, DeviceFlags)

// -------------------------------------------------------------------------------------------------
const char* toString(DeviceFlag flag, bool withClass = true);
QString toString(DeviceFlags flags, const QString& separator, bool withClass = true);
QStringList toStringList(DeviceFlags flags, bool withClass = true);

// -------------------------------------------------------------------------------------------------
struct SubDeviceConnectionDetails {
  SubDeviceConnectionDetails(const DeviceId& dId, const DeviceScan::SubDevice& sd,
                             ConnectionType type, ConnectionMode mode);

  DeviceId deviceId;
  ConnectionType type;
  ConnectionMode mode;
  bool grabbed = false;
  DeviceFlags deviceFlags = DeviceFlags::NoFlags;
  QString devicePath;
};

// -------------------------------------------------------------------------------------------------
template<int Size, typename T = struct input_event>
struct InputBuffer {
  auto pos() const { return pos_; }
  void reset() { pos_ = 0; }
  auto data() { return data_.data(); }
  auto size() const { return data_.size(); }
  T& current() { return data_.at(pos_); }
  InputBuffer& operator++() { ++pos_; return *this; }
  T& operator[](size_t pos) { return data_[pos]; }
  T& first() { return data_[0]; }
private:
  std::array<T, Size> data_;
  size_t pos_ = 0;
};

// -------------------------------------------------------------------------------------------------
class SubDeviceConnection : public QObject, public async::Async<SubDeviceConnection>
{
  Q_OBJECT
public:
  virtual ~SubDeviceConnection() = 0;

  virtual bool isConnected() const;
  virtual void disconnect(); // destroys socket notifier(s) and close file handle(s)

  auto type() const { return m_details.type; }
  auto mode() const { return m_details.mode; }
  auto isGrabbed() const { return m_details.grabbed; }
  auto flags() const { return m_details.deviceFlags; }
  const auto& path() const { return m_details.devicePath; }
  const auto& deviceId() const { return m_details.deviceId; }

  inline bool hasFlags(DeviceFlags f) const { return ((flags() & f) == f); }

  const std::shared_ptr<InputMapper>& inputMapper() const;
  QSocketNotifier* socketReadNotifier();   // Read notifier for Hidraw and Event connections for receiving data from device

signals:
  void flagsChanged(DeviceFlags f);
  void socketReadError(int err);

protected:
  SubDeviceConnection(const DeviceId& dId, const DeviceScan::SubDevice& sd, ConnectionType type, ConnectionMode mode);
  DeviceFlags setFlags(DeviceFlags f, bool set = true);

  SubDeviceConnectionDetails m_details;
  std::shared_ptr<InputMapper> m_inputMapper; ///< Shared input mapper from parent device.
  std::unique_ptr<QSocketNotifier> m_readNotifier;
};

// -------------------------------------------------------------------------------------------------
class SubEventConnection : public SubDeviceConnection
{
  Q_OBJECT
  class Token{};

public:
  static std::shared_ptr<SubEventConnection> create(const DeviceScan::SubDevice& sd,
                                                    const DeviceConnection& dc);

  SubEventConnection(Token, const DeviceId&, const DeviceScan::SubDevice&);
  virtual ~SubEventConnection();
  bool isConnected() const;
  auto& inputBuffer() { return m_inputEventBuffer; }

protected:
  InputBuffer<12> m_inputEventBuffer;
};

// -------------------------------------------------------------------------------------------------
class HidrawConnectionInterface
{
  // Generic plain, synchronous sendData interface
  virtual ssize_t sendData(const QByteArray& msg) = 0;
  virtual ssize_t sendData(const void* msg, size_t msgLen) = 0;
};

// -------------------------------------------------------------------------------------------------
class SubHidrawConnection : public SubDeviceConnection, public HidrawConnectionInterface
{
  Q_OBJECT

protected:
  class Token{};

public:
  static std::shared_ptr<SubHidrawConnection> create(const DeviceScan::SubDevice& sd,
                                                     const DeviceConnection& dc);

  SubHidrawConnection(Token, const DeviceId&, const DeviceScan::SubDevice&);
  virtual ~SubHidrawConnection();
  virtual bool isConnected() const override;
  virtual void disconnect() override;

  // Generic plain, synchronous sendData implementation for hidraw devices.
  ssize_t sendData(const QByteArray& msg) override;
  ssize_t sendData(const void* msg, size_t msgLen) override;

protected:
  void createSocketNotifiers(int fd, const QString& path);
  static int openHidrawSubDevice(const DeviceScan::SubDevice& sd, const DeviceId& devId);
  std::unique_ptr<QSocketNotifier> m_writeNotifier;

private:
  void onHidrawDataAvailable(int fd);
};
