// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md
#pragma once

#include <QObject>
#include <QPointer>

class Spotlight;
struct DeviceId;

/// Class that offers easy access to device commands with a given Spotlight
/// instance.
class DeviceCommandHelper : public QObject
{
  Q_OBJECT

public:
  explicit DeviceCommandHelper(QObject* parent, Spotlight* spotlight);
  virtual ~DeviceCommandHelper();

  bool sendVibrateCommand(uint8_t intensity, uint8_t length);
  bool sendVibrateCommand(const DeviceId& deviceId, uint8_t intensity, uint8_t length);

private:
  QPointer<Spotlight> m_spotlight;
};
