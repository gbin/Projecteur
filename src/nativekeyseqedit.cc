// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "nativekeyseqedit.h"

#include "inputmapconfig.h"
#include "inputseqedit.h"

#include <KLocalizedString>

#include <linux/input.h>

#include <QApplication>
#include <QKeyEvent>
#include <QStaticText>
#include <QStyleOptionFrame>
#include <QStylePainter>
#include <QTimer>

namespace {
  // -----------------------------------------------------------------------------------------------
  constexpr int maxKeyCount = 4; // Same as QKeySequence
  constexpr int keySeqInterval = 950;
} // end anonymous namespace

// -------------------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------------------
NativeKeySeqEdit::NativeKeySeqEdit(QWidget* parent)
  : QWidget(parent)
  , m_timer(new QTimer(this))
{
  setFocusPolicy(Qt::StrongFocus); // Accept focus by tabbing and clicking
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setAttribute(Qt::WA_InputMethodEnabled, false);
  setAttribute(Qt::WA_MacShowFocusRect, true);

  m_timer->setSingleShot(true);
  m_timer->setInterval(keySeqInterval);
  connect(m_timer, &QTimer::timeout, this, [this](){ setRecording(false); });
}

// -------------------------------------------------------------------------------------------------
NativeKeySeqEdit::~NativeKeySeqEdit() = default;

// -------------------------------------------------------------------------------------------------
const NativeKeySequence& NativeKeySeqEdit::keySequence() const
{
  return m_nativeSequence;
}

// -------------------------------------------------------------------------------------------------
void NativeKeySeqEdit::setKeySequence(const NativeKeySequence& nks)
{
  if (nks == m_nativeSequence) { return; }

  m_nativeSequence = nks;
  update();
  emit keySequenceChanged(m_nativeSequence);
}

// -------------------------------------------------------------------------------------------------
void NativeKeySeqEdit::clear()
{
  if (m_nativeSequence.count() == 0) { return; }

  m_nativeSequence.clear();
  update();
  emit keySequenceChanged(m_nativeSequence);
}


// -------------------------------------------------------------------------------------------------
QStyleOptionFrame NativeKeySeqEdit::styleOption() const
{
  QStyleOptionFrame option;
  option.initFrom(this);
  option.rect = contentsRect();
  option.lineWidth = style()->pixelMetric(QStyle::PM_DefaultFrameWidth, &option, this);
  option.midLineWidth = 0;
  option.state |= (QStyle::State_Sunken | QStyle::State_ReadOnly);
  option.features = QStyleOptionFrame::None;
  return option;
}

// -------------------------------------------------------------------------------------------------
QSize NativeKeySeqEdit::sizeHint() const
{
  // Adjusted from QLineEdit::sizeHint (Qt 5.9)
  ensurePolished();

  const QStyleOptionFrame opt = styleOption();
  constexpr int verticalMargin = 3;
  constexpr int horizontalMargin = 3;
  const int h = opt.fontMetrics.height() + 2 * verticalMargin;

  const int w = std::max(opt.fontMetrics.horizontalAdvance(QLatin1Char('x')) * 17 + 2 * horizontalMargin,
                         opt.fontMetrics.horizontalAdvance(m_nativeSequence.toString()));
  return style()->sizeFromContents(QStyle::CT_LineEdit, &opt, QSize(w, h), this);
}

// -------------------------------------------------------------------------------------------------
void NativeKeySeqEdit::paintEvent(QPaintEvent* /* event */)
{
  const QStyleOptionFrame option = styleOption();

  QStylePainter p(this);
  p.drawPrimitive(QStyle::PE_PanelLineEdit, option);

  const auto& fm = option.fontMetrics;
  int xPos = (option.rect.height()-fm.height()) / 2;
  if (recording())
  {
    const int spacingX = static_cast<int>(QStaticText(" ").size().width());
    xPos += drawRecordingSymbol(xPos, p, option) + spacingX;
    if (m_recordedQtKeys.empty()) {
      xPos += drawPlaceHolderText(xPos, p, option, i18n("Press shortcut..."));
    } else {
      xPos += drawText(xPos, p, option, NativeKeySequence::toString(m_recordedQtKeys, m_recordedNativeModifiers));
      xPos += drawText(xPos, p, option, ", ...");
    }
  }
  else {
    xPos += drawSequence(xPos, p, option, m_nativeSequence);
  }
}

