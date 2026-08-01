// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#pragma once

#include <cstdint>

#include <QMetaType>
#include <QString>

// Bus on which device is connected
enum class BusType : uint8_t { Unknown, Usb, Bluetooth };

enum class ConnectionType : uint8_t { Event, Hidraw };

enum class ConnectionMode : uint8_t { ReadOnly, WriteOnly, ReadWrite };

// -------------------------------------------------------------------------------------------------
const char* toString(BusType bt, bool withClass = true);
const char* toString(ConnectionType ct, bool withClass = true);
const char* toString(ConnectionMode cm, bool withClass = true);

inline QString formatHexId(uint16_t id)
{
  return QStringLiteral("%1").arg(id, 4, 16, QLatin1Char('0'));
}

// -------------------------------------------------------------------------------------------------
struct DeviceId
{
  uint16_t vendorId = 0;
  uint16_t productId = 0;
  BusType busType = BusType::Unknown;
  QString phys{}; // should be sufficient to differentiate between two devices of the same type
                  // - not tested, don't have two devices of any type currently.

  inline bool operator==(const DeviceId& rhs) const {
    return std::tie(vendorId, productId, busType, phys) == std::tie(rhs.vendorId, rhs.productId, rhs.busType, rhs.phys);
  }

  inline bool operator!=(const DeviceId& rhs) const {
    return std::tie(vendorId, productId, busType, phys) != std::tie(rhs.vendorId, rhs.productId, rhs.busType, rhs.phys);
  }

  inline bool operator<(const DeviceId& rhs) const {
    return std::tie(vendorId, productId, busType, phys) < std::tie(rhs.vendorId, rhs.productId, rhs.busType, rhs.phys);
  }
};
Q_DECLARE_METATYPE(DeviceId);
