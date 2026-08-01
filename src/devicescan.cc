// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "devicescan.h"

#include <KLocalizedString>

#include <array>

#include <QDirIterator>
#include <QFileInfo>
#include <QTextStream>

#include <linux/input.h>

// Function declaration to check for extra devices, definition in generated source
bool isExtraDeviceSupported(quint16 vendorId, quint16 productId);
QString getExtraDeviceName(quint16 vendorId, quint16 productId);

namespace {
  // -----------------------------------------------------------------------------------------------
  // List of supported devices
  const std::array<SupportedDevice, 4> supportedDefaultDevices {{
    {0x46d, 0xc53e, false, "Logitech Spotlight (USB)"},
    {0x46d, 0xb503, true, "Logitech Spotlight (Bluetooth)"},
    {0x46d, 0xc548, false, "Logitech Spotlight 2 (USB-C receiver)"},
    {0x46d, 0xb506, true, "Logitech Spotlight 2 (Bluetooth)"},
  }};

  // -----------------------------------------------------------------------------------------------
  bool isDeviceSupported(quint16 vendorId, quint16 productId)
  {
    const auto it = std::find_if(supportedDefaultDevices.cbegin(), supportedDefaultDevices.cend(),
    [vendorId, productId](const SupportedDevice& d) {
      return (vendorId == d.vendorId) && (productId == d.productId);
    });
    return (it != supportedDefaultDevices.cend()) || isExtraDeviceSupported(vendorId, productId);
  }

  // -----------------------------------------------------------------------------------------------
  bool isAdditionallySupported(quint16 vendorId, quint16 productId,
                               const std::vector<SupportedDevice>& devices)
  {
    const auto it = std::find_if(devices.cbegin(), devices.cend(),
    [vendorId, productId](const SupportedDevice& d) {
      return (vendorId == d.vendorId) && (productId == d.productId);
    });
    return (it != devices.cend());
  }

  // -----------------------------------------------------------------------------------------------
  // Return the defined device name for vendor/productId if defined in
  // any of the supported device lists (default, extra, additional)
  QString getUserDeviceName(quint16 vendorId, quint16 productId,
                            const std::vector<SupportedDevice>& additionalDevices)
  {
    const auto it = std::find_if(supportedDefaultDevices.cbegin(), supportedDefaultDevices.cend(),
    [vendorId, productId](const SupportedDevice& d) {
      return (vendorId == d.vendorId) && (productId == d.productId);
    });
    if (it != supportedDefaultDevices.cend() && it->name.size()) { return it->name; }

    auto extraName = getExtraDeviceName(vendorId, productId);
    if (!extraName.isEmpty()) { return  extraName; }

    const auto ait = std::find_if(additionalDevices.cbegin(), additionalDevices.cend(),
    [vendorId, productId](const SupportedDevice& d) {
      return (vendorId == d.vendorId) && (productId == d.productId);
    });
    if (ait != additionalDevices.cend() && ait->name.size()) { return ait->name; }
    return QString();
  }

  // -----------------------------------------------------------------------------------------------
  quint64 readULongLongFromDeviceFile(const QString& filename)
  {
    QFile f(filename);
    if (f.open(QIODevice::ReadOnly)) {
      return f.readAll().trimmed().toULongLong(nullptr, 16);
    }
    return 0;
  }

  // -----------------------------------------------------------------------------------------------
  QString readStringFromDeviceFile(const QString& filename)
  {
    QFile f(filename);
    if (f.open(QIODevice::ReadOnly)) {
      return f.readAll().trimmed();
    }
    return QString();
  }

  // -----------------------------------------------------------------------------------------------
  QString readPropertyFromDeviceFile(const QString& filename, const QString& property)
  {
    QFile f(filename);
    if (f.open(QIODevice::ReadOnly)) {
      auto contents = f.readAll();
      QTextStream in(&contents, QIODevice::ReadOnly);
      while (!in.atEnd())
      {
        const auto line = in.readLine();
        if (line.startsWith(property) && line.size() > property.size() && line[property.size()] == '=')
        {
          return line.mid(property.size() + 1);
        }
      }
    }
    return QString();
  }