// -------------------------------------------------------------------------------------------------
void NativeKeySeqEdit::mouseDoubleClickEvent(QMouseEvent* e)
{
  QWidget::mouseDoubleClickEvent(e);
  e->accept();
  setRecording(!recording());
}

// -------------------------------------------------------------------------------------------------
void NativeKeySeqEdit::reset()
{
  m_timer->stop();
  m_recordedQtKeys.clear();
  m_recordedNativeModifiers.clear();
  m_recordedEvents.clear();
  m_lastKey = -1;
  m_nativeModifiersPressed.clear();
}

// -------------------------------------------------------------------------------------------------
void NativeKeySeqEdit::setRecording(bool doRecord)
{
  if (m_recording == doRecord) { return; }

  m_recording = doRecord;

  if (m_recording)
  { // started recording mode
    reset();
  }
  else
  { // finished recording
    if (m_recordedQtKeys.size() > 0)
    {
      NativeKeySequence recorded(m_recordedQtKeys, std::move(m_recordedNativeModifiers),
                                 std::move(m_recordedEvents));
      if (recorded != m_nativeSequence) {
        m_nativeSequence.swap(recorded);
        emit keySequenceChanged(m_nativeSequence);
      }
    }
    reset();
    emit editingFinished(this);
  }
  update();
  emit recordingChanged(m_recording);
}

//-------------------------------------------------------------------------------------------------
bool NativeKeySeqEdit::event(QEvent* e)
{
  switch (e->type())
  {
  case QEvent::KeyPress: {
    const auto ke = static_cast<QKeyEvent*>(e);
    if (recording() && (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab))
    {
      keyPressEvent(ke);
      e->accept();
      return true;
    }
    break;
  }
  case QEvent::Shortcut:
    return true;
  case QEvent::ShortcutOverride:
    e->accept();
    return true;
  default :
    break;
  }
  return QWidget::event(e);
}

// -------------------------------------------------------------------------------------------------
void NativeKeySeqEdit::recordKeyPressEvent(QKeyEvent* e)
{
  int key = m_lastKey = e->key();

  if (key == Qt::Key_Control
      || key == Qt::Key_Shift
      || key == Qt::Key_Meta
      || key == Qt::Key_Alt
      || key == Qt::Key_AltGr)
  {
    m_nativeModifiersPressed.insert(e->nativeScanCode() - 8); // See comment below about the -8;
    return;
  }

  if (key == Qt::Key_unknown) {
    return;
  }

  if (m_recordedQtKeys.size() >= maxKeyCount) {
    setRecording(false);
    return;
  }

  key |= getQtModifiers(e->modifiers());

  m_recordedQtKeys.push_back(key);
  m_recordedNativeModifiers.push_back(getNativeModifiers(m_nativeModifiersPressed));

  // TODO Verify that (nativeScanCode - 8) equals the codes from input-event-codes.h on
  // all Linux desktops.. (not only xcb..) - comes from #define MIN_KEYCODE 8 in evdev.c
  KeyEvent pressed; KeyEvent released;
  for (const auto modifierKey : m_nativeModifiersPressed)
  {
    pressed.emplace_back(EV_KEY, modifierKey, 1);
    released.emplace_back(EV_KEY, modifierKey, 0);
  }
  pressed.emplace_back(EV_KEY, e->nativeScanCode()-8, 1);
  released.emplace_back(EV_KEY, e->nativeScanCode()-8, 0);
  pressed.emplace_back(EV_SYN, SYN_REPORT, 0);
  released.emplace_back(EV_SYN, SYN_REPORT, 0);

  m_recordedEvents.emplace_back(std::move(pressed));
  m_recordedEvents.emplace_back(std::move(released));

  update();
  e->accept();

  if (m_recordedQtKeys.size() >= maxKeyCount) {
    setRecording(false);
  }
}

// -------------------------------------------------------------------------------------------------
void NativeKeySeqEdit::keyPressEvent(QKeyEvent* e)
{
  if (!recording())
  {
    if (e->key() == Qt::Key_Enter ||   e->key() == Qt::Key_Return)
    {
      setRecording(true);
      return;
    }

    if (e->key() == Qt::Key_Delete)
    {
      clear();
      return;
    }

    QWidget::keyPressEvent(e);
    return;
  }

  recordKeyPressEvent(e);
}

