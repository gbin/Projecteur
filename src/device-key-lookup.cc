// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "device-key-lookup.h"

#include "enum-helper.h"

#include <linux/input.h>

#include <unordered_map>

namespace {
// -------------------------------------------------------------------------------------------------
inline uint32_t eHash(uint16_t type, uint16_t code)
{
  return  ( (static_cast<size_t>(type) << 16)
          | (static_cast<size_t>(code)) );
}

// -------------------------------------------------------------------------------------------------
inline uint32_t eHash(const DeviceInputEvent& die) {
  return eHash(die.type, die.code);
}

// -------------------------------------------------------------------------------------------------
uint32_t dHash(const DeviceId& dId)
{
  return (static_cast<size_t>(dId.vendorId) << 16) | dId.productId;
}

} // end anonymous namespace

namespace KeyName
{
// -------------------------------------------------------------------------------------------------
const QString& lookup(const DeviceId& dId, const DeviceInputEvent& die)
{
  using KeyNameMap = std::unordered_map<uint32_t, const QString>;

  static const KeyNameMap logitechSpotlightMapping = {
    { eHash(EV_KEY, BTN_LEFT), QObject::tr("Click") },
    { eHash(EV_KEY, KEY_RIGHT), QObject::tr("Next") },
    { eHash(EV_KEY, KEY_LEFT), QObject::tr("Back") },
    { eHash(EV_KEY, to_integral(SpecialKeys::Key::NextHold)),
       SpecialKeys::eventSequenceInfo(SpecialKeys::Key::NextHold).name },
    { eHash(EV_KEY, to_integral(SpecialKeys::Key::BackHold)),
       SpecialKeys::eventSequenceInfo(SpecialKeys::Key::BackHold).name },
  };

  static const KeyNameMap avattoH100Mapping = {
    { eHash(EV_KEY, BTN_LEFT), QObject::tr("Click") },
    { eHash(EV_KEY, KEY_PAGEDOWN), QObject::tr("Down") },
    { eHash(EV_KEY, KEY_PAGEUP), QObject::tr("Up") },
  };

  static const std::unordered_map<uint32_t, const KeyNameMap&> map =
  {
    {dHash({0x046d, 0xc53e}), logitechSpotlightMapping}, // Spotlight USB
    {dHash({0x046d, 0xb503}), logitechSpotlightMapping}, // Spotlight Bluetooth
    {dHash({0x046d, 0xb506}), logitechSpotlightMapping}, // Spotlight 2 Bluetooth
    {dHash({0x0c45, 0x8101}), avattoH100Mapping},        // Avatto H100, August WP200
  };

  // check for device id
  const auto dit = map.find(dHash(dId));
  if (dit != map.cend())
  {
    // check for key event sequence
    const auto& kesMap = dit->second;
    const auto kit = kesMap.find(eHash(die));
    if (kit != kesMap.cend()) {
      return kit->second;
    }
  }

  static const QString notFound;
  return notFound;
}
} // end namespace KeyName