  // -----------------------------------------------------------------------------------------------
  DeviceScan::Device deviceFromUEventFile(const QString& filename)
  {
    QFile f(filename);
    DeviceScan::Device spotlightDevice;
    static const QString hid_id("HID_ID");
    static const QString hid_name("HID_NAME");
    static const QString hid_phys("HID_PHYS");
    static const std::array<const QString*, 3> properties = {{ &hid_id, &hid_name, &hid_phys }};

    if (!f.open(QIODevice::ReadOnly)) {
      return spotlightDevice;
    }

    auto contents = f.readAll();
    QTextStream in(&contents, QIODevice::ReadOnly);
    while (!in.atEnd())
    {
      const auto line = in.readLine();
      for (const auto property : properties)
      {
        if (line.startsWith(*property) && line.size() > property->size() && line[property->size()] == '=')
        {
          const QString value = line.mid(property->size() + 1);

          if (*property == hid_id)
          {
            const auto ids = value.split(':');
            const auto busType = ids.empty() ? 0: ids[0].toUShort(nullptr, 16);
            switch (busType)
            {
              case BUS_USB: spotlightDevice.id.busType = BusType::Usb; break;
              case BUS_BLUETOOTH: spotlightDevice.id.busType = BusType::Bluetooth; break;
              default: spotlightDevice.id.busType = BusType::Unknown;
            }
            spotlightDevice.id.vendorId = ids.size() > 1 ? ids[1].toUShort(nullptr, 16) : 0;
            spotlightDevice.id.productId = ids.size() > 2 ? ids[2].toUShort(nullptr, 16) : 0;
          }
          else if (*property == hid_name)
          {
            spotlightDevice.name = value;
          }
          else if (*property == hid_phys)
          {
            spotlightDevice.id.phys = value.split('/').first();
          }
        }
      }
    }
    return spotlightDevice;
  }
} // end anonymous namespace

