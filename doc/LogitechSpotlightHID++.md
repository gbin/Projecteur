# Logitech Spotlight HID++

## HID++ Basics

There are two major version of Logitech HID++ protocol. The Logitech spotlight
supports HID++ protocol version 2.0+. This document provide information about
HID++ version 2.0+ only. In the HID++ protocol two different types of messages
can be used for communication to the Logitech Spotlight device.
These two types of message are

  1. Short Message: 7 bytes long. Default message scheme for USB Dongle. The
     Spotlight device only supports short messages, when it is connected through
     the USB dongle.

     * First Byte: `0x10`

     * Second Byte: Device code for which the message is meant (in case it
       is sent from PC)/originated. `0xff` for USB dongle, `0x01` for
       Logitech Spotlight device.

     * Third Byte: Feature Index. Some of the featureIndex are `0x00` (for
       Root feature: used for querying device details),
       `0x80` (short set), `0x81` (short get).

     * Forth Byte: If third byte is not `0x80` or `0x81` then last 4 bits
       (`forth_byte & 0xf0`) are function code and first 4 bits
       (`forth_byte & 0x0f`) are software identification code.
       Software identification code is random value in range of 0 to 15
       (used to differentiate traffic for different softwares).

     * Fifth - Seventh Bytes: Parameters/data

  2. Long Message: 20 bytes long. Logitech Spotlight supports long messages in
     any connection mode (through USB dongle and Bluetooth). In long messages, the
     first byte is `0x11`, the next three bytes (second byte to forth byte) are the
     same as in short messages. However, in long messages, there are additional
     bytes (Fifth - Twentieth bytes) that can be used as parameters/data.

Please note that in device response the first four bytes will be the same as
in the request message, if no error is reported. However, in case of an error,
the third byte in the device response will be `0x8f` and first, second, forth
and fifth byte in the device response will be same as the first, second, third
and forth byte respectively as in the request message from the application.

If the Spotlight device is connected through Bluetooth then a short HID++
message meant for device should be transformed to a long HID++ message before
sending it to device. For changing a short message to a long message, the first
byte is replaced as `0x11` and the message is appended with trailing zero to
achieve the length of 20.

## HID++ Feature Code

HID++ feature codes (of type `uint16_t`; 2^16 possible feature codes) are
defined for a set of all the possible features supported by any Logitech HID++
device produced up until today. The feature code is part of the HID++ protocol
and does not vary for different devices. Some of the well known HID++2 feature
codes are:

| Feature Code Name          |  Byte Value  |
| -------------------------- | ------------:|
| `ROOT`                     | `0x0000`     |
| `FEATURE_SET`              | `0x0001`     |
| `FEATURE_INFO`             | `0x0002`     |
| `DEVICE_FW_VERSION`        | `0x0003`     |
| `DEVICE_UNIT_ID`           | `0x0004`     |
| `DEVICE_NAME`              | `0x0005`     |
| `DEVICE_GROUPS`            | `0x0006`     |
| `DEVICE_FRIENDLY_NAME`     | `0x0007`     |
| `KEEP_ALIVE`               | `0x0008`     |
| `RESET`                    | `0x0020`     |
| `CRYPTO_ID`                | `0x0021`     |
| `TARGET_SOFTWARE`          | `0x0030`     |
| `WIRELESS_SIGNAL_STRENGTH` | `0x0080`     |
| `DFUCONTROL_LEGACY`        | `0x00C0`     |
| `DFUCONTROL_UNSIGNED`      | `0x00C1`     |
| `DFUCONTROL_SIGNED`        | `0x00C2`     |
| `DFU`                      | `0x00D0`     |

