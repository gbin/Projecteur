// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md
#pragma once

#include <chrono>

#include <QObject>

class DeviceCommandHelper;
class QTimer;
class Settings;
class Spotlight;

class PresentationTimer : public QObject
{
  Q_OBJECT

public:
  enum class State { Idle, Running, Completed };
  Q_ENUM(State)

  PresentationTimer(Settings* settings, Spotlight* spotlight,
                    DeviceCommandHelper* deviceCommandHelper, QObject* parent = nullptr);

  State state() const { return m_state; }
  QString stateName() const;
  int durationSeconds() const { return m_durationSeconds; }
  int remainingSeconds() const { return m_remainingSeconds; }
  int hapticStrength() const { return m_hapticStrength; }

public slots:
  void start();
  void restart();
  void reset();
  void setDurationSeconds(int seconds);
  void setHapticStrength(int strength);

signals:
  void stateChanged(PresentationTimer::State state);
  void durationSecondsChanged(int seconds);
  void remainingSecondsChanged(int seconds);
  void hapticStrengthChanged(int strength);

private slots:
  void updateRemaining();

private:
  using Clock = std::chrono::steady_clock;

  void beginCountdown();
  void setState(State state);
  void setRemainingSeconds(int seconds);

  Settings* const m_settings;
  DeviceCommandHelper* const m_deviceCommandHelper;
  QTimer* const m_updateTimer;
  Clock::time_point m_deadline;
  State m_state = State::Idle;
  int m_durationSeconds = 15 * 60;
  int m_remainingSeconds = m_durationSeconds;
  int m_hapticStrength = 50;
};
