// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md
#pragma once

#include <QObject>
#include <QPixmap>

class KWinScreencast;
class QScreen;

class LinuxDesktop : public QObject
{
  Q_OBJECT

public:
  enum class Type : uint8_t { KDE, Other };

  explicit LinuxDesktop(QObject* parent = nullptr);
  ~LinuxDesktop() override;

  bool isWayland() const { return m_wayland; };
  Type type() const { return m_type; };

  QPixmap grabScreen(QScreen* screen) const;
  QObject* streamScreen(QScreen* screen, QObject* parent = nullptr);
  void setShakeCursorEffectSuppressed(bool suppressed);

private:
  bool m_wayland = false;
  Type m_type = Type::Other;
  bool m_shakeCursorEffectSuppressed = false;
  KWinScreencast* m_screencast = nullptr;
};
