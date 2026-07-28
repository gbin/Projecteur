// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "device.h"

#include "deviceinput.h"
#include "devicescan.h"
#include "enum-helper.h"
#include "hidpp.h"
#include "projecteur_device_debug.h"
#include "projecteur_hid_debug.h"

#include <QSocketNotifier>
#include <QTimer>

#include <fcntl.h>
#include <linux/hidraw.h>
#include <unistd.h>

namespace  {
  const auto hexId = formatHexId;
} // end anonymous namespace

// -------------------------------------------------------------------------------------------------
const char* toString(BusType bt, bool withClass)
{
  switch (bt) {
    ENUM_CASE_STRINGIFY3(BusType, Unknown, withClass);
    ENUM_CASE_STRINGIFY3(BusType, Usb, withClass);
    ENUM_CASE_STRINGIFY3(BusType, Bluetooth, withClass);
  }
  return withClass ? "BusType::(unknown)" : "(unkown)";
}

// -------------------------------------------------------------------------------------------------
const char* toString(ConnectionType ct, bool withClass)
{
  switch (ct) {
    ENUM_CASE_STRINGIFY3(ConnectionType, Event, withClass);
    ENUM_CASE_STRINGIFY3(ConnectionType, Hidraw, withClass);
  }
  return withClass ? "ConnectionType::(unknown)" : "(unkown)";
}

// -------------------------------------------------------------------------------------------------
const char* toString(ConnectionMode cm, bool withClass)
{
  switch (cm) {
    ENUM_CASE_STRINGIFY3(ConnectionMode, ReadOnly, withClass);
    ENUM_CASE_STRINGIFY3(ConnectionMode, WriteOnly, withClass);
    ENUM_CASE_STRINGIFY3(ConnectionMode, ReadWrite, withClass);
  }
  return withClass ? "ConnectionMode::(unknown)" : "(unkown)";
}

// -------------------------------------------------------------------------------------------------
DeviceConnection::DeviceConnection(const DeviceId& id, const QString& name,
                                   std::shared_ptr<VirtualDevice> vmouse,
                                   std::shared_ptr<VirtualDevice> vkeyboard)
  : m_deviceId(id)
  , m_deviceName(name)
  , m_inputMapper(std::make_shared<InputMapper>(std::move(vmouse), std::move(vkeyboard)))
{
}

// -------------------------------------------------------------------------------------------------
DeviceConnection::~DeviceConnection() = default;

// -------------------------------------------------------------------------------------------------
bool DeviceConnection::hasSubDevice(const QString& path) const
{
  const auto find_it = m_subDeviceConnections.find(path);
  return (find_it != m_subDeviceConnections.end() && find_it->second && find_it->second->isConnected());
}

// -------------------------------------------------------------------------------------------------
void DeviceConnection::addSubDevice(std::shared_ptr<SubDeviceConnection> sdc)
{
  if (!sdc) { return; }

  const auto path = sdc->path();
  connect(&*sdc, &SubDeviceConnection::flagsChanged, this, [this, path](){
    emit subDeviceFlagsChanged(m_deviceId, path);
  });

  m_subDeviceConnections[path] = std::move(sdc);
  emit subDeviceConnected(m_deviceId, path);
}

// -------------------------------------------------------------------------------------------------
bool DeviceConnection::removeSubDevice(const QString& path)
{
  auto find_it = m_subDeviceConnections.find(path);
  if (find_it != m_subDeviceConnections.end())
  {
    if (find_it->second) { find_it->second->disconnect(); } // Important
    qCDebug(PROJECTEUR_DEVICE_LOG).noquote() << QStringLiteral("Disconnected sub-device: %1 (%2:%3) %4").arg(m_deviceName).arg(hexId(m_deviceId.vendorId)).arg(hexId(m_deviceId.productId)).arg(path);
    emit subDeviceDisconnected(m_deviceId, path);
    m_subDeviceConnections.erase(find_it);
    return true;
  }
  return false;
}

// -------------------------------------------------------------------------------------------------
std::shared_ptr<SubDeviceConnection> DeviceConnection::subDevice(const QString& devicePath) const
{
  const auto it = m_subDeviceConnections.find(devicePath);
  if (it == m_subDeviceConnections.cend()) {
    return {};
  }

  return it->second;
}

