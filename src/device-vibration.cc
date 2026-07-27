// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "device-vibration.h"

#include "device-hidpp.h"
#include "hidpp.h"
#include "iconwidgets.h"
#include "logging.h"

#include <QCheckBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSocketNotifier>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <array>
#include <chrono>

DECLARE_LOGGING_CATEGORY(hid)

// -------------------------------------------------------------------------------------------------
namespace {
  constexpr uint32_t numTimers = 3;
} // end anonymous namespace

// -------------------------------------------------------------------------------------------------
struct TimerWidget::Impl
{
  // -----------------------------------------------------------------------------------------------
  explicit Impl(TimerWidget* parent)
    : stack(new QStackedWidget(parent))
    , editor(new QWidget(parent))
    , overlay(new QWidget(parent))
    , checkbox(new QCheckBox(parent))
    , sbHours(new QSpinBox(parent))
    , sbMinutes(new QSpinBox(parent))
    , sbSeconds(new QSpinBox(parent))
    , btnStartStop(new IconButton(Font::Icon::media_control_48, parent))
    , timer(new QTimer(parent))
    , countdownTimer(new QTimer(parent))
    , overlayLabel(new QLabel(parent))
  {
    const auto layout = new QHBoxLayout(parent);
    layout->addWidget(checkbox);
    layout->addWidget(stack);
    layout->setContentsMargins(0, 0, 0, 0);

    stack->addWidget(editor);
    stack->addWidget(overlay);
    const auto editLayout = new QHBoxLayout(editor);
    const auto m = editLayout->contentsMargins();
    editLayout->setContentsMargins(m.left(), 0, m.right(), 0);
    editLayout->addWidget(sbHours);
    editLayout->addWidget(new QLabel(TimerWidget::tr("h"), editor));
    editLayout->addWidget(sbMinutes);
    editLayout->addWidget(new QLabel(TimerWidget::tr("m"), editor));
    editLayout->addWidget(sbSeconds);
    editLayout->addWidget(new QLabel(TimerWidget::tr("s"), editor));
    editLayout->addStretch(1);

    constexpr auto day = std::chrono::hours(24);
    constexpr auto hoursMax = (day - std::chrono::hours(1)).count();
    constexpr auto minutesMax = std::chrono::minutes(60).count() - 1;
    constexpr auto secondsMax = std::chrono::seconds(60).count() - 1;

    sbHours->setRange(0, hoursMax);
    sbMinutes->setRange(0, minutesMax);
    sbSeconds->setRange(0, secondsMax);

    layout->addWidget(btnStartStop);
    btnStartStop->setCheckable(true);
    QObject::connect(btnStartStop, &IconButton::toggled, parent, [this](bool checked) {
      stack->setCurrentWidget(checked ? overlay : editor);
      btnStartStop->setText(checked
                              ? QChar(static_cast<char16_t>(Font::Icon::media_control_50))
                              : QChar(static_cast<char16_t>(Font::Icon::media_control_48)));
      if (checked) {
        secondsLeft = valueSeconds();
        updateOverlayLabel(secondsLeft);
        countdownTimer->start();
        timer->start();
      } else {
        timer->stop();
        countdownTimer->stop();
      }
    });

    const auto overlayLayout = new QHBoxLayout(overlay);
    overlayLayout->addWidget(overlayLabel);
    overlayLayout->setContentsMargins(m.left(), 0, m.right(), 0);
    overlayLabel->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    editor->setEnabled(checkbox->isChecked());
    btnStartStop->setEnabled(checkbox->isChecked());
    QObject::connect(checkbox, &QCheckBox::toggled, parent, [this, parent](bool checked) {
      editor->setEnabled(checked);
      if (!checked) { btnStartStop->setChecked(false); }
      btnStartStop->setEnabled(checked);
      emit parent->enabledChanged(checked);
    });

    QObject::connect(timer, &QTimer::timeout, parent, [this](){ btnStartStop->setChecked(false); });
    QObject::connect(sbHours, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), parent,
    [this, parent]() {
      updateTimerInterval();
      emit parent->valueSecondsChanged(valueSeconds());
    });
    QObject::connect(sbMinutes, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), parent,
    [this, parent]() {
      updateTimerInterval();
      emit parent->valueSecondsChanged(valueSeconds());
    });
    QObject::connect(sbSeconds, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), parent,
    [this, parent]() {
     updateTimerInterval();
     emit parent->valueSecondsChanged(valueSeconds());
    });

    timer->setSingleShot(true);
    countdownTimer->setInterval(1000);

    QObject::connect(countdownTimer, &QTimer::timeout, parent, [this](){
      updateOverlayLabel(--secondsLeft);
    });
  }

  int valueSeconds() const {
    return sbSeconds->value() + sbMinutes->value() * 60 + sbHours->value() * 60 * 60;
  }

  // -----------------------------------------------------------------------------------------------
  void updateTimerInterval() {
    timer->setInterval(valueSeconds() * 1000);
  }

  // -----------------------------------------------------------------------------------------------
  void updateOverlayLabel(int remainingSeconds)
  {
    const std::chrono::seconds remainingTime(remainingSeconds);
    const auto hours = std::chrono::duration_cast<std::chrono::hours>(remainingTime);
    const auto mins = std::chrono::duration_cast<std::chrono::minutes>(remainingTime-hours);
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(remainingTime-hours-mins);

    overlayLabel->setText(QString("%1:%2:%3")
                          .arg(hours.count(), 2, 10, QChar('0'))
                          .arg(mins.count(), 2, 10, QChar('0'))
                          .arg(secs.count(), 2, 10, QChar('0')));
  }

  // -----------------------------------------------------------------------------------------------
  QStackedWidget* stack = nullptr;
  QWidget* editor = nullptr;
  QWidget* overlay = nullptr;
  QCheckBox* checkbox = nullptr;
  QSpinBox* sbHours = nullptr;
  QSpinBox* sbMinutes = nullptr;
  QSpinBox* sbSeconds = nullptr;
  IconButton* btnStartStop = nullptr;
  QTimer* timer = nullptr;
  QTimer* countdownTimer = nullptr;
  QLabel* overlayLabel = nullptr;
  int secondsLeft = 0;
};

