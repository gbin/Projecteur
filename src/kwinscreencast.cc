// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "kwinscreencast.h"

#include "projecteur_desktop_debug.h"

#include <QScreen>

KWinScreencastStream::KWinScreencastStream(
  struct ::zkde_screencast_stream_unstable_v1* stream, QObject* parent)
  : QObject(parent)
  , QtWayland::zkde_screencast_stream_unstable_v1(stream)
{
}

KWinScreencastStream::~KWinScreencastStream()
{
  if (isInitialized()) {
    close();
  }
}

void KWinScreencastStream::zkde_screencast_stream_unstable_v1_closed()
{
  if (isInitialized()) {
    close();
  }
  emit closed();
}

void KWinScreencastStream::zkde_screencast_stream_unstable_v1_created(uint32_t node)
{
  if (m_nodeId == node) {
    return;
  }
  m_nodeId = node;
  emit nodeIdChanged();
}

void KWinScreencastStream::zkde_screencast_stream_unstable_v1_failed(
  const QString& error)
{
  m_error = error;
  emit errorChanged();
  qCWarning(PROJECTEUR_DESKTOP_LOG).noquote()
    << QStringLiteral("KWin screencast failed: %1").arg(error);
}

void KWinScreencastStream::zkde_screencast_stream_unstable_v1_serial(
  uint32_t objectSerialHi, uint32_t objectSerialLow)
{
  const quint64 serial = (quint64(objectSerialHi) << 32) | objectSerialLow;
  if (m_objectSerial == serial) {
    return;
  }
  m_objectSerial = serial;
  emit objectSerialChanged();
}

KWinScreencast::KWinScreencast(QObject* parent)
  : QWaylandClientExtensionTemplate(6)
{
  setParent(parent);
}

KWinScreencast::~KWinScreencast()
{
  if (isInitialized()) {
    destroy();
  }
}

KWinScreencastStream* KWinScreencast::streamScreen(QScreen* screen, QObject* parent)
{
  if (!isActive() || !screen) {
    return nullptr;
  }

  const QRect geometry = screen->geometry();
  auto* const stream = QtWayland::zkde_screencast_unstable_v1::stream_region(
    geometry.x(), geometry.y(), geometry.width(), geometry.height(),
    0, pointer_hidden);
  if (!stream) {
    return nullptr;
  }
  return new KWinScreencastStream(stream, parent);
}
