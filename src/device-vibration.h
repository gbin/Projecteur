// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md
#pragma once

#include <QPointer>
#include <QWidget>
#include <memory>

class QSpinBox;
class QLabel;
class SubDeviceConnection;
class SubHidppConnection;

// -------------------------------------------------------------------------------------------------
class TimerWidget : public QWidget
{
  Q_OBJECT

public:
  TimerWidget(QWidget* parent);
  ~TimerWidget() override;

  bool timerEnabled() const;
  void setTimerEnabled(bool enabled);

  void start();
  void stop();
  bool timerRunning() const;
  void setValueSeconds(int seconds);
  void setValueMinutes(int minutes);
  int valueSeconds() const;

signals:
  void timeout();
  void valueSecondsChanged(int);
  void enabledChanged(bool);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

// -------------------------------------------------------------------------------------------------
class MultiTimerWidget : public QWidget
{
  Q_OBJECT

public:
  explicit MultiTimerWidget(QWidget* parent = nullptr);
  virtual ~MultiTimerWidget() override;

  /// Returns the number of timers
  static int timerCount();

  void setTimerEnabled(uint32_t timerId, bool enabled);
  bool timerEnabled(uint32_t timerId) const;

  void startTimer(uint32_t timerId);
  void stopTimer(uint32_t timerId);
  void stopAllTimers();
  bool timerRunning(uint32_t timerId) const;

  void setTimerValue(uint32_t timerId, int seconds);
  int timerValue(uint32_t timerId) const;

signals:
  /// Emitted when a timer times out.
  void timeout(uint32_t timerId);
  void timerEnabledChanged(uint32_t timerId, bool enabled);
  void timerValueChanged(uint32_t timerId, int seconds);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

// -------------------------------------------------------------------------------------------------
class VibrationSettingsWidget : public QWidget
{
  Q_OBJECT

public:
  explicit VibrationSettingsWidget(QWidget* parent = nullptr);

  uint8_t length() const;
  void setLength(uint8_t len);

  uint8_t intensity() const;
  void setIntensity(uint8_t intensity);

  void setSubDeviceConnection(SubDeviceConnection* sdc);
  void sendVibrateCommand();

signals:
  void intensityChanged(uint8_t intensity);
  void lengthChanged(uint8_t length);

private:
  QPointer<SubHidppConnection> m_subDeviceConnection;
  QLabel* m_lengthLabel = nullptr;
  QSpinBox* m_sbLength = nullptr;
  QSpinBox* m_sbIntensity = nullptr;
};