// -------------------------------------------------------------------------------------------------
TimerWidget::TimerWidget(QWidget* parent)
  : QWidget(parent)
  , m_impl(new Impl(this))
{
  connect(m_impl->timer, &QTimer::timeout, this, &TimerWidget::timeout);
}

// -------------------------------------------------------------------------------------------------
TimerWidget::~TimerWidget() = default;

// -------------------------------------------------------------------------------------------------
bool TimerWidget::timerEnabled() const {
  return m_impl->checkbox->isChecked();
}

// -------------------------------------------------------------------------------------------------
void TimerWidget::setTimerEnabled(bool enabled) {
  m_impl->checkbox->setChecked(enabled);
}

// -------------------------------------------------------------------------------------------------
bool TimerWidget::timerRunning() const {
  return m_impl->timer->isActive();
}

// -------------------------------------------------------------------------------------------------
void TimerWidget::start() {
  if (timerEnabled()) {
    m_impl->btnStartStop->setChecked(true);
  }
}

// -------------------------------------------------------------------------------------------------
void TimerWidget::stop() {
  m_impl->btnStartStop->setChecked(false);
}

// -------------------------------------------------------------------------------------------------
void TimerWidget::setValueSeconds(int seconds)
{
  const std::chrono::seconds totalSecs(seconds);
  const auto hours = std::chrono::duration_cast<std::chrono::hours>(totalSecs);
  const auto mins = std::chrono::duration_cast<std::chrono::minutes>(totalSecs-hours);
  const auto secs = std::chrono::duration_cast<std::chrono::seconds>(totalSecs-hours-mins);
  m_impl->sbHours->setValue( static_cast<int>(hours.count()) );
  m_impl->sbMinutes->setValue( static_cast<int>(mins.count()) );
  m_impl->sbSeconds->setValue( static_cast<int>(secs.count()) );
}

// -------------------------------------------------------------------------------------------------
void TimerWidget::setValueMinutes(int minutes) {
  setValueSeconds(minutes * 60);
}