// -------------------------------------------------------------------------------------------------
bool DeviceConnection::hasHidppSupport() const {
  // HID++ only for Logitech devices
  return m_deviceId.vendorId == 0x046d;
}

// -------------------------------------------------------------------------------------------------
SubDeviceConnectionDetails::SubDeviceConnectionDetails(const DeviceId& dId, const DeviceScan::SubDevice& sd,
                                                       ConnectionType type, ConnectionMode mode)
  : deviceId(dId), type(type), mode(mode), devicePath(sd.deviceFile)
{}

// -------------------------------------------------------------------------------------------------
SubDeviceConnection::SubDeviceConnection(const DeviceId& dId, const DeviceScan::SubDevice& sd,
                                         ConnectionType type, ConnectionMode mode)
  : m_details(dId, sd, type, mode) {}

// -------------------------------------------------------------------------------------------------
SubDeviceConnection::~SubDeviceConnection() = default;

// -------------------------------------------------------------------------------------------------
DeviceFlags SubDeviceConnection::setFlags(DeviceFlags f, bool set)
{
  const auto previousFlags = flags();
  if (set) {
    m_details.deviceFlags |= f;
  } else {
    m_details.deviceFlags &= ~f;
  }

  if (m_details.deviceFlags != previousFlags) {
    emit flagsChanged(m_details.deviceFlags);
  }
  return m_details.deviceFlags;
}

// -------------------------------------------------------------------------------------------------
bool SubDeviceConnection::isConnected() const {
  return false;
}

// -------------------------------------------------------------------------------------------------
void SubDeviceConnection::disconnect() {
  if (m_readNotifier) {
    m_readNotifier->setEnabled(false);
    m_readNotifier.reset();
  }
}

// -------------------------------------------------------------------------------------------------
const std::shared_ptr<InputMapper>& SubDeviceConnection::inputMapper() const  {
  return m_inputMapper;
}

// -------------------------------------------------------------------------------------------------
QSocketNotifier* SubDeviceConnection::socketReadNotifier() {
  return m_readNotifier.get();
}

// -------------------------------------------------------------------------------------------------
SubEventConnection::SubEventConnection(Token /* token */,
                                       const DeviceId& dId, const DeviceScan::SubDevice& sd)
  : SubDeviceConnection(dId, sd, ConnectionType::Event, ConnectionMode::ReadOnly) {}

// -------------------------------------------------------------------------------------------------
SubEventConnection::~SubEventConnection() = default;

// -------------------------------------------------------------------------------------------------
bool SubEventConnection::isConnected() const {
  return (m_readNotifier && m_readNotifier->isEnabled());
}

