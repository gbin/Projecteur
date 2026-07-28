// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "inputseqedit.h"

#include "device-key-lookup.h"
#include "deviceinput.h"
#include "inputmapconfig.h"
#include "logging.h"

#include <KLocalizedString>

#include <QApplication>
#include <QMenu>
#include <QPaintEvent>
#include <QPainterPath>
#include <QStaticText>
#include <QStyleOptionFrame>
#include <QStylePainter>
#include <QVBoxLayout>

#include <algorithm>

#include <linux/input.h>

namespace {
  // -----------------------------------------------------------------------------------------------
  // Returns true if the second Key event 'equals' the first one, with the only difference, that the
  // first one is the key press and the second the release event.
  bool isButtonTap(const KeyEvent& first, const KeyEvent& second)
  {
    return std::equal(first.cbegin(), first.cend(), second.cbegin(), second.cend(),
      [](const DeviceInputEvent& e1, const DeviceInputEvent& e2)
      {
        if (e1.type != EV_KEY) { return e1 == e2; } // just compare for non key events

        return (e2.type == EV_KEY // special handling for key events...
                && e1.code == e2.code
                && e1.value == 1   // event 1 press
                && e2.value == 0); // event 2 release
      });
  }

  // -----------------------------------------------------------------------------------------------
  int drawKeyEvent(int startX, QPainter& p, const QStyleOption& option, const KeyEvent& ke,
                   const DeviceId& dId, bool buttonTap = false)
  {
    if (ke.empty()) { return 0; }

    static auto const pressChar = QChar(0x2193); // ↓
    static auto const releaseChar = QChar(0x2191); // ↑

    const auto& die = (ke.back().code != SYN_REPORT) ? ke.back() : ke.front();
    const auto& lookupName = KeyName::lookup(dId, die);

    // TODO Some devices (e.g. August WP 200) have buttons that send a key combination
    //      (modifiers + key) - this is ignored completely right now.
    const auto text = QString("[%1%2%3")
                         .arg(lookupName.isEmpty() ? QString("%1").arg(die.code, 0, 16) : lookupName)
                         .arg(buttonTap ? pressChar
                                        : ke.back().value ? pressChar : releaseChar)
                         .arg(buttonTap ? "" : "]");

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

    if (buttonTap)
    {
      QRect br2; // draw down and up arrow closer together
      const auto t2 = QString("%2]").arg(releaseChar);
      const auto w = option.fontMetrics.rightBearing(pressChar)
                   + option.fontMetrics.leftBearing(releaseChar);
      p.drawText(r.adjusted(br.width() - w, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, t2, &br2);
      br.setWidth(br.width() + br2.width());
    }

    p.restore();

    return br.width();
  }

  // -----------------------------------------------------------------------------------------------
  int drawKeyEventSequence(int startX, QPainter& p, const QStyleOption& option,
                           const KeyEventSequence& kes, const DeviceId& dId,
                           bool drawEmptyPlaceholder = true)
  {
    if (kes.empty())
    {
      if (!drawEmptyPlaceholder) { return 0; }
      return InputSeqEdit::drawEmptyIndicator(startX, p, option);
    }

    int sequenceWidth = 0;
    const int paddingX = static_cast<int>(QStaticText(" ").size().width());
    for (auto it = kes.cbegin(); it!=kes.cend(); ++it)
    {
      if (it != kes.cbegin()) { sequenceWidth += paddingX; }
      if (startX + sequenceWidth >= option.rect.width()) { break; }

      const bool isTap = [&]()
      { // Check if this event and the next event represent a button press & release
        const auto next = std::next(it);
        if (next != kes.cend() && isButtonTap(*it, *next)) {
          it = next;
          return true;
        }
        return false;
      }();

      sequenceWidth += drawKeyEvent(startX + sequenceWidth, p, option, *it, dId, isTap);
    }

    return sequenceWidth;
  }

  // -----------------------------------------------------------------------------------------------
  int drawPlaceHolderText(int startX, QPainter& p, const QStyleOption& option, const QString& text, bool textDisabled)
  {
    const auto r = QRect(QPoint(startX + option.rect.left(), option.rect.top()),
                         option.rect.bottomRight());

    p.save();
    if (textDisabled)
    {
      p.setPen(option.palette.color(QPalette::Disabled, QPalette::Text));
    }
    else
    {
      p.setPen(option.palette.color(QPalette::Text));
    }
    QRect br;
    p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, text, &br);
    p.restore();

    return br.width();
  }
} // end anonymous namespace