// -------------------------------------------------------------------------------------------------
int TimerWidget::valueSeconds() const {
  return m_impl->valueSeconds();
}

// -------------------------------------------------------------------------------------------------
struct MultiTimerWidget::Impl
{
  explicit Impl(QWidget* parent)
  {
    for (size_t i = 0; i < numTimers; ++i) {
      timers.at(i) = new TimerWidget(parent);
    }
  }

  std::array<TimerWidget*, numTimers> timers = {};
};

// -------------------------------------------------------------------------------------------------
MultiTimerWidget::MultiTimerWidget(QWidget* parent)
  : QWidget(parent)
  , m_impl(new Impl(this))
{
  constexpr int defaultTimeoutIncrMin = 15;

  const auto layout = new QHBoxLayout(this);
  const auto iconLabel = new IconLabel(Font::time_19, this);
  layout->addWidget(iconLabel);
  layout->setAlignment(iconLabel, Qt::AlignTop);

  const auto groupBox = new QGroupBox(tr("Timers"), this);
  groupBox->setSizePolicy(groupBox->sizePolicy().horizontalPolicy(),
                          QSizePolicy::Maximum);
  layout->addWidget(groupBox);
  layout->setAlignment(groupBox, Qt::AlignTop);
  const auto timerLayout = new QVBoxLayout(groupBox);

  for (uint32_t i = 0; i < numTimers; ++i)
  {
    timerLayout->addWidget(m_impl->timers.at(i));
    const auto timerDefaultValueMinutes = defaultTimeoutIncrMin + i * defaultTimeoutIncrMin;

    m_impl->timers.at(i)->setValueMinutes(static_cast<int>(timerDefaultValueMinutes));

    connect(m_impl->timers.at(i), &TimerWidget::valueSecondsChanged, this, [this, i](int secs) {
      emit timerValueChanged(i, secs);
    });

    connect(m_impl->timers.at(i), &TimerWidget::enabledChanged, this, [this, i](bool enabled) {
      emit timerEnabledChanged(i, enabled);
    });

    connect(m_impl->timers.at(i), &TimerWidget::timeout, this, [this, i](){
      emit timeout(i);
    });
  }

  layout->setStretch(1, 1);
}

// -------------------------------------------------------------------------------------------------
MultiTimerWidget::~MultiTimerWidget() = default;

// -------------------------------------------------------------------------------------------------
int MultiTimerWidget::timerCount() {
  return numTimers;
}

// -------------------------------------------------------------------------------------------------
void MultiTimerWidget::setTimerEnabled(uint32_t timerId, bool enabled)
{
  if (timerId >= numTimers) { return; }
  m_impl->timers.at(timerId)->setTimerEnabled(enabled);
}

// -------------------------------------------------------------------------------------------------
bool MultiTimerWidget::timerEnabled(uint32_t timerId) const
{
  if (timerId >= numTimers) { return false; }
  return m_impl->timers.at(timerId)->timerEnabled();
}

// -------------------------------------------------------------------------------------------------
void MultiTimerWidget::startTimer(uint32_t timerId)
{
  if (timerId >= numTimers) { return; }
  m_impl->timers.at(timerId)->start();
}

// -------------------------------------------------------------------------------------------------
void MultiTimerWidget::stopTimer(uint32_t timerId)
{
  if (timerId >= numTimers) { return; }
  m_impl->timers.at(timerId)->stop();
}

// -------------------------------------------------------------------------------------------------
void MultiTimerWidget::stopAllTimers()
{
  for (size_t i = 0; i < numTimers; ++i) {
    m_impl->timers.at(i)->stop();
  }
}

// -------------------------------------------------------------------------------------------------
bool MultiTimerWidget::timerRunning(uint32_t timerId) const
{
  if (timerId >= numTimers) { return false; }
  return m_impl->timers.at(timerId)->timerRunning();
}

// -------------------------------------------------------------------------------------------------
void MultiTimerWidget::setTimerValue(uint32_t timerId, int seconds)
{
  if (timerId >= numTimers) { return; }
  m_impl->timers.at(timerId)->setValueSeconds(seconds);
}