// -------------------------------------------------------------------------------------------------
std::shared_ptr<SubEventConnection> SubEventConnection::create(const DeviceScan::SubDevice& sd,
                                                               const DeviceConnection& dc)
{
  const int evfd = ::open(sd.deviceFile.toLocal8Bit().constData(), O_RDONLY, 0);

  if (evfd == -1) {
    qCWarning(PROJECTEUR_DEVICE_LOG).noquote() << QStringLiteral("Cannot open event device '%1' for read.").arg(sd.deviceFile);
    return std::shared_ptr<SubEventConnection>();
  }

  struct input_id id{};
  ioctl(evfd, EVIOCGID, &id); // get the event sub-device id

  // Check against given device id
  if (id.vendor != dc.deviceId().vendorId || id.product != dc.deviceId().productId)
  {
    ::close(evfd);
    qCDebug(PROJECTEUR_DEVICE_LOG).noquote() << QStringLiteral("Device id mismatch: %1 (%2:%3)").arg(sd.deviceFile).arg(hexId(id.vendor)).arg(hexId(id.product));
    return std::shared_ptr<SubEventConnection>();
  }

  unsigned long bitmask = 0;
  if (ioctl(evfd, EVIOCGBIT(0, sizeof(bitmask)), &bitmask) < 0)
  {
    ::close(evfd);
    qCWarning(PROJECTEUR_DEVICE_LOG).noquote() << QStringLiteral("Cannot get device properties: %1 (%2:%3)").arg(sd.deviceFile).arg(hexId(id.vendor)).arg(hexId(id.product));
    return std::shared_ptr<SubEventConnection>();
  }

  auto connection = std::make_shared<SubEventConnection>(Token{}, dc.deviceId(), sd);

  if (!!(bitmask & (1 << EV_SYN))) { connection->m_details.deviceFlags |= DeviceFlag::SynEvents; }
  if (!!(bitmask & (1 << EV_REP))) { connection->m_details.deviceFlags |= DeviceFlag::RepEvents; }
  if (!!(bitmask & (1 << EV_KEY))) { connection->m_details.deviceFlags |= DeviceFlag::KeyEvents; }
  if (!!(bitmask & (1 << EV_REL)))
  {
    unsigned long relEvents = 0;
    ioctl(evfd, EVIOCGBIT(EV_REL, sizeof(relEvents)), &relEvents);
    const bool hasRelXEvents = !!(relEvents & (1 << REL_X));
    const bool hasRelYEvents = !!(relEvents & (1 << REL_Y));
    if (hasRelXEvents && hasRelYEvents) {
      connection->m_details.deviceFlags |= DeviceFlag::RelativeEvents;
    }
  }

  connection->m_details.grabbed = [&dc, evfd, &sd]()
  {
    // Grab device inputs if a virtual device exists.
    if (dc.inputMapper()->hasVirtualDevice())
    {
      const int res = ioctl(evfd, EVIOCGRAB, 1);
      if (res == 0) { return true; }

      // Grab not successful
      qCCritical(PROJECTEUR_DEVICE_LOG).noquote() << QStringLiteral("Error grabbing device: %1 (return value: %2)").arg(sd.deviceFile).arg(res);
      ioctl(evfd, EVIOCGRAB, 0);
    }
    return false;
  }();

  fcntl(evfd, F_SETFL, fcntl(evfd, F_GETFL, 0) | O_NONBLOCK);
  if ((fcntl(evfd, F_GETFL, 0) & O_NONBLOCK) == O_NONBLOCK) {
    connection->m_details.deviceFlags |= DeviceFlag::NonBlocking;
  }

  // Create socket notifier
  connection->m_readNotifier = std::make_unique<QSocketNotifier>(evfd, QSocketNotifier::Read);
  QSocketNotifier* const notifier = connection->m_readNotifier.get();
  // Auto clean up and close descriptor on destruction of notifier
  connect(notifier, &QSocketNotifier::destroyed,
  [grabbed = connection->m_details.grabbed, evfd, path=sd.deviceFile]() {
    if (grabbed) {
      ioctl(evfd, EVIOCGRAB, 0);
    }
    qCDebug(PROJECTEUR_DEVICE_LOG).noquote() << QStringLiteral("Closing file descriptor for '%1'").arg(path);
    ::close(evfd);
  });

  connection->m_inputMapper = dc.inputMapper();
  return connection;
}

// -------------------------------------------------------------------------------------------------
SubHidrawConnection::SubHidrawConnection(Token /* token */,
                                         const DeviceId& dId, const DeviceScan::SubDevice& sd)
  : SubDeviceConnection(dId, sd, ConnectionType::Hidraw, ConnectionMode::ReadWrite) {}

// -------------------------------------------------------------------------------------------------
SubHidrawConnection::~SubHidrawConnection() = default;

// -------------------------------------------------------------------------------------------------
bool SubHidrawConnection::isConnected() const {
  return (m_readNotifier && m_readNotifier->isEnabled()) && (m_writeNotifier);
}

// -------------------------------------------------------------------------------------------------
void SubHidrawConnection::disconnect() {
  SubDeviceConnection::disconnect();
  if (m_writeNotifier) {
    m_writeNotifier->setEnabled(false);
    m_writeNotifier.reset();
  }
}

