// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "presentationtimer.h"

#include "device-command-helper.h"
#include "settings.h"
#include "spotlight.h"

#include <algorithm>
#include <cmath>

#include <QTimer>

namespace {
constexpr int MinimumDurationSeconds = 60;
constexpr int MaximumDurationSeconds = 180 * 60;
constexpr int TimerUpdateIntervalMs = 250;
}

PresentationTimer::PresentationTimer(Settings* settings, Spotlight* spotlight,
                                     DeviceCommandHelper* deviceCommandHelper, QObject* parent)
  : QObject(parent)
  , m_settings(settings)
  , m_spotlight(spotlight)
  , m_deviceCommandHelper(deviceCommandHelper)
  , m_updateTimer(new QTimer(this))
  , m_enabled(settings->presentationTimerEnabled())
  , m_durationSeconds(std::clamp(settings->presentationTimerDurationSeconds(),
                                 MinimumDurationSeconds, MaximumDurationSeconds))
  , m_remainingSeconds(m_durationSeconds)
{
  m_updateTimer->setTimerType(Qt::PreciseTimer);
  m_updateTimer->setInterval(TimerUpdateIntervalMs);
  connect(m_updateTimer, &QTimer::timeout, this, &PresentationTimer::updateRemaining);
  connect(spotlight, &Spotlight::slideNavigationPressed, this, &PresentationTimer::start);
}

QString PresentationTimer::stateName() const
{
  switch (m_state) {
    case State::Idle: return QStringLiteral("idle");
    case State::Running: return QStringLiteral("running");
    case State::Completed: return QStringLiteral("completed");
  }
  return QStringLiteral("idle");
}

void PresentationTimer::setEnabled(bool enabled)
{
  if (m_enabled == enabled) { return; }
  m_enabled = enabled;
  m_settings->setPresentationTimerEnabled(enabled);
  emit enabledChanged(enabled);
  if (!enabled) { reset(); }
}

void PresentationTimer::start()
{
  if (!m_enabled) { return; }
  if (m_state == State::Idle) { beginCountdown(); }
}

void PresentationTimer::restart()
{
  if (!m_enabled) { return; }
  beginCountdown();
}

void PresentationTimer::reset()
{
  m_updateTimer->stop();
  setRemainingSeconds(m_durationSeconds);
  setState(State::Idle);
}

void PresentationTimer::setDurationSeconds(int seconds)
{
  const int duration = std::clamp(seconds, MinimumDurationSeconds, MaximumDurationSeconds);
  if (m_durationSeconds == duration) { return; }

  m_durationSeconds = duration;
  m_settings->setPresentationTimerDurationSeconds(duration);
  emit durationSecondsChanged(duration);
  if (m_state == State::Idle) { setRemainingSeconds(duration); }
}

void PresentationTimer::updateRemaining()
{
  if (m_state != State::Running) { return; }

  const auto remainingMs =
    std::chrono::duration_cast<std::chrono::milliseconds>(m_deadline - Clock::now()).count();
  if (remainingMs > 0) {
    setRemainingSeconds(static_cast<int>((remainingMs + 999) / 1000));
    return;
  }

  m_updateTimer->stop();
  setRemainingSeconds(0);
  setState(State::Completed);

  if (m_deviceCommandHelper && m_spotlight)
  {
    for (const auto& device : m_spotlight->connectedDevices())
    {
      const int strength = std::clamp(
        m_settings->devicePresentationTimerHapticStrength(device.id), 0, 100);
      if (strength == 0) { continue; }

      const auto intensity = static_cast<uint8_t>(
        std::lround(static_cast<double>(strength) * 255.0 / 100.0));
      m_deviceCommandHelper->sendVibrateCommand(device.id, intensity, 0);
    }
  }
}

void PresentationTimer::beginCountdown()
{
  m_deadline = Clock::now() + std::chrono::seconds(m_durationSeconds);
  setRemainingSeconds(m_durationSeconds);
  setState(State::Running);
  m_updateTimer->start();
}

void PresentationTimer::setState(State state)
{
  if (m_state == state) { return; }
  m_state = state;
  emit stateChanged(state);
}

void PresentationTimer::setRemainingSeconds(int seconds)
{
  if (m_remainingSeconds == seconds) { return; }
  m_remainingSeconds = seconds;
  emit remainingSecondsChanged(seconds);
}
