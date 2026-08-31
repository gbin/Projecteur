// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md
#pragma once

#include <memory>
#include <vector>

#include <QDataStream>
#include <QKeySequence>
#include <QObject>

class VirtualDevice;
class QTimer;

// -------------------------------------------------------------------------------------------------
/// This is basically the input_event struct from linux/input.h without the time member
struct DeviceInputEvent
{
  DeviceInputEvent() = default;
  DeviceInputEvent(uint16_t type, uint16_t code, int32_t value) : type(type), code(code), value(value) {}
  DeviceInputEvent(const struct input_event& ie);
  DeviceInputEvent(const DeviceInputEvent&) = default;
  DeviceInputEvent(DeviceInputEvent&&) = default;

  DeviceInputEvent& operator=(const DeviceInputEvent&) = default;
  DeviceInputEvent& operator=(DeviceInputEvent&&) = default;

  uint16_t type;
  uint16_t code;
  int32_t  value;

  bool operator==(const DeviceInputEvent& o) const;
  bool operator!=(const DeviceInputEvent& o) const;
  bool operator==(const struct input_event& o) const;
  bool operator<(const DeviceInputEvent& o) const;
  bool operator<(const struct input_event& o) const;
};

// -------------------------------------------------------------------------------------------------
QDataStream& operator<<(QDataStream& s, const DeviceInputEvent& die);
QDataStream& operator>>(QDataStream& s, DeviceInputEvent& die);

// -------------------------------------------------------------------------------------------------
template<typename T>
QDataStream& operator<<(QDataStream& s, const std::vector<T>& container)
{
  s << quint32(container.size());
  for (const auto& item : container) {
    s << item;
  }
  return s;
}

template<typename T>
QDataStream& operator>>(QDataStream& s, std::vector<T>& container)
{
  quint32 size{};
  s >> size;
  container.resize(size);
  for (quint64 i = 0; i < size; ++i) {
    s >> container[i];
  }
  return s;
}

// -------------------------------------------------------------------------------------------------
/// KeyEvent is a sequence of DeviceInputEvent.
using KeyEvent = std::vector<DeviceInputEvent>;

/// KeyEventSequence is a sequence of KeyEvents.
using KeyEventSequence = std::vector<KeyEvent>;
Q_DECLARE_METATYPE(KeyEventSequence);

// -------------------------------------------------------------------------------------------------
QDebug operator<<(QDebug debug, const DeviceInputEvent &ie);
QDebug operator<<(QDebug debug, const KeyEvent &ke);

// -------------------------------------------------------------------------------------------------
// Some inputs from Logitech Spotlight device (like Next Hold and Back Hold events) are not a valid
// input event (input_event in linux/input.h) in a conventional sense. They are communicated
// via HID++ messages from the device. Using the input mapper we need to
// reserve some KeyEventSequence for these events. These KeyEventSequence should be designed in
// such a way that they cannot interfere with other valid input events from the device.
namespace SpecialKeys
{
  enum class Key : uint16_t {
    NextHold = 0x0e10,
    BackHold = 0x0e11,
    NextHoldMove = 0x0ff0,
    BackHoldMove = 0x0ff1,
  };

  struct SpecialKeyEventSeqInfo {
    QString name;
    KeyEventSequence keyEventSeq;
  };

  const SpecialKeyEventSeqInfo& logitechSpotlightHoldMove(const KeyEventSequence& inputSequence);
  const SpecialKeyEventSeqInfo& eventSequenceInfo(SpecialKeys::Key key);
  const std::map<Key, SpecialKeyEventSeqInfo>& keyEventSequenceMap();
}

// -------------------------------------------------------------------------------------------------
class NativeKeySequence
{
public:
  enum Modifier : uint16_t {
    NoModifier  = 0,
    LeftCtrl    = 1 << 0,
    RightCtrl   = 1 << 1,
    LeftAlt     = 1 << 2,
    RightAlt    = 1 << 3,
    LeftShift   = 1 << 4,
    RightShift  = 1 << 5,
    LeftMeta    = 1 << 6,
    RightMeta   = 1 << 7,
  };

  NativeKeySequence();
  NativeKeySequence(NativeKeySequence&&) = default;
  NativeKeySequence(const NativeKeySequence&) = default;