// -------------------------------------------------------------------------------------------------
std::shared_ptr<SubHidrawConnection> SubHidrawConnection::create(const DeviceScan::SubDevice& sd,
                                                                 const DeviceConnection& dc)
{
  const int devfd = openHidrawSubDevice(sd, dc.deviceId());
  if (devfd == -1) { return std::shared_ptr<SubHidrawConnection>(); }

  auto connection = std::make_shared<SubHidrawConnection>(Token{}, dc.deviceId(), sd);
  connection->createSocketNotifiers(devfd, sd.deviceFile);

  connect(connection->socketReadNotifier(), &QSocketNotifier::activated,
          &*connection, &SubHidrawConnection::onHidrawDataAvailable);

  return connection;
}

// -----------------------------------------------------------------------------------------------
int SubHidrawConnection::openHidrawSubDevice(const DeviceScan::SubDevice& sd, const DeviceId& devId)
{
  constexpr int errorResult = -1;
  const int devfd = ::open(sd.deviceFile.toLocal8Bit().constData(), O_RDWR|O_NONBLOCK , 0);

  if (devfd == errorResult) {
    qCWarning(PROJECTEUR_DEVICE_LOG).noquote() << QStringLiteral("Cannot open hidraw device '%1' for read/write.").arg(sd.deviceFile);
    return errorResult;
  }

  { // Get Report Descriptor Size and Descriptor -- currently unused, but if it fails
    // we don't use the device
    int descriptorSize = 0;
    if (ioctl(devfd, HIDIOCGRDESCSIZE, &descriptorSize) < 0)
    {
      qCWarning(PROJECTEUR_DEVICE_LOG).noquote() << QStringLiteral("Cannot retrieve report descriptor size of hidraw device '%1'.").arg(sd.deviceFile);
      ::close(devfd);
      return errorResult;
    }

    struct hidraw_report_descriptor reportDescriptor {};
    reportDescriptor.size = descriptorSize;
    if (ioctl(devfd, HIDIOCGRDESC, &reportDescriptor) < 0)
    {
      qCWarning(PROJECTEUR_DEVICE_LOG).noquote() << QStringLiteral("Cannot retrieve report descriptor of hidraw device '%1'.").arg(sd.deviceFile);
      ::close(devfd);
      return errorResult;
    }
  }

  struct hidraw_devinfo devinfo {};
  // get the hidraw sub-device id info
  if (ioctl(devfd, HIDIOCGRAWINFO, &devinfo) < 0)
  {
    qCWarning(PROJECTEUR_DEVICE_LOG).noquote() << QStringLiteral("Cannot get info from hidraw device '%1'.").arg(sd.deviceFile);
    ::close(devfd);
    return errorResult;
  };

  // Check against given device id
  if (static_cast<uint16_t>(devinfo.vendor) != devId.vendorId
      || static_cast<uint16_t>(devinfo.product) != devId.productId)
  {
    qCDebug(PROJECTEUR_DEVICE_LOG).noquote() << QStringLiteral("Device id mismatch: %1 (%2:%3)").arg(sd.deviceFile).arg(hexId(devinfo.vendor)).arg(hexId(devinfo.product));
    ::close(devfd);
    return errorResult;
  }

  return devfd;
}

// -------------------------------------------------------------------------------------------------
ssize_t SubHidrawConnection::sendData(const QByteArray& msg)
{
  return sendData(msg.data(), msg.size());
}

// -------------------------------------------------------------------------------------------------
ssize_t SubHidrawConnection::sendData(const void* msg, size_t msgLen)
{
  constexpr ssize_t errorResult = -1;

  if (mode() != ConnectionMode::ReadWrite || !m_writeNotifier) { return errorResult; }
  const auto res = ::write(m_writeNotifier->socket(), msg, msgLen);

  if (static_cast<size_t>(res) == msgLen) {
    qCDebug(PROJECTEUR_HID_LOG).noquote() << res << "bytes written to" << path() << "("
                  << QByteArray::fromRawData(static_cast<const char*>(msg), msgLen).toHex() << ")";
  } else {
    qCWarning(PROJECTEUR_HID_LOG).noquote() << QStringLiteral("Writing to '%1' failed. (%2)").arg(path()).arg(res);
  }

  return res;
}

