// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "device-command-helper.h"

#include "device-hidpp.h"
#include "spotlight.h"

// -------------------------------------------------------------------------------------------------
DeviceCommandHelper::DeviceCommandHelper(QObject* parent, Spotlight* spotlight)
  : QObject(parent), m_spotlight(spotlight)
{

}

// -------------------------------------------------------------------------------------------------
DeviceCommandHelper::~DeviceCommandHelper() = default;


// -------------------------------------------------------------------------------------------------
bool DeviceCommandHelper::sendVibrateCommand(uint8_t intensity, uint8_t length)
{
  if (m_spotlight.isNull()) {
    return false;
  }

  bool commandSent = false;
  for (const auto& device : m_spotlight->connectedDevices()) {
    commandSent = sendVibrateCommand(device.id, intensity, length) || commandSent;
  }
  return commandSent;
}

// -------------------------------------------------------------------------------------------------
bool DeviceCommandHelper::sendVibrateCommand(const DeviceId& deviceId, uint8_t intensity,
                                             uint8_t length)
{
  if (m_spotlight.isNull()) {
    return false;
  }

  const auto connection = m_spotlight->deviceConnection(deviceId);
  if (!connection || !connection->hasHidppSupport()) {
    return false;
  }

  bool commandSent = false;
  for (const auto& subInfo : connection->subDevices())
  {
    const auto& subConnection = subInfo.second;
    if (!subConnection || !subConnection->hasFlags(DeviceFlag::Vibrate)) {
      continue;
    }

    if (const auto hidppConnection =
          std::dynamic_pointer_cast<SubHidppConnection>(subConnection))
    {
      hidppConnection->sendVibrateCommand(intensity, length,
      [](HidppConnectionInterface::MsgResult, HIDPP::Message&&) {
          // logDebug(hid) << tr("Vibrate command returned: %1 (%2)")
          //        .arg(toString(result)).arg(msg.hex());
      });
      commandSent = true;
    }
  }

  return commandSent;
}