  NativeKeySequence(const std::vector<int>& qtKeys,
                    std::vector<uint16_t>&& nativeModifiers,
                    KeyEventSequence&& kes);

  NativeKeySequence& operator=(NativeKeySequence&&) = default;
  NativeKeySequence& operator=(const NativeKeySequence&) = default;
  bool operator==(const NativeKeySequence& other) const;
  bool operator!=(const NativeKeySequence& other) const;

  void swap(NativeKeySequence& other);
  int count() const;
  bool empty() const { return count() == 0; }
  const auto& keySequence() const { return m_keySequence; }
  const auto& nativeSequence() const { return m_nativeSequence; }
  QString toString() const;

  void clear();

  friend QDataStream& operator>>(QDataStream& s, NativeKeySequence& ks) {
    return s >> ks.m_keySequence >> ks.m_nativeSequence >> ks.m_nativeModifiers;
  }

  friend QDataStream& operator<<(QDataStream& s, const NativeKeySequence& ks) {
    return s << ks.m_keySequence << ks.m_nativeSequence << ks.m_nativeModifiers;
  }

  static QString toString(int qtKey, uint16_t nativeModifiers);
  static QString toString(const std::vector<int>& qtKey,
                          const std::vector<uint16_t>& nativeModifiers);

  struct predefined {
    static const NativeKeySequence& altTab();
    static const NativeKeySequence& altF4();
    static const NativeKeySequence& meta();
  };

private:
  QKeySequence m_keySequence;
  KeyEventSequence m_nativeSequence;
  std::vector<uint16_t> m_nativeModifiers;
};
Q_DECLARE_METATYPE(NativeKeySequence)

// -------------------------------------------------------------------------------------------------
struct Action
{
  enum class Type {
    KeySequence = 1,
    CyclePresets = 2,
    ToggleSpotlight = 3,
    ToggleLaser = 4,
    TogglePointerMode = 5,
    ScrollHorizontal = 11,
    ScrollVertical = 12,
    VolumeControl = 13,
  };

  virtual ~Action() = default;

  virtual Type type() const = 0;
  virtual QDataStream& save(QDataStream&) const = 0;
  virtual QDataStream& load(QDataStream&) = 0;
  virtual bool empty() const = 0;
};

// -------------------------------------------------------------------------------------------------
const char* toString(Action::Type at, bool withClass = true);

// -------------------------------------------------------------------------------------------------
struct KeySequenceAction : public Action
{
  KeySequenceAction() = default;
  KeySequenceAction(const NativeKeySequence& ks) : keySequence(ks) {}
  Type type() const override { return Type::KeySequence; }
  QDataStream& save(QDataStream& s) const override { return s << keySequence; }
  QDataStream& load(QDataStream& s) override { return s >> keySequence; }
  bool empty() const override { return keySequence.empty(); }
  bool operator==(const KeySequenceAction& o) const { return keySequence == o.keySequence; }

  NativeKeySequence keySequence;
};

// -------------------------------------------------------------------------------------------------
struct CyclePresetsAction : public Action
{
  Type type() const override { return Type::CyclePresets; }
  QDataStream& save(QDataStream& s) const override { return s << placeholder; }
  QDataStream& load(QDataStream& s) override { return s >> placeholder; }
  bool empty() const override { return false; }
  bool operator==(const CyclePresetsAction&) const { return true; }
  bool placeholder = false;
};

// -------------------------------------------------------------------------------------------------
struct ToggleSpotlightAction : public Action
{
  Type type() const override { return Type::ToggleSpotlight; }
  QDataStream& save(QDataStream& s) const override { return s << placeholder; }
  QDataStream& load(QDataStream& s) override { return s >> placeholder; }
  bool empty() const override { return false; }
  bool operator==(const ToggleSpotlightAction&) const { return true; }
  bool placeholder = false;
};

// -------------------------------------------------------------------------------------------------
struct ToggleLaserAction : public Action
{
  Type type() const override { return Type::ToggleLaser; }
  QDataStream& save(QDataStream& s) const override { return s << placeholder; }
  QDataStream& load(QDataStream& s) override { return s >> placeholder; }
  bool empty() const override { return false; }
  bool operator==(const ToggleLaserAction&) const { return true; }
  bool placeholder = false;
};