namespace DeviceScan {
  // -----------------------------------------------------------------------------------------------
  ScanResult getDevices(const std::vector<SupportedDevice>& additionalDevices)
  {
    const QString hidDevicePath = QStringLiteral("/sys/bus/hid/devices");

    ScanResult result;
    const QFileInfo dpInfo(hidDevicePath);

    if (!dpInfo.exists()) {
      result.errorMessages.push_back(i18n("HID device path '%1' does not exist.", hidDevicePath));
      return result;
    }

    if (!dpInfo.isExecutable()) {
      result.errorMessages.push_back(
        i18n("HID device path '%1': Cannot list files.", hidDevicePath));
      return result;
    }

    QDirIterator hidIt(hidDevicePath, QDir::System | QDir::Dirs | QDir::Executable | QDir::NoDotAndDotDot);
    while (hidIt.hasNext())
    {
      hidIt.next();

      const QFileInfo uEventFile(QDir(hidIt.filePath()).filePath("uevent"));
      if (!uEventFile.exists()) { continue; }

      // Get basic information from uevent file
      Device newDevice = deviceFromUEventFile(uEventFile.filePath());
      const auto& deviceId = newDevice.id;
      // Skip unsupported devices
      if (deviceId.vendorId == 0 || deviceId.productId == 0) { continue; }
      if (!isDeviceSupported(deviceId.vendorId, deviceId.productId)
          && !(isAdditionallySupported(deviceId.vendorId, deviceId.productId, additionalDevices))) { continue; }

      // Check if device is already in list (and we have another sub-device for it)
      const auto find_it = std::find_if(result.devices.begin(), result.devices.end(),
      [&newDevice](const Device& existingDevice){
        return existingDevice.id == newDevice.id;
      });

      Device& rootDevice = [&find_it, &result, &newDevice, &additionalDevices]() -> Device&
      {
        if (find_it == result.devices.end())
        {
          newDevice.userName = getUserDeviceName(newDevice.id.vendorId, newDevice.id.productId, additionalDevices);
          result.devices.emplace_back(std::move(newDevice));
          return result.devices.back();
        }
        return *find_it;
      }();

      int eventSubDeviceCount = 0;

      // Iterate over 'input' sub-dircectory, check for input-hid device nodes
      const QFileInfo inputSubdir(QDir(hidIt.filePath()).filePath("input"));
      if (inputSubdir.exists() || inputSubdir.isExecutable())
      {
        QDirIterator inputIt(inputSubdir.filePath(), QDir::System | QDir::Dirs | QDir::Executable | QDir::NoDotAndDotDot);
        while (inputIt.hasNext())
        {
          inputIt.next();

          SubDevice subDevice;
          QDirIterator dirIt(inputIt.filePath(), QDir::System | QDir::Dirs | QDir::Executable | QDir::NoDotAndDotDot);
          while (dirIt.hasNext())
          {
            dirIt.next();
            if (!dirIt.fileName().startsWith("event")) { continue; }
            subDevice.type = SubDevice::Type::Event;
            subDevice.deviceFile = readPropertyFromDeviceFile(QDir(dirIt.filePath()).filePath("uevent"), "DEVNAME");
            if (!subDevice.deviceFile.isEmpty()) {
              subDevice.deviceFile = QDir("/dev").filePath(subDevice.deviceFile);
              break;
            }
          }

          if (subDevice.deviceFile.isEmpty()) { continue; }
          subDevice.phys = readStringFromDeviceFile(QDir(inputIt.filePath()).filePath("phys"));
          ++eventSubDeviceCount;

          // Check if device supports relative events
          const auto supportedEvents = readULongLongFromDeviceFile(QDir(inputIt.filePath()).filePath("capabilities/ev"));
          const bool hasRelativeEvents = !!(supportedEvents & (1 << EV_REL));

          // Check if device supports relative x and y event types
          const auto supportedRelEv = readULongLongFromDeviceFile(QDir(inputIt.filePath()).filePath("capabilities/rel"));
          const bool hasRelXEvents = !!(supportedRelEv & (1 << REL_X));
          const bool hasRelYEvents = !!(supportedRelEv & (1 << REL_Y));

          subDevice.hasRelativeEvents = hasRelativeEvents && hasRelXEvents && hasRelYEvents;

          const QFileInfo fi(subDevice.deviceFile);
          subDevice.deviceReadable = fi.isReadable();
          subDevice.deviceWritable = fi.isWritable();

          rootDevice.subDevices.emplace_back(std::move(subDevice));
        }
      }

      // Spotlight (Bluetooth) have hidraw interface in the same folder. However
      // for other connection, it has separate folder for hidraw device and input device.
      if (!(rootDevice.id.busType == BusType::Bluetooth) && eventSubDeviceCount > 0) { continue; }

      // Iterate over 'hidraw' sub-dircectory, check for hidraw device node
      const QFileInfo hidrawSubdir(QDir(hidIt.filePath()).filePath("hidraw"));
      if (hidrawSubdir.exists() || hidrawSubdir.isExecutable())
      {
        QDirIterator hidrawIt(hidrawSubdir.filePath(), QDir::System | QDir::Dirs | QDir::Executable | QDir::NoDotAndDotDot);
        while (hidrawIt.hasNext())
        {
          hidrawIt.next();
          if (!hidrawIt.fileName().startsWith("hidraw")) { continue; }
          SubDevice subDevice;
          subDevice.deviceFile = readPropertyFromDeviceFile(QDir(hidrawIt.filePath()).filePath("uevent"), "DEVNAME");
          if (!subDevice.deviceFile.isEmpty()) {
            subDevice.type = SubDevice::Type::Hidraw;
            subDevice.deviceFile = QDir("/dev").filePath(subDevice.deviceFile);
            if (subDevice.deviceFile.isEmpty()) { continue; }
            const QFileInfo fi(subDevice.deviceFile);
            subDevice.deviceReadable = fi.isReadable();
            subDevice.deviceWritable = fi.isWritable();

            rootDevice.subDevices.emplace_back(std::move(subDevice));
          }
        }
      }
    }

    for (const auto& dev : result.devices)
    {
      const bool allReadable = std::all_of(dev.subDevices.cbegin(), dev.subDevices.cend(),
      [](const SubDevice& subDevice){
        return subDevice.deviceReadable;
      });

      const bool allWriteable = std::all_of(dev.subDevices.cbegin(), dev.subDevices.cend(),
      [](const SubDevice& subDevice){
        return subDevice.deviceWritable;
      });

      result.numDevicesReadable += allReadable;
      result.numDevicesWritable += allWriteable;
    }

    return result;
  }
} // end namespace DeviceScan