// -------------------------------------------------------------------------------------------------
InputSeqEdit::InputSeqEdit(InputMapper* im, const DeviceId& dId, QWidget* parent)
  : QWidget(parent)
  , m_deviceId(dId)
{
  setInputMapper(im);

  setFocusPolicy(Qt::StrongFocus); // Accept focus by tabbing and clicking
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setAttribute(Qt::WA_InputMethodEnabled, false);
  setAttribute(Qt::WA_MacShowFocusRect, true);
}

// -------------------------------------------------------------------------------------------------
InputSeqEdit::~InputSeqEdit() = default;

// -------------------------------------------------------------------------------------------------
QStyleOptionFrame InputSeqEdit::styleOption() const
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
QSize InputSeqEdit::sizeHint() const
{
  // Adjusted from QLineEdit::sizeHint (Qt 5.9)
  ensurePolished();
  QFontMetrics fm(font());
  constexpr int verticalMargin = 3;
  constexpr int horizontalMargin = 3;
  const int h = fm.height() + 2 * verticalMargin;
  const int w = fm.horizontalAdvance(QLatin1Char('x')) * 17 + 2 * horizontalMargin;

  const QStyleOptionFrame option = styleOption();

  return style()->sizeFromContents(QStyle::CT_LineEdit, &option, QSize(w, h), this);
}

// -------------------------------------------------------------------------------------------------
void InputSeqEdit::paintEvent(QPaintEvent* /* paintEvent */)
{
  const QStyleOptionFrame option = styleOption();

  QStylePainter p(this);
  p.drawPrimitive(QStyle::PE_PanelLineEdit, option);

  const bool recording = m_inputMapper && m_inputMapper->recordingMode();

  const auto& fm = option.fontMetrics;
  int xPos = (option.rect.height()-fm.height()) / 2;

  if (recording)
  {
    const auto spacingX = QStaticText(" ").size().width();
    xPos += drawRecordingSymbol(xPos, p, option) + spacingX;
    if (m_recordedSequence.empty()) {
      drawPlaceHolderText(xPos, p, option, i18n("Press device button(s)..."));
    } else {
      drawKeyEventSequence(xPos, p, option, m_recordedSequence, m_deviceId, false);
    }
  }
  else {
    drawKeyEventSequence(xPos, p, option, m_inputSequence, m_deviceId);
  }
}

// -------------------------------------------------------------------------------------------------
const KeyEventSequence& InputSeqEdit::inputSequence() const
{
  return m_inputSequence;
}

// -------------------------------------------------------------------------------------------------
void InputSeqEdit::setInputSequence(const KeyEventSequence& is)
{
  if (is == m_inputSequence) { return; }

  m_inputSequence = is;
  update();
  emit inputSequenceChanged(m_inputSequence);
}

// -------------------------------------------------------------------------------------------------
void InputSeqEdit::clear()
{
  if (m_inputSequence.empty()) { return; }

  m_inputSequence.clear();
  update();
  emit inputSequenceChanged(m_inputSequence);
}

// -------------------------------------------------------------------------------------------------
void InputSeqEdit::mouseDoubleClickEvent(QMouseEvent* e)
{
  QWidget::mouseDoubleClickEvent(e);
  if (!m_inputMapper) { return; }
  e->accept();
  m_inputMapper->setRecordingMode(!m_inputMapper->recordingMode());
}

//-------------------------------------------------------------------------------------------------
void InputSeqEdit::keyPressEvent(QKeyEvent* e)
{
  if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return)
  {
    m_inputMapper->setRecordingMode(!m_inputMapper->recordingMode());
    return;
  }

  if (e->key() == Qt::Key_Escape)
  {
    if (m_inputMapper && m_inputMapper->recordingMode()) {
      m_inputMapper->setRecordingMode(false);
      return;
    }
  }
  else if (e->key() == Qt::Key_Delete)
  {
    if (m_inputMapper && m_inputMapper->recordingMode()) {
      m_inputMapper->setRecordingMode(false);
    } else {
      setInputSequence(KeyEventSequence{});
    }
    return;
  }

  QWidget::keyPressEvent(e);
}

// -------------------------------------------------------------------------------------------------
void InputSeqEdit::keyReleaseEvent(QKeyEvent* e)
{
  QWidget::keyReleaseEvent(e);
}