// -------------------------------------------------------------------------------------------------
int MultiTimerWidget::timerValue(uint32_t timerId) const
{
  if (timerId >= numTimers) { return -1; }
  return m_impl->timers.at(timerId)->valueSeconds();
}

// -------------------------------------------------------------------------------------------------
VibrationSettingsWidget::VibrationSettingsWidget(QWidget* parent)
  : QWidget(parent)
  , m_sbLength(new QSpinBox(this))
  , m_sbIntensity(new QSpinBox(this))
{
  constexpr int vibrationIntensityMin = 25;
  constexpr int vibrationIntensityMax = 255;

  m_sbLength->setRange(0, 10);
  m_sbIntensity->setRange(vibrationIntensityMin, vibrationIntensityMax);

  const auto layout = new QHBoxLayout(this);
  const auto iconLabel = new IconLabel(Font::control_panel_9, this);
  layout->addWidget(iconLabel);
  layout->setAlignment(iconLabel, Qt::AlignTop);

  const auto groupBox = new QGroupBox(tr("Vibration Settings"), this);
  groupBox->setSizePolicy(groupBox->sizePolicy().horizontalPolicy(),
                          QSizePolicy::Maximum);
  layout->addWidget(groupBox);
  layout->setAlignment(groupBox, Qt::AlignTop);

  const auto grid = new QGridLayout(groupBox);
  grid->addWidget(new QLabel(tr("Length"), this), 0, 0);
  grid->addWidget(new QLabel(tr("Intensity"), this), 1, 0);
  grid->addWidget(m_sbLength, 0, 1);
  grid->addWidget(m_sbIntensity, 1, 1);
  grid->setColumnStretch(0, 1);
  grid->setColumnStretch(1, 2);

  const auto testBtn = new QPushButton(tr("Test"), this);
  grid->addWidget(testBtn, 2, 0, 1, 2);

  m_sbLength->setValue(0x00);
  m_sbIntensity->setValue(0x80);

  connect(m_sbLength, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this,
  [this](int value){
    emit lengthChanged(value);
  });

  connect(m_sbIntensity, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this,
  [this](int value){
    emit intensityChanged(value);
  });

  connect(testBtn, &QPushButton::clicked, this, &VibrationSettingsWidget::sendVibrateCommand);

  layout->setStretch(1, 1);
}

// -------------------------------------------------------------------------------------------------
uint8_t VibrationSettingsWidget::length() const {
  return m_sbLength->value();
}

// -------------------------------------------------------------------------------------------------
uint8_t VibrationSettingsWidget::intensity() const {
  return m_sbIntensity->value();
}

// -------------------------------------------------------------------------------------------------
void VibrationSettingsWidget::setLength(uint8_t len)
{
  if (m_sbLength->value() == len) { return; }
  m_sbLength->setValue(len);
}

// -------------------------------------------------------------------------------------------------
void VibrationSettingsWidget::setIntensity(uint8_t intensity)
{
  if (m_sbIntensity->value() == intensity) { return; }
  m_sbIntensity->setValue(intensity);
}

// -------------------------------------------------------------------------------------------------
void VibrationSettingsWidget::setSubDeviceConnection(SubDeviceConnection *sdc)
{
  m_subDeviceConnection = qobject_cast<SubHidppConnection*>(sdc);
}

// -------------------------------------------------------------------------------------------------
void VibrationSettingsWidget::sendVibrateCommand()
{
  if (!m_subDeviceConnection) { return; }
  if (!m_subDeviceConnection->isConnected()) { return; }
  if (!m_subDeviceConnection->hasFlags(DeviceFlag::Vibrate)) { return; }

  const uint8_t vlen = m_sbLength->value();
  const uint8_t vint = m_sbIntensity->value();
  m_subDeviceConnection->sendVibrateCommand(vint, vlen,
  [](HidppConnectionInterface::MsgResult result, HIDPP::Message&& msg) {
    logDebug(hid) << tr("Vibrate command returned: %1 (%2)")
                     .arg(toString(result)).arg(msg.hex());
  });
}