// -------------------------------------------------------------------------------------------------
void SubHidrawConnection::createSocketNotifiers(int fd, const QString& path)
{
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  if ((fcntl(fd, F_GETFL, 0) & O_NONBLOCK) == O_NONBLOCK) {
    m_details.deviceFlags |= DeviceFlag::NonBlocking;
  }

  // Create read and write socket notifiers
  m_readNotifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read);
  QSocketNotifier *const readNotifier = m_readNotifier.get();
  auto fdPtr = std::make_shared<int>(fd);

  // Auto clean up and close descriptor on destruction of notifier
  connect(readNotifier, &QSocketNotifier::destroyed, [fdPtr, path]()
  {
    if (fdPtr && *fdPtr != -1) {
      qCDebug(PROJECTEUR_DEVICE_LOG).noquote() << QStringLiteral("Closing file descriptor for '%1'").arg(path);
      ::close(*fdPtr);
      *fdPtr = -1;
    }
  });

  m_writeNotifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Write);
  QSocketNotifier *const writeNotifier = m_writeNotifier.get();
  writeNotifier->setEnabled(false); // Disable write notifier by default
  // Auto clean up and close descriptor on destruction of notifier
  connect(writeNotifier, &QSocketNotifier::destroyed, [fdPtr, path]()
  {
    if (fdPtr && *fdPtr != -1) {
      qCDebug(PROJECTEUR_DEVICE_LOG).noquote() << QStringLiteral("Closing file descriptor for '%1'").arg(path);
      ::close(*fdPtr);
      *fdPtr = -1;
    }
  });
}

// -------------------------------------------------------------------------------------------------
void SubHidrawConnection::onHidrawDataAvailable(int fd)
{
  QByteArray readVal(20, 0);
  const auto res = ::read(fd, readVal.data(), readVal.size());
  if (res < 0) {
    if (errno != EAGAIN) {
      emit socketReadError(errno);
    }
    return;
  }

  // For generic hidraw devices without known protocols, just print out the
  // received data into the debug log
  qCDebug(PROJECTEUR_HID_LOG).noquote() << "Received" << readVal.toHex() << "from" << path();
}

// -------------------------------------------------------------------------------------------------
const char* toString(DeviceFlag f, bool withClass)
{
  switch(f) {
    ENUM_CASE_STRINGIFY3(DeviceFlag, NoFlags, withClass);
    ENUM_CASE_STRINGIFY3(DeviceFlag, NonBlocking, withClass);
    ENUM_CASE_STRINGIFY3(DeviceFlag, SynEvents, withClass);
    ENUM_CASE_STRINGIFY3(DeviceFlag, RepEvents, withClass);
    ENUM_CASE_STRINGIFY3(DeviceFlag, RelativeEvents, withClass);
    ENUM_CASE_STRINGIFY3(DeviceFlag, KeyEvents, withClass);
    ENUM_CASE_STRINGIFY3(DeviceFlag, Hidpp, withClass);
    ENUM_CASE_STRINGIFY3(DeviceFlag, Vibrate, withClass);
    ENUM_CASE_STRINGIFY3(DeviceFlag, ReportBattery, withClass);
    ENUM_CASE_STRINGIFY3(DeviceFlag, NextHold, withClass);
    ENUM_CASE_STRINGIFY3(DeviceFlag, BackHold, withClass);
    ENUM_CASE_STRINGIFY3(DeviceFlag, PointerSpeed, withClass);
  }
  return withClass ? "DeviceFlag::(unknown)" : "(unknown)";
}

// -------------------------------------------------------------------------------------------------
QString toString(DeviceFlags flags, const QString& separator, bool withClass)
{
  return toStringList(flags, withClass).join(separator);
}

// -------------------------------------------------------------------------------------------------
QStringList toStringList(DeviceFlags flags, bool withClass)
{
  if (flags == DeviceFlags::NoFlags) {
    return QStringList{ ENUM_STRINGIFY3(DeviceFlag, NoFlags, withClass) };
  }

  QStringList list;

  for (size_t i = 0; i < sizeof(std::underlying_type_t<DeviceFlag>) * 8; ++i)
  {
    const std::underlying_type_t<DeviceFlag> singleFlag = 1 << i;
    if ((to_integral(flags) & singleFlag) == singleFlag) {
      list.push_back(toString(to_enum<DeviceFlag>(singleFlag)));
    }
  }

  return list;
}