// -------------------------------------------------------------------------------------------------
void NativeKeySeqEdit::keyReleaseEvent(QKeyEvent* e)
{
  if (recording())
  {
    if (e->key() == Qt::Key_Control
        || e->key() == Qt::Key_Shift
        || e->key() == Qt::Key_Meta
        || e->key() == Qt::Key_Alt
        || e->key() == Qt::Key_AltGr)
    {
      m_nativeModifiersPressed.erase(e->nativeScanCode() - 8);
    }

    if (m_recordedQtKeys.size() && m_lastKey == e->key()) {
      if (m_recordedQtKeys.size() < maxKeyCount) {
        m_timer->start();
      } else {
        setRecording(false);
      }
    }
    return;
  }

  QWidget::keyReleaseEvent(e);
}

// -------------------------------------------------------------------------------------------------
void NativeKeySeqEdit::focusOutEvent(QFocusEvent* e)
{
  setRecording(false);
  QWidget::focusOutEvent(e);
}

// -------------------------------------------------------------------------------------------------
int NativeKeySeqEdit::getQtModifiers(Qt::KeyboardModifiers state)
{
  int result = 0;
  if (state & Qt::ControlModifier)     { result |= Qt::ControlModifier; }
  if (state & Qt::MetaModifier)        {  result |= Qt::MetaModifier; }
  if (state & Qt::AltModifier)         {  result |= Qt::AltModifier; }
  if (state & Qt::ShiftModifier)       {  result |= Qt::ShiftModifier; }
  if (state & Qt::GroupSwitchModifier) { result |= Qt::GroupSwitchModifier; }
  return result;
}

// -------------------------------------------------------------------------------------------------
uint16_t NativeKeySeqEdit::getNativeModifiers(const std::set<int>& modifiersPressed)
{
  using Modifier = NativeKeySequence::Modifier;
  uint16_t modifiers = Modifier::NoModifier;
  for (const auto& modKey : modifiersPressed)
  {
    switch(modKey)
    {
      case KEY_LEFTCTRL: modifiers |= Modifier::LeftCtrl; break;
      case KEY_RIGHTCTRL: modifiers |= Modifier::RightCtrl; break;
      case KEY_LEFTALT: modifiers |= Modifier::LeftAlt; break;
      case KEY_RIGHTALT: modifiers |= Modifier::RightAlt; break;
      case KEY_LEFTSHIFT: modifiers |= Modifier::LeftShift; break;
      case KEY_RIGHTSHIFT: modifiers |= Modifier::RightShift; break;
      case KEY_LEFTMETA: modifiers |= Modifier::LeftMeta; break;
      case KEY_RIGHTMETA: modifiers |= Modifier::RightMeta; break;
      default: break;
    }
  }
  return modifiers;
}

// -------------------------------------------------------------------------------------------------
int NativeKeySeqEdit::drawRecordingSymbol(int startX, QPainter& p, const QStyleOption& option)
{
  return InputSeqEdit::drawRecordingSymbol(startX, p, option);
}

// -------------------------------------------------------------------------------------------------
int NativeKeySeqEdit::drawPlaceHolderText(int startX, QPainter& p, const QStyleOption& option, const QString& text)
{
  return InputSeqEdit::drawPlaceHolderText(startX, p, option, text);
}

// -------------------------------------------------------------------------------------------------
int NativeKeySeqEdit::drawText(int startX, QPainter& p, const QStyleOption& option, const QString& text)
{
  const auto r = QRect(QPoint(startX + option.rect.left(), option.rect.top()),
                       option.rect.bottomRight());
  p.save();

  if (option.state & QStyle::State_Selected) {
    p.setPen(option.palette.color(QPalette::HighlightedText));
  } else {
    p.setPen(option.palette.color(QPalette::Text));
  }

  QRect br;
  p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, text, &br);
  p.restore();

  return br.width();
}

// -------------------------------------------------------------------------------------------------
int NativeKeySeqEdit::drawSequence(int startX, QPainter& p, const QStyleOption& option,
                                   const NativeKeySequence& ks, bool drawEmptyPlaceholder)
{
  if (ks.count() == 0)
  {
    if (!drawEmptyPlaceholder) { return 0; }
    return InputSeqEdit::drawEmptyIndicator(startX, p, option);
  }

  return drawText(startX, p, option, ks.toString());
}
