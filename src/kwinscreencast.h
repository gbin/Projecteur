// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md
#pragma once

#include "qwayland-zkde-screencast-unstable-v1.h"

#include <QWaylandClientExtensionTemplate>

class QScreen;

class KWinScreencastStream final
  : public QObject
  , public QtWayland::zkde_screencast_stream_unstable_v1
{
  Q_OBJECT
  Q_PROPERTY(quint64 objectSerial READ objectSerial NOTIFY objectSerialChanged)
  Q_PROPERTY(uint nodeId READ nodeId NOTIFY nodeIdChanged)
  Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
  explicit KWinScreencastStream(
    struct ::zkde_screencast_stream_unstable_v1* stream,
    QObject* parent = nullptr);
  ~KWinScreencastStream() override;

  quint64 objectSerial() const { return m_objectSerial; }
  uint nodeId() const { return m_nodeId; }
  QString error() const { return m_error; }

signals:
  void objectSerialChanged();
  void nodeIdChanged();
  void errorChanged();
  void closed();

private:
  void zkde_screencast_stream_unstable_v1_closed() override;
  void zkde_screencast_stream_unstable_v1_created(uint32_t node) override;
  void zkde_screencast_stream_unstable_v1_failed(const QString& error) override;
  void zkde_screencast_stream_unstable_v1_serial(
    uint32_t objectSerialHi, uint32_t objectSerialLow) override;

  quint64 m_objectSerial = 0;
  uint m_nodeId = 0;
  QString m_error;
};

class KWinScreencast final
  : public QWaylandClientExtensionTemplate<KWinScreencast>
  , public QtWayland::zkde_screencast_unstable_v1
{
  Q_OBJECT

public:
  explicit KWinScreencast(QObject* parent = nullptr);
  ~KWinScreencast() override;

  KWinScreencastStream* streamScreen(QScreen* screen, QObject* parent = nullptr);
};