// -------------------------------------------------------------------------------------------------
void InputSeqEdit::focusOutEvent(QFocusEvent* e)
{
  if (m_inputMapper) {
    m_inputMapper->setRecordingMode(false);
  }

  QWidget::focusOutEvent(e);
}

// -------------------------------------------------------------------------------------------------
void InputSeqEdit::setInputMapper(InputMapper* im)
{
  if (m_inputMapper == im) { return; }

  const auto removeIm = [this](){
    if (m_inputMapper) {
      m_inputMapper->disconnect(this);
      this->disconnect(m_inputMapper);
    }
    m_inputMapper = nullptr;
  };

  removeIm();
  m_inputMapper = im;
  if (m_inputMapper == nullptr) { return; }

  connect(m_inputMapper, &InputMapper::destroyed, this,
  [removeIm=std::move(removeIm)](){
    removeIm();
  });

  connect(m_inputMapper, &InputMapper::recordingStarted, this, [this](){
    m_recordedSequence.clear();
  });

  connect(m_inputMapper, &InputMapper::recordingFinished, this, [this](bool canceled){
    if (!canceled) { setInputSequence(m_recordedSequence); }
    m_inputMapper->setRecordingMode(false);
    m_recordedSequence.clear();
  });

  connect(m_inputMapper, &InputMapper::recordingModeChanged, this, [this](bool recording){
    update();
    if (!recording) { emit editingFinished(this); }
  });

  connect(m_inputMapper, &InputMapper::keyEventRecorded, this, [this](const KeyEvent& ke){
    m_recordedSequence.push_back(ke);
    if (m_recordedSequence.size() >= m_maxRecordingLength) {
      setInputSequence(m_recordedSequence);
      m_inputMapper->setRecordingMode(false);
    } else {
      update();
    }
  });
}

// -------------------------------------------------------------------------------------------------
int InputSeqEdit::drawRecordingSymbol(int startX, QPainter& p, const QStyleOption& option)
{
  const auto iconSize = option.fontMetrics.height();
  const auto marginTop = (option.rect.height() - iconSize) / 2;
  const QRect iconRect(startX, marginTop, iconSize, iconSize);

  p.save();
  p.setPen(Qt::lightGray);
  p.setBrush(QBrush(Qt::red));
  p.setRenderHint(QPainter::Antialiasing);
  p.drawEllipse(iconRect);
  p.restore();

  return iconRect.width();
}

// -------------------------------------------------------------------------------------------------
int InputSeqEdit::drawPlaceHolderText(int startX, QPainter& p, const QStyleOption& option, const QString& text)
{
  return ::drawPlaceHolderText(startX, p, option, text, true);
}

// -------------------------------------------------------------------------------------------------
int InputSeqEdit::drawEmptyIndicator(int startX, QPainter& p, const QStyleOption& option)
{
  p.save();
  p.setFont([&p](){ auto f = p.font(); f.setItalic(true); return f; }());
  if (option.state & QStyle::State_Selected) {
    p.setPen(option.palette.color(QPalette::Disabled, QPalette::HighlightedText));
  } else {
    p.setPen(option.palette.color(QPalette::Disabled, QPalette::Text));
  }

  static const QStaticText textNone(i18n("None"));
  const auto top = static_cast<int>((option.rect.height() - textNone.size().height()) / 2);
  p.drawStaticText(startX + option.rect.left(), option.rect.top() + top, textNone);
  p.restore();
  return static_cast<int>(textNone.size().width());
}

// -------------------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------------------
void InputSeqDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const
{
  // Let QStyledItemDelegate handle drawing current focus inidicator and other basic stuff..
  QStyledItemDelegate::paint(painter, option, index);
  const auto imModel = qobject_cast<const InputMapConfigModel*>(index.model());
  if (!imModel) { return; }

  // Our custom drawing of the KeyEventSequence...
  const auto& fm = option.fontMetrics;
  const int xPos = (option.rect.height()-fm.height()) / 2;
  const auto& keySeq = imModel->configData(index).deviceSequence;
  const auto& holdMoveEvent = SpecialKeys::logitechSpotlightHoldMove(keySeq);

  if (!holdMoveEvent.name.isEmpty()) {
    drawPlaceHolderText(xPos, *painter, option, holdMoveEvent.name, false);
  }
  else {
    drawKeyEventSequence(xPos, *painter, option, keySeq, imModel->deviceId());
  }

  if (option.state & QStyle::State_HasFocus) {
    drawCurrentIndicator(*painter, option);
  }
}