A more extensive list of known feature codes are documented by the
[Solaar project](https://github.com/pwr-Solaar/Solaar/blob/master/docs/features.md).
Some of the feature codes relevant for the Logitech Spotlight are defined in
[hidpp.rs](../projecteur-core/src/hidpp.rs).

```c++
enum class FeatureCode : uint16_t {
  Root                 = 0x0000,
  FeatureSet           = 0x0001,
  FirmwareVersion      = 0x0003,
  DeviceName           = 0x0005,
  Reset                = 0x0020,
  DFUControlSigned     = 0x00c2,
  BatteryStatus        = 0x1000,
  UnifiedBattery       = 0x1004,
  Haptic               = 0x19b0,
  PresenterControl     = 0x1a00,
  Sensor3D             = 0x1a01,
  ReprogramControlsV4  = 0x1b04,
  WirelessDeviceStatus = 0x1db4,
  SwapCancelButton     = 0x2005,
  PointerSpeed         = 0x2205,
};
```

No single Logitech device supports all feature codes. Rather, a device supports
a limited range of features and corresponding feature codes. Inside the device,
the supported feature codes are mapped to an index (or Feature Index). This
mapping is called FeatureSet table. For any device, the Root Feature Code (`0x0000`)
has an index of `0x00`. Root Feature Index (`0x00`) is used for getting the
entire FeatureSet, and pinging the device.

For any device, the feature index corresponding to any feature code can be
obtained by using Root Feature Index (`0x00`) by sending the request message
`{0x10, 0x01, 0x00, 0x0d, Feature_Code(2 bytes), 0x00}` (here, the function
code is `0x00` and the software identification code is `0x0d` in forth byte).
If the return message is not an error message, the fifth byte in the response
is the Feature index and the 6th byte is the Feature type (See below).

The application can retrieve the entire FeatureSet table for the device with
following steps:

  1. Get the number of features supported by device:
     * Get the _Feature Index_ corresponding to the FeatureSet code (`0x0001`).
     * Get the number of features supported by sending the request message
       `{0x10, 0x01, (FeatureSet Index), 0x0d, 0x00, 0x00, 0x00}`
       (3rd byte is the Feature Index for FeatureSet Code; function code
        is `0x00` and software identification code is `0x0d` in forth byte).
       In the response, the 5th byte will be the number of features
       supported, except the root feature. As stated above, Root feature
       always has the Feature Index `0x00`. Hence, total number of features
       supported is one plus the count obtained in the response.

  2. Iterate over the Feature Indexes 1 to  the number of features supported and
     send the request (assuming Feature_Index for feature set is `0x01`; third byte)
     `{0x10, 0x01, 0x01, 0x1d, Feature_Index, 0x00, 0x00}` (function code is `0x10`
     and software identification code is `0x0d` in forth byte). The response will
     contain the HID++ Feature Code at byte 5 and 6 as `uint16_t` and the Feature
     Type at byte 7 for a valid Feature Index. In the Feature Type byte,
     if 7th bit is set this means _Software Hidden_, if bit 8 is set this means
     _Obsolete feature_. So, Software_Hidden = (`Feature_Type & (1<<6)`) and
     Obsolete_Feature = (`Feature_Type & (1<<7)`).
     Software Hidden or Obsolete features should not be handled by any application.
     In case the Feature Index is not valid (i.e.,feature index > number of
     feature supported) then `0x0200` will be in the response at byte 5 and 6.

The FeatureSet table for a device may change with a firmware update. The
application should cache FeatureSet table along with Firmware version and only
read FeatureSet table again if the firmware version has changed. This logic for
getting FeatureSet table from device is implemented in
the HID++ initialization in [hidpp.rs](../projecteur-core/src/hidpp.rs).

## Resetting Logitech Spotlight device

Depending on the connection mode (USB dongle or Bluetooth), the Logitech
Spotlight device can be reset with following HID++ message from the application:

  1. Reset the USB dongle by sending following commands in sequence

     ```json
     {0x10, 0xff, 0x81, 0x00, 0x00, 0x00, 0x00} // get wireless notification and software connection status
     {0x10, 0xff, 0x80, 0x00, 0x00, 0x01, 0x00} // set sofware bit to false
     {0x10, 0xff, 0x80, 0x02, 0x02, 0x00, 0x00} // initialize the USB dongle
     {0x10, 0xff, 0x80, 0x00, 0x00, 0x09, 0x00} // set sofware bit to true
     ```

  2. Load the FeatureSet table for the device (from pre-existing cache or from
     the device if firmware version has changed by calling `initFromDevice`
     handling in [hidpp.rs](../projecteur-core/src/hidpp.rs)).

  3. Reset the Spotlight device with the Feature index for Reset Feature Code
     from the FeatureSet table. If the Feature Index for Reset Feature Code is
     `0x05`, then HID++ request message for resetting will be
     `{0x10, 0x01, 0x05, 0x1d, 0x00, 0x00, 0x00}`.

In addition to these steps, the Projecteur also pings the device by sending
`{0x10, 0x01, 0x00, 0x1d, 0x00, 0x00, 0x5d}` (function code `0x10` and software
identification code `0x0d` in forth byte; the last byte is a random value that
will returned back on 7th byte in the response message). The response to this
ping contains the HID++ version (`fifth_byte + sixth_byte/10.0`) supported by
the device.

Further, Projecteur configures the Logitech device to send `Next Hold` and
`Back Hold` events and resets the pointer speed to a default value with
following HID++ commands:

```json
// enable next button hold (0x07 - Feature Index for ReprogramControlsV4 Feature Code, 0xda - next button, 0x33 - hold event)
{0x11, 0x01, 0x07, 0x3d, 0x00, 0xda, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
// back button hold (0x07 - Feature Index for ReprogramControlsV4 Feature Code, 0xdc - back button, 0x33 - hold event)
{0x11, 0x01, 0x07, 0x3d, 0x00, 0xdc, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
// Reset pointer Speed (0x0a - Feature Index for Reset Feature Code, 0x14 - 5th level pointer speeed (can be between 0x10-0x19))
{0x10, 0x01, 0x0a, 0x1d, 0x14, 0x00, 0x00}
```

These initialization steps are implemented in `initReceiver` and `initPresenter`
the device communication code in [hidpp.rs](../projecteur-core/src/hidpp.rs).
After reprogramming the Next and Back buttons, the spotlight device will send
mouse movement data when either of these button are long-pressed and device is
moved. The processing of these events are discussed in the
[following section](#response-to-next-hold-and-back-hold-keys).

For completeness, it should be noted that the official Logitech Spotlight
software reprogram the click and double click events too by following HID++
commands:

```json
// Send click event as HID++ message (0x07 - Feature Index for ReprogramControlsV4 Feature Code, 0xd8-click button, 0x33- hold event)
{0x11, 0x01, 0x07, 0x3d, 0x00, 0xd8, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
// Send double click as HID++ message (0x07 - Feature Index for ReprogramControlsV4 Feature Code, 0xdf - double click)
{0x11, 0x01, 0x07, 0x3d, 0x00, 0xdf, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
```

Projecteur does not send these packages. Instead, it grab the events from mouse
event device associated with Spotlight. The approach taken by Projecteur is
advantageous as it help in implementing Input Mapping feature that official
Logitech Software lacks. However, this approach also makes porting Projecteur
to different platforms more difficult.

## Important HID++ commands for Spotlight device

### Wireless Notification on Activation/Deactivation of Spotlight Device

The Spotlight device sends a wireless notification if it gets activated.
Wireless notification will be short HID++ message if the Spotlight device is
connected through USB. Otherwise, it will be a long HID++ message.

For short HID++ wireless notifications, the third byte will be `0x41`. In this
message, the 6th bit in 5th byte shows the activation status of spotlight
device. If the 6th bit is 0 then device just got active, otherwise the device
just got deactivated.

A long HID++ wireless notification is only received for device activation.
In this message, third byte will be the Feature Index for the Wireless
Notification Feature Code (`0x1db4`).

### Vibration support

The spotlight device can vibrate if the HID++ message
`{0x10, 0x01, (Feature Index for Presenter Control Feature Code), 0x1d, length, 0xe8, intensity}`
is sent to it. In the message, length can range between `0x00` to `0x0a`.

The Logitech Spotlight 2 instead exposes the Haptic Feature Code (`0x19b0`).
Projecteur first sets its global haptic level with function `0x02` and payload
`{enabled, level}`, where level is a percentage. It then plays the built-in
Completed waveform (`0x07`) with function `0x04`. Unlike the original
Presenter Control command, this feature does not accept a vibration length.

### Battery Status

Battery status can be requested by sending request command
`{0x10, 0x01, 0x06, 0x0d, 0x00, 0x00, 0x00}` (assuming the Feature Index for
Battery Status Feature Code (`0x1000`) is `0x06`; function code is `0x00` and
software identification code is `0x0d`). In the response, the fifth byte shows
current battery level in percent, sixth byte shows the next reported battery
level in percent (device do not report continuous battery level) and the seventh
byte shows the state of battery with following possible values.

```cpp
enum class BatteryStatus : uint8_t {Discharging    = 0x00,
                                    Charging       = 0x01,
                                    AlmostFull     = 0x02,
                                    Full           = 0x03,
                                    SlowCharging   = 0x04,
                                    InvalidBattery = 0x05,
                                    ThermalError   = 0x06,
                                    ChargingError  = 0x07
                                   };
```

Newer devices such as the Logitech Spotlight 2 use the Unified Battery Feature
Code (`0x1004`). Its `getStatus` request uses function code `0x01`; the response
contains the current discharge percentage in the fifth byte, an approximate
battery level in the sixth byte, and the same battery status value in the
seventh byte.

## Processing of device response

All of the HID++ commands listed above result in response messages from the
Spotlight device. For most messages, these responses from device are just the
acknowledgements of the HID++ commands sent by the application. However, some
responses from the Spotlight device contain useful information. These responses
are processed in the  `onHidppDataAvailable` method in the `SubHidppConnection`
implementation in [hidpp.rs](../projecteur-core/src/hidpp.rs). Description of HID++ messages
from device to reprogrammed keys (`Next Hold` and `Back Hold`) are provided in
following sub-section:

### Response to `Next Hold` and `Back Hold` keys

The first HID++ message sent by Spotlight device at the start and end of any
hold event is `{0x11, 0x01, 0x07, 0x00, (button_code), ...followed by zeroes ....}`
(assuming `0x07` is the Feature Index for ReprogramControlsV4 Feature Code (`0x1b04`)).
When the hold event starts, button_code will be `0xda` for start of `Next Hold`
event and `0xdc` for start of `Back Hold` event. When the button is released
the HID++ message received have `0x00` as button_code for both cases.

During the Hold event, the Spotlight device sends the relative mouse movement
data as HID++ messages. These messages are of form
`{0x11, 0x01, 0x07, 0x10, (mouse data in 4 bytes), ...followed by zeroes ....}`
(assuming `0x07` is the Feature Index for ReprogramControlsV4 Feature Code (`0x1b04`)).
In the four bytes (for mouse data), the second and last bytes are relative `x`
and `y` values. These relative `x` and `y` values are used for Scrolling and
Volume Control Actions in Projecteur.

The relevant functions for processing `Next Hold` and `Back Hold` are provided
in `registerForNotifications` method in the `Spotlight` class
([hid_report.rs](../projecteur-core/src/hid_report.rs)).

## Further information

For more information about HID++ protocol in general please check
[logitech-hidpp module](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-logitech-hidpp.c)
code in linux kernel source. Documentation from
[Solaar project](https://github.com/pwr-Solaar/Solaar/blob/master/docs/)
might be helpful too.
