// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "virtualdevice.h"

#include "logging.h"

#include <KLocalizedString>

#include <fcntl.h>
#include <linux/uinput.h>
#include <linux/input-event-codes.h>
#include <unistd.h>

#include <QFileInfo>

LOGGING_CATEGORY(virtualdevice, "virtualdevice")

// KEY_MACRO1 is only defined in newer linux versions
#ifndef KEY_MACRO1
#define KEY_MACRO1 0x290
#endif

struct VirtualDevice::Token {};

// -------------------------------------------------------------------------------------------------
VirtualDevice::VirtualDevice(Token /* token */, int fd, const char* name, const char* sysfs_name)
  : m_uinpFd(fd)
  , m_userName(name)
  , m_deviceName(sysfs_name)
{}

// -------------------------------------------------------------------------------------------------
VirtualDevice::~VirtualDevice()
{
  if (m_uinpFd >= 0)
  {
    ioctl(m_uinpFd, UI_DEV_DESTROY);
    ::close(m_uinpFd);
    logDebug(virtualdevice)
      << i18n("uinput Device Closed (%1; %2)", m_userName, m_deviceName);
  }
}

// -------------------------------------------------------------------------------------------------
// Setup a uinput device that can send mouse or keyboard events.
std::shared_ptr<VirtualDevice> VirtualDevice::create(Type deviceType,
                                                     const char* name,
                                                     uint16_t virtualVendorId,
                                                     uint16_t virtualProductId,
                                                     uint16_t virtualVersionId,
                                                     const char* location)
{
  const QFileInfo fi(location);
  if (!fi.exists()) {
    logWarn(virtualdevice) << i18n("File not found: %1", location);
    logWarn(virtualdevice) << i18n("Please check if uinput kernel module is loaded");
    return std::shared_ptr<VirtualDevice>();
  }

  const int fd = ::open(location, O_WRONLY | O_NDELAY);
  if (fd < 0) {
    logWarn(virtualdevice) << i18n("Unable to open: %1", location);
    logWarn(virtualdevice) << i18n("Please check if current user has write access");
    return std::shared_ptr<VirtualDevice>();
  }

  struct uinput_user_dev uinp {};
  snprintf(uinp.name, sizeof(uinp.name), "%s", name);
  uinp.id.bustype = BUS_USB;
  uinp.id.vendor = virtualVendorId;
  uinp.id.product = virtualProductId;
  uinp.id.version = virtualVersionId;

  // Setup the uinput device
  // (see all in Linux's input-event-codes.h)
  ioctl(fd, UI_SET_EVBIT, EV_SYN);
  ioctl(fd, UI_SET_EVBIT, EV_KEY);
  ioctl(fd, UI_SET_EVBIT, EV_REL);

  // Set all relative event code bits on virtual device
  for (int i = 0; i < REL_CNT; ++i) {
    ioctl(fd, UI_SET_RELBIT, i);
  }

  // Thank's to Matthias Blümel / https://github.com/Blaimi
  // for the detailed investigation on the uinput issue on newer
  // Linux distributions.
  // See https://github.com/jahnf/Projecteur/issues/175#issuecomment-1432112896

  if (deviceType == Type::Mouse) {
    // Set key code bits for a virtual mouse
    for (int i = BTN_MISC; i < KEY_OK; ++i) {
      ioctl(fd, UI_SET_KEYBIT, i);
    }
  } else if (deviceType == Type::Keyboard) {
    // Set key code bits for a virtual keyboard
    for (int i = 1; i < BTN_MISC; ++i) {
      ioctl(fd, UI_SET_KEYBIT, i);
    }
    for (int i = KEY_OK; i < KEY_MACRO1; ++i) {
      ioctl(fd, UI_SET_KEYBIT, i);
    }
    // will set key bits from i = KEY_MACRO1 to i < KEY_CNT also work?
  }

  // Create input device into input sub-system
  const auto bytesWritten = write(fd, &uinp, sizeof(uinp));
  if ((bytesWritten != sizeof(uinp)) || (ioctl(fd, UI_DEV_CREATE)))
  {
    ::close(fd);
    logWarn(virtualdevice) << i18n("Unable to create Virtual (UINPUT) device.");
    return std::unique_ptr<VirtualDevice>();
  }

  // Log the device name
  char sysfs_device_name[16]{};
  ioctl(fd, UI_GET_SYSNAME(sizeof(sysfs_device_name)), sysfs_device_name);
  logInfo(virtualdevice) << i18n("Created uinput device: %1",
                                 QString("%1; /sys/devices/virtual/input/%2")
                                   .arg(name, sysfs_device_name));

  return std::make_shared<VirtualDevice>(Token{}, fd, name, sysfs_device_name);
}

// -------------------------------------------------------------------------------------------------
void VirtualDevice::emitEvents(const struct input_event input_events[], size_t num)
{
  if (!num) { return; }

  if (const ssize_t sz = sizeof(input_event) * num) {
    const auto bytesWritten = write(m_uinpFd, input_events, sz);
    if (bytesWritten != sz) {
      logError(virtualdevice) << i18n("Error while writing to virtual device.");
    }
  }
}

// -------------------------------------------------------------------------------------------------
void VirtualDevice::emitEvents(const std::vector<struct input_event>& events)
{
  emitEvents(events.data(), events.size());
}