// -------------------------------------------------------------------------------------------------
struct TogglePointerModeAction : public Action
{
  Type type() const override { return Type::TogglePointerMode; }
  QDataStream& save(QDataStream& s) const override { return s << placeholder; }
  QDataStream& load(QDataStream& s) override { return s >> placeholder; }
  bool empty() const override { return false; }
  bool operator==(const TogglePointerModeAction&) const { return true; }
  bool placeholder = false;
};

// -------------------------------------------------------------------------------------------------
struct ScrollHorizontalAction : public Action
{
  Type type() const override { return Type::ScrollHorizontal; }
  QDataStream& save(QDataStream& s) const override { return s << placeholder; }
  QDataStream& load(QDataStream& s) override { return s >> placeholder; }
  bool empty() const override { return false; }
  bool operator==(const ScrollHorizontalAction&) const { return true; }
  bool placeholder = false;

  int param = 0;
};

// -------------------------------------------------------------------------------------------------
struct ScrollVerticalAction : public Action
{
  Type type() const override { return Type::ScrollVertical; }
  QDataStream& save(QDataStream& s) const override { return s << placeholder; }
  QDataStream& load(QDataStream& s) override { return s >> placeholder; }
  bool empty() const override { return false; }
  bool operator==(const ScrollVerticalAction&) const { return true; }
  bool placeholder = false;

  int param = 0;
};

// -------------------------------------------------------------------------------------------------
struct VolumeControlAction : public Action
{
  Type type() const override { return Type::VolumeControl; }
  QDataStream& save(QDataStream& s) const override { return s << placeholder; }
  QDataStream& load(QDataStream& s) override { return s >> placeholder; }
  bool empty() const override { return false; }
  bool operator==(const VolumeControlAction&) const { return true; }
  bool placeholder = false;

  int param = 0;
};

// -------------------------------------------------------------------------------------------------
namespace GlobalActions {
  std::shared_ptr<ScrollHorizontalAction> scrollHorizontal();
  std::shared_ptr<ScrollVerticalAction> scrollVertical();
  std::shared_ptr<VolumeControlAction> volumeControl();
}

// -------------------------------------------------------------------------------------------------
struct MappedAction
{
  bool operator==(const MappedAction& o) const;
  std::shared_ptr<Action> action;
};
QDataStream& operator>>(QDataStream& s, MappedAction& mia);
QDataStream& operator<<(QDataStream& s, const MappedAction& mia);
Q_DECLARE_METATYPE(MappedAction);

// -------------------------------------------------------------------------------------------------
class InputMapConfig : public std::map<KeyEventSequence, MappedAction>{};

// -------------------------------------------------------------------------------------------------
class InputMapper : public QObject
{
  Q_OBJECT

public:
  InputMapper(
    std::shared_ptr<VirtualDevice> virtualMouse,
    std::shared_ptr<VirtualDevice> virtualKeyboard,
    QObject* parent = nullptr);

  ~InputMapper();

  void resetState(); // Reset any stored sequence state.

  // input_events = complete sequence including SYN event
  void addEvents(const struct input_event input_events[], size_t num);
  void addEvents(const KeyEvent& key_events);

  bool recordingMode() const;
  void setRecordingMode(bool recording);

  int keyEventInterval() const;
  void setKeyEventInterval(int interval);

  using SpecialMoveInputs = std::vector<SpecialKeys::SpecialKeyEventSeqInfo>;
  const SpecialMoveInputs& specialMoveInputs();
  void setSpecialMoveInputs(SpecialMoveInputs moveInputs);

  std::shared_ptr<VirtualDevice> virtualMouse() const;
  std::shared_ptr<VirtualDevice> virtualKeyboard() const;
  bool hasVirtualDevice() const;

  void setConfiguration(const InputMapConfig& config);
  void setConfiguration(InputMapConfig&& config);
  const InputMapConfig& configuration() const;

signals:
  void configurationChanged();
  void recordingModeChanged(bool recording);
  void keyEventRecorded(const KeyEvent&);
  // Right before first key event recorded:
  void recordingStarted();
  // After key sequence interval timer timeout or max sequence length reached
  void recordingFinished(bool canceled); // canceled if recordingMode was set to false instead of interval time out

  void actionMapped(std::shared_ptr<Action> action);

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