// -------------------------------------------------------------------------------------------------
void InputSeqDelegate::drawCurrentIndicator(QPainter &p, const QStyleOption &option)
{
  p.save();
  const auto squareSize = option.rect.height() / 3;
  const QRectF rect(option.rect.bottomRight()-QPoint(squareSize, squareSize),
                    QSize(squareSize, squareSize));

  const auto brush = QBrush((option.state & QStyle::State_Selected)
                              ? option.palette.color(QPalette::HighlightedText)
                              : option.palette.color(QPalette::Highlight));
  {
    QPainterPath path(rect.topRight());
    path.lineTo(rect.bottomRight());
    path.lineTo(rect.bottomLeft());
    path.lineTo(rect.topRight());
    p.fillPath(path, brush);
  }
  p.restore();
}

// -------------------------------------------------------------------------------------------------
QWidget* InputSeqDelegate::createEditor(QWidget* parent,
                                        const QStyleOptionViewItem& /*option*/,
                                        const QModelIndex& index) const

{
  if (const auto imModel = qobject_cast<const InputMapConfigModel*>(index.model()))
  {
    if (imModel->inputMapper()) { imModel->inputMapper()->setRecordingMode(false); }
    auto *editor = new InputSeqEdit(imModel->inputMapper(), imModel->deviceId(), parent);
    connect(editor, &InputSeqEdit::editingFinished, this, &InputSeqDelegate::commitAndCloseEditor);
    if (imModel->inputMapper()) { imModel->inputMapper()->setRecordingMode(true); }
    return editor;
  }

  return nullptr;
}

// -------------------------------------------------------------------------------------------------
void InputSeqDelegate::commitAndCloseEditor(InputSeqEdit* editor)
{
  emit commitData(editor);
  emit closeEditor(editor);
}

// -------------------------------------------------------------------------------------------------
void InputSeqDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
  if (const auto seqEditor = qobject_cast<InputSeqEdit*>(editor))
  {
    if (const auto imModel = qobject_cast<const InputMapConfigModel*>(index.model()))
    {
      seqEditor->setInputSequence(imModel->configData(index).deviceSequence);
      return;
    }
  }

  QStyledItemDelegate::setEditorData(editor, index);
}

// -------------------------------------------------------------------------------------------------
void InputSeqDelegate::setModelData(QWidget* editor, QAbstractItemModel* model,
                                    const QModelIndex& index) const
{
  if (const auto seqEditor = qobject_cast<InputSeqEdit*>(editor))
  {
    if (const auto imModel = qobject_cast<InputMapConfigModel*>(model))
    {
      imModel->setInputSequence(index, seqEditor->inputSequence());
      return;
    }
  }

  QStyledItemDelegate::setModelData(editor, model, index);
}

// -------------------------------------------------------------------------------------------------
QSize InputSeqDelegate::sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const
{
  if (const auto imModel = qobject_cast<const InputMapConfigModel*>(index.model()))
  {
    // TODO Calculate size hint from KeyEventSequence.....
    return QStyledItemDelegate::sizeHint(option, index);
  }
  return QStyledItemDelegate::sizeHint(option, index);
}

// -------------------------------------------------------------------------------------------------
void InputSeqDelegate::inputSeqContextMenu(QWidget* parent, InputMapConfigModel* model,
                                           const QModelIndex& index, const QPoint& globalPos)
{
  if (!index.isValid() || !model) { return; }

  const auto& specialMoveInputs = model->inputMapper()->specialMoveInputs();
  if (!specialMoveInputs.empty())
  {
    auto* const menu = new QMenu(parent);

    for (const auto& input : specialMoveInputs)
    {
      const auto qaction = menu->addAction(input.name);
      connect(qaction, &QAction::triggered, this, [model, index, inputSeq=input.keyEventSeq](){
        model->setInputSequence(index, inputSeq);
        const auto& currentItem = model->configData(index);
        if (!currentItem.action) {
          model->setItemActionType(index, Action::Type::ScrollVertical);
        }
        else
        {
          switch (currentItem.action->type())
          {
            case Action::Type::ScrollHorizontal:   // [[fallthrough]];
            case Action::Type::ScrollVertical:     // [[fallthrough]];
            case Action::Type::VolumeControl: {
              // scrolling and volume control allowed for special input
              break;
            }
            default: {
              model->setItemActionType(index, Action::Type::ScrollVertical);
              break;
            }
          }
        }
      });
    }

    menu->exec(globalPos);
    menu->deleteLater();
  }
}
