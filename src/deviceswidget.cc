// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "deviceswidget.h"

#include "device-hidpp.h"
#include "device-vibration.h"
#include "deviceinput.h"
#include "iconwidgets.h"
#include "inputmapconfig.h"
#include "logging.h"
#include "settings.h"
#include "spotlight.h"

#include <QComboBox>
#include <QLabel>
#include <QLayout>
#include <QShortcut>
#include <QSpinBox>
#include <QStackedLayout>
#include <QStyle>
#include <QTabWidget>
#include <QTextEdit>
#include <QTextList>
#include <QTimer>

DECLARE_LOGGING_CATEGORY(preferences)

// -------------------------------------------------------------------------------------------------
namespace {
  const auto hexId = logging::hexId;

  QString descriptionString(const QString& name, const DeviceId& id) {
    return QString("%1 (%2:%3) [%4]").arg(name, hexId(id.vendorId), hexId(id.productId), id.phys);
  }

  const auto invalidDeviceId = DeviceId(); // vendorId = 0, productId = 0

  bool removeTab(QTabWidget* tabWidget, QWidget* widget)
  {
    const auto idx = tabWidget->indexOf(widget);
    if (idx >= 0) {
      tabWidget->removeTab(idx);
      return true;
    }
    return false;
  }
} // end anonymous namespace

// -------------------------------------------------------------------------------------------------
DevicesWidget::DevicesWidget(Settings* settings, Spotlight* spotlight, QWidget* parent)
  : QWidget(parent)
{
  createDeviceComboBox(spotlight);

  const auto stackLayout = new QStackedLayout(this);
  const auto disconnectedWidget = createDisconnectedStateWidget();
  stackLayout->addWidget(disconnectedWidget);
  const auto deviceWidget = createDevicesWidget(settings, spotlight);
  stackLayout->addWidget(deviceWidget);

  const bool anyDeviceConnected = spotlight->anySpotlightDeviceConnected();
  stackLayout->setCurrentWidget(anyDeviceConnected ? deviceWidget : disconnectedWidget);

  connect(spotlight, &Spotlight::anySpotlightDeviceConnectedChanged, this,
  [stackLayout, deviceWidget, disconnectedWidget](bool anyConnected){
    stackLayout->setCurrentWidget(anyConnected ? deviceWidget : disconnectedWidget);
  });
}

// -------------------------------------------------------------------------------------------------
DeviceId DevicesWidget::currentDeviceId() const
{
  if (m_devicesCombo->currentIndex() < 0) {
    return invalidDeviceId;
  }

  return qvariant_cast<DeviceId>(m_devicesCombo->currentData());
}

// -------------------------------------------------------------------------------------------------
TimerTabWidget* DevicesWidget::createTimerTabWidget(Settings* settings, Spotlight* spotlight)
{
  Q_UNUSED(spotlight);
  const auto w = new TimerTabWidget(settings, this);
  w->loadSettings(currentDeviceId());

  connect(this, &DevicesWidget::currentDeviceChanged, this, [this](const DeviceId& dId) {
    if (m_timerTabWidget) { m_timerTabWidget->loadSettings(dId); }
  });

  return w;
}

// -------------------------------------------------------------------------------------------------
QWidget* DevicesWidget::createDevicesWidget(Settings* settings, Spotlight* spotlight)
{
  const auto dw = new QWidget(this);
  const auto vLayout = new QVBoxLayout(dw);
  const auto devHLayout = new QHBoxLayout();
  vLayout->addLayout(devHLayout);

  devHLayout->addWidget(new QLabel(tr("Device"), dw));
  devHLayout->addWidget(m_devicesCombo);
  devHLayout->setStretch(1, 1);

  vLayout->addSpacing(10);

  m_tabWidget = new QTabWidget(dw);
  vLayout->addWidget(m_tabWidget);

  m_tabWidget->addTab(createInputMapperWidget(settings, spotlight), tr("Input Mapping"));
  m_timerTabWidget = createTimerTabWidget(settings, spotlight);

  updateTimerTab(spotlight);

  m_deviceDetailsTabWidget = createDeviceInfoWidget(spotlight);
  m_tabWidget->addTab(m_deviceDetailsTabWidget, tr("Details"));

  // Update the timer tab when the current device has changed
  connect(this, &DevicesWidget::currentDeviceChanged, this,
  [spotlight, this]() { updateTimerTab(spotlight); });

  return dw;
}

// -------------------------------------------------------------------------------------------------
QWidget* DevicesWidget::createDeviceInfoWidget(Spotlight* spotlight)
{
  const auto diWidget = new DeviceInfoWidget(this);

  connect(this, &DevicesWidget::currentDeviceChanged, this,
  [diWidget, spotlight](const DeviceId& dId) {
    diWidget->setDeviceConnection(spotlight->deviceConnection(dId).get());
  });

  diWidget->setDeviceConnection(spotlight->deviceConnection(currentDeviceId()).get());
  return diWidget;
}

// -------------------------------------------------------------------------------------------------
QWidget* DevicesWidget::createInputMapperWidget(Settings* settings, Spotlight* /*spotlight*/)
{
  const auto delShortcut = new QShortcut( QKeySequence(Qt::ShiftModifier | Qt::Key_Delete), this);

  const auto imWidget = new QWidget(this);
  const auto layout = new QVBoxLayout(imWidget);
  const auto intervalLayout = new QHBoxLayout();

  const auto addBtn = new IconButton(Font::Icon::plus_5, imWidget);
  addBtn->setToolTip(tr("Add a new input mapping entry."));
  const auto delBtn = new IconButton(Font::Icon::trash_can_1, imWidget);
  delBtn->setToolTip(tr("Delete the selected input mapping entries (%1).", "%1=shortcut")
                       .arg(delShortcut->key().toString()));
  delBtn->setEnabled(false);

  const auto intervalLbl = new QLabel(tr("Input Sequence Interval"), imWidget);
  const auto intervalSb = new QSpinBox(this);
  const auto intervalUnitLbl = new QLabel(tr("ms"), imWidget);
  intervalSb->setMaximum(settings->inputSequenceIntervalRange().max);
  intervalSb->setMinimum(settings->inputSequenceIntervalRange().min);
  intervalSb->setValue(m_inputMapper ? m_inputMapper->keyEventInterval()
                                     : settings->deviceInputSeqInterval(currentDeviceId()));
  intervalSb->setSingleStep(50);

  intervalLayout->addWidget(addBtn);
  intervalLayout->addWidget(delBtn);
  intervalLayout->addStretch(1);
  intervalLayout->addWidget(intervalLbl);
  intervalLayout->addWidget(intervalSb);
  intervalLayout->addWidget(intervalUnitLbl);

  const auto tblView = new InputMapConfigView(imWidget);
  const auto imModel = new InputMapConfigModel(m_inputMapper, currentDeviceId(), imWidget);
  if (m_inputMapper) { imModel->setConfiguration(m_inputMapper->configuration()); }

  tblView->setModel(imModel);
  const auto selectionModel = tblView->selectionModel();

  auto updateImWidget = [this, imWidget]() {
    imWidget->setDisabled(!m_inputMapper || !m_inputMapper->hasVirtualDevice());
  };
  updateImWidget();

  connect(this, &DevicesWidget::currentDeviceChanged, this,
  [this, imModel, intervalSb, updateImWidget=std::move(updateImWidget)](const DeviceId& dId)
  {
    imModel->setInputMapper(m_inputMapper);
    if (m_inputMapper) {
      intervalSb->setValue(m_inputMapper->keyEventInterval());
      imModel->setConfiguration(m_inputMapper->configuration());
      imModel->setDeviceId(dId);
    }
    updateImWidget();
  });

  connect(intervalSb, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
  this, [this, settings](int valueMs) {
    if (m_inputMapper) {
      m_inputMapper->setKeyEventInterval(valueMs);
      settings->setDeviceInputSeqInterval(currentDeviceId(), valueMs);
    }
  });

  connect(selectionModel, &QItemSelectionModel::selectionChanged, this,
  [delBtn, selectionModel](){
    delBtn->setEnabled(selectionModel->hasSelection());
  });

  auto removeCurrentSelection = [imModel, selectionModel](){
    const auto selectedRows = selectionModel->selectedRows();
    std::vector<int> rows;
    rows.reserve(selectedRows.size());
    for (const auto& selectedRow : selectedRows) {
      rows.emplace_back(selectedRow.row());
    }
    imModel->removeConfigItemRows(std::move(rows));
  };

  connect(delBtn, &QToolButton::clicked, this, removeCurrentSelection);
  // --- Delete selected items on Shift + Delete
  connect(delShortcut, &QShortcut::activated, this, std::move(removeCurrentSelection));

  connect(addBtn, &QToolButton::clicked, this, [imModel, tblView](){
    tblView->selectRow(imModel->addNewItem(std::make_shared<KeySequenceAction>()));
  });

  layout->addLayout(intervalLayout);
  layout->addWidget(tblView);
  return imWidget;
}

// -------------------------------------------------------------------------------------------------
void DevicesWidget::createDeviceComboBox(Spotlight* spotlight)
{
  m_devicesCombo = new QComboBox(this);
  m_devicesCombo->setToolTip(tr("List of connected devices."));

  for (const auto& dev : spotlight->connectedDevices()) {
    const auto data = QVariant::fromValue(dev.id);
    if (m_devicesCombo->findData(data) < 0) {
      m_devicesCombo->addItem(descriptionString(dev.name, dev.id), data);
    }
  }

  connect(spotlight, &Spotlight::deviceDisconnected, this,
  [this](const DeviceId& id, const QString& /*name*/)
  {
    const auto idx = m_devicesCombo->findData(QVariant::fromValue(id));
    if (idx >= 0) {
      m_devicesCombo->removeItem(idx);
    }
  });

  connect(spotlight, &Spotlight::deviceConnected, this,
  [this](const DeviceId& id, const QString& name)
  {
    const auto data = QVariant::fromValue(id);
    if (m_devicesCombo->findData(data) < 0) {
      m_devicesCombo->addItem(descriptionString(name, id), data);
    }
  });

  connect(m_devicesCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
  [this, spotlight](int index)
  {
    if (index < 0)
    {
      m_inputMapper = nullptr;
      emit currentDeviceChanged(invalidDeviceId);
      return;
    }

    const auto devId = qvariant_cast<DeviceId>(m_devicesCombo->itemData(index));
    const auto currentConn = spotlight->deviceConnection(devId);
    m_inputMapper = currentConn ? currentConn->inputMapper().get() : nullptr;

    emit currentDeviceChanged(devId);
  });

  const auto currentConn = spotlight->deviceConnection(currentDeviceId());
  m_inputMapper = currentConn ? currentConn->inputMapper().get() : nullptr;
}

// -------------------------------------------------------------------------------------------------
QWidget* DevicesWidget::createDisconnectedStateWidget()
{
  const auto stateWidget = new QWidget(this);
  const auto hbox = new QHBoxLayout(stateWidget);
  const auto label = new QLabel(tr("No devices connected."), stateWidget);
  label->setToolTip(label->text());
  auto icon = style()->standardIcon(QStyle::SP_MessageBoxWarning);
  const auto iconLabel = new QLabel(stateWidget);
  iconLabel->setPixmap(icon.pixmap(16,16));
  hbox->addStretch();
  hbox->addWidget(iconLabel);
  hbox->addWidget(label);
  hbox->addStretch();
  return stateWidget;
}

// -------------------------------------------------------------------------------------------------
TimerTabWidget::TimerTabWidget(Settings* settings, QWidget* parent)
  : QWidget(parent)
  , m_settings(settings)
  , m_multiTimerWidget(new MultiTimerWidget(this))
  , m_vibrationSettingsWidget(new VibrationSettingsWidget(this))
{
  const auto layout = new QVBoxLayout(this);

  layout->addWidget(m_multiTimerWidget);
  layout->addWidget(m_vibrationSettingsWidget);

  connect(m_multiTimerWidget, &MultiTimerWidget::timerValueChanged, this,
  [this](int id, int secs) {
    m_settings->setTimerSettings(m_deviceId, id, m_multiTimerWidget->timerEnabled(id), secs);
  });

  connect(m_multiTimerWidget, &MultiTimerWidget::timerEnabledChanged, this,
  [this](int id, bool enabled) {
    m_settings->setTimerSettings(m_deviceId, id, enabled, m_multiTimerWidget->timerValue(id));
  });

  connect(m_vibrationSettingsWidget, &VibrationSettingsWidget::intensityChanged, this,
  [this](uint8_t intensity) {
    m_settings->setVibrationSettings(m_deviceId, m_vibrationSettingsWidget->length(), intensity);
  });

  connect(m_vibrationSettingsWidget, &VibrationSettingsWidget::lengthChanged, this,
  [this](uint8_t len) {
    m_settings->setVibrationSettings(m_deviceId, len, m_vibrationSettingsWidget->intensity());
  });

  connect(m_multiTimerWidget, &MultiTimerWidget::timeout,
          m_vibrationSettingsWidget, &VibrationSettingsWidget::sendVibrateCommand);
}

// -------------------------------------------------------------------------------------------------
void DevicesWidget::updateTimerTab(Spotlight* spotlight)
{
    // Helper method to return the first subconnection that supports vibrate.
  auto getVibrateConnection = [](const std::shared_ptr<DeviceConnection>& conn) {
    if (conn) {
      for (const auto& item : conn->subDevices()) {
        if (item.second->hasFlags(DeviceFlag::Vibrate)) { return item.second; }
      }
    }
    return std::shared_ptr<SubDeviceConnection>{};
  };

  const auto currentConn = spotlight->deviceConnection(currentDeviceId());
  const auto vibrateConn = getVibrateConnection(currentConn);

  if (m_timerTabContext) { m_timerTabContext->deleteLater(); }

  if (vibrateConn)
  {
    if (m_tabWidget->indexOf(m_timerTabWidget) < 0) {
      m_tabWidget->insertTab(1, m_timerTabWidget, tr("Vibration Timer"));
    }
    m_timerTabWidget->setSubDeviceConnection(vibrateConn.get());
  }
  else if (m_timerTabWidget) {
    removeTab(m_tabWidget, m_timerTabWidget);
    m_timerTabWidget->setSubDeviceConnection(nullptr);
  }

  if (currentConn) {
    m_timerTabContext = QPointer<QObject>(new QObject(this));
    connect(&*currentConn, &DeviceConnection::subDeviceFlagsChanged, m_timerTabContext,
    [currId=currentDeviceId(), spotlight, this](const DeviceId& id, const QString& /* path */) {
      if (currId != id) { return; }
      updateTimerTab(spotlight);
    });
  }

}

// -------------------------------------------------------------------------------------------------
void TimerTabWidget::loadSettings(const DeviceId& deviceId)
{
  m_multiTimerWidget->stopAllTimers();
  m_multiTimerWidget->blockSignals(true);
  m_vibrationSettingsWidget->blockSignals(true);

  m_deviceId = deviceId;

  for (int i = 0; i < m_multiTimerWidget->timerCount(); ++i) {
    const auto ts = m_settings->timerSettings(deviceId, i);
    m_multiTimerWidget->setTimerEnabled(i, ts.first);
    m_multiTimerWidget->setTimerValue(i, ts.second);
  }

  const auto vs = m_settings->vibrationSettings(deviceId);
  m_vibrationSettingsWidget->setLength(vs.first);
  m_vibrationSettingsWidget->setIntensity(vs.second);

  m_vibrationSettingsWidget->blockSignals(false);
  m_multiTimerWidget->blockSignals(false);
}

// -------------------------------------------------------------------------------------------------
void TimerTabWidget::setSubDeviceConnection(SubDeviceConnection* sdc) {
  m_vibrationSettingsWidget->setSubDeviceConnection(sdc);
}

// -------------------------------------------------------------------------------------------------
DeviceInfoWidget::DeviceInfoWidget(QWidget* parent)
  : QWidget(parent)
  , m_textEdit(new QTextEdit(this))
  , m_delayedUpdateTimer(new QTimer(this))
  , m_batteryInfoTimer(new QTimer(this))
{
  m_textEdit->setReadOnly(true);

  const auto layout = new QVBoxLayout(this);
  layout->addWidget(m_textEdit);

  constexpr int delayedUpdateTimerInterval = 150;
  m_delayedUpdateTimer->setSingleShot(true);
  m_delayedUpdateTimer->setInterval(delayedUpdateTimerInterval);
  connect(m_delayedUpdateTimer, &QTimer::timeout, this, &DeviceInfoWidget::updateTextEdit);

  m_batteryInfoTimer->setSingleShot(false);
  m_batteryInfoTimer->setTimerType(Qt::VeryCoarseTimer);
  m_batteryInfoTimer->setInterval(5 * 60 * 1000); // 5 minutes
}

// -------------------------------------------------------------------------------------------------
void DeviceInfoWidget::delayedTextEditUpdate() {
  m_delayedUpdateTimer->start();
}

// -------------------------------------------------------------------------------------------------
void DeviceInfoWidget::setDeviceConnection(DeviceConnection* connection)
{
  if (m_connection == connection) { return; }
  if (m_connectionContext) { m_connectionContext->deleteLater(); }

  m_connection = connection;

  if (m_connection.isNull())
  {
    m_delayedUpdateTimer->stop();
    m_batteryInfoTimer->stop();
    m_textEdit->clear();
    return;
  }

  m_connectionContext = new QObject(this);

  m_deviceBaseInfo.clear();
  m_deviceBaseInfo.emplace_back("Name", m_connection->deviceName());
  m_deviceBaseInfo.emplace_back("VendorId", hexId(m_connection->deviceId().vendorId));
  m_deviceBaseInfo.emplace_back("ProductId", hexId(m_connection->deviceId().productId));
  m_deviceBaseInfo.emplace_back("Phys", m_connection->deviceId().phys);
  m_deviceBaseInfo.emplace_back("Bus Type", toString(m_connection->deviceId().busType, false));

  connect(m_connection, &DeviceConnection::subDeviceConnected, m_connectionContext,
  [this](const DeviceId& /* deviceId */, const QString& path)
  {
    if (const auto sdc = m_connection->subDevice(path))
    {
      updateSubdeviceInfo(sdc.get());
      connectToSubdeviceUpdates(sdc.get());
      delayedTextEditUpdate();
    }
  });

  connect(m_connection, &DeviceConnection::subDeviceConnected, m_connectionContext,
  [this](const DeviceId& /* deviceId */, const QString& path)
  {
    const auto it = m_subDevices.find(path);
    if (it == m_subDevices.cend()) {
      return;
    }

    if (it->second.isHidpp) {
      m_hidppInfo.clear();
    }

    if (it->second.hasBatteryInfo) {
      m_batteryInfo.clear();
      m_batteryInfoTimer->stop();
    }

    m_subDevices.erase(it);
    delayedTextEditUpdate();
  });

  initSubdeviceInfo();
  updateTextEdit();
}

// -------------------------------------------------------------------------------------------------
void DeviceInfoWidget::connectToBatteryUpdates(SubHidppConnection* hdc)
{
  if (hdc->hasFlags(DeviceFlag::ReportBattery))
  {
    connect(hdc, &SubHidppConnection::batteryInfoChanged, m_connectionContext, [this, hdc]() {
      updateBatteryInfo(hdc);
      m_batteryInfoTimer->start();
      delayedTextEditUpdate();
    });

    connect(m_batteryInfoTimer, &QTimer::timeout, m_connectionContext, [hdc]() {
      hdc->triggerBattyerInfoUpdate();
    });
  }
}

// -------------------------------------------------------------------------------------------------
void DeviceInfoWidget::connectToSubdeviceUpdates(SubDeviceConnection* sdc)
{
  connect(sdc, &SubDeviceConnection::flagsChanged, m_connectionContext, [this, sdc]()
  {
    if (!m_subDevices[sdc->path()].hasBatteryInfo
        && sdc->hasFlags(DeviceFlag::ReportBattery))
    {
      if (const auto hdc = qobject_cast<SubHidppConnection*>(sdc)) {
        connectToBatteryUpdates(hdc);
        hdc->triggerBattyerInfoUpdate();
      }
    }

    updateSubdeviceInfo(sdc);
    if (const auto hdc = qobject_cast<SubHidppConnection*>(sdc)) {
      updateHidppInfo(hdc);
      delayedTextEditUpdate();
    }
  });

  // HID++ device only updates
  if (const auto hdc = qobject_cast<SubHidppConnection*>(sdc))
  {
      connectToBatteryUpdates(hdc);

      if (hdc->busType() == BusType::Usb)
      {
        connect(hdc, &SubHidppConnection::receiverStateChanged, m_connectionContext,
        [this](SubHidppConnection::ReceiverState s) {
          m_hidppInfo.receiverState = toString(s, false);
          delayedTextEditUpdate();
        });
      }

      connect(hdc, &SubHidppConnection::presenterStateChanged, m_connectionContext,
      [this, hdc](SubHidppConnection::PresenterState s) {
        m_hidppInfo.presenterState = toString(s, false);
        const auto pv = hdc->protocolVersion();
        m_hidppInfo.protocolVersion = QString("%1.%2").arg(pv.major).arg(pv.minor);
        delayedTextEditUpdate();
      });
  }
}

// -------------------------------------------------------------------------------------------------
void DeviceInfoWidget::updateTextEdit()
{
  m_textEdit->clear();

  QTextCharFormat normalFormat;
  normalFormat.setFontUnderline(false);
  QTextCharFormat underlineFormat;
  underlineFormat.setFontUnderline(true);
  QTextCharFormat italicFormat;
  italicFormat.setFontItalic(true);

  auto cursor = m_textEdit->textCursor();

  { // Insert table with basic device information
    QTextTableFormat tableFormat;
    tableFormat.setBorder(1);
    tableFormat.setCellSpacing(0);
    tableFormat.setBorderBrush(QBrush(Qt::lightGray));
    tableFormat.setCellPadding(2);
    tableFormat.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    cursor.insertTable(m_deviceBaseInfo.size(), 2, tableFormat);

    for (const auto& info : m_deviceBaseInfo)
    {
      cursor.insertText(info.first, italicFormat);
      cursor.movePosition(QTextCursor::NextCell);
      cursor.insertText(info.second, normalFormat);
      cursor.movePosition(QTextCursor::NextCell);
    }
    cursor.movePosition(QTextCursor::End);
  }

  { // Insert list of sub devices
    cursor.insertBlock();
    cursor.insertBlock();
    cursor.insertText(tr("Sub devices:"), underlineFormat);
    cursor.insertText(" ", normalFormat);
    cursor.insertBlock();
    cursor.movePosition(QTextCursor::PreviousBlock);
    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.setBlockCharFormat(normalFormat);
    QTextListFormat listFormat;
    listFormat.setStyle(QTextListFormat::ListDisc);
    listFormat.setIndent(1);
    cursor.insertList(listFormat);

    for (const auto& subDeviceInfo : m_subDevices) {
      cursor.insertText(subDeviceInfo.first);
      cursor.insertText(": ");
      cursor.insertText(subDeviceInfo.second.info);
      if (cursor.currentList()->itemNumber(cursor.block())
          < static_cast<int>(m_subDevices.size() - 1)) {
        cursor.insertBlock();
      }
    }
    cursor.movePosition(QTextCursor::MoveOperation::NextBlock);
  }

  if (!m_batteryInfo.isEmpty()) {
    cursor.insertBlock();
    cursor.insertText(tr("Battery Info:"), underlineFormat);
    cursor.insertText(" ", normalFormat);
    cursor.insertText(m_batteryInfo);
    cursor.insertBlock();
  }

  if (!m_hidppInfo.presenterState.isEmpty())
  {
    cursor.insertBlock();
    cursor.insertText(tr("HID++ Info:"), underlineFormat);
    cursor.insertText(" ", normalFormat);
    cursor.insertBlock();
    cursor.movePosition(QTextCursor::PreviousBlock);
    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.setBlockCharFormat(normalFormat);
    QTextListFormat listFormat;
    listFormat.setStyle(QTextListFormat::ListDisc);
    listFormat.setIndent(1);
    cursor.insertList(listFormat);

    if (!m_hidppInfo.receiverState.isEmpty()) {
      cursor.insertText(tr("Receiver state:"), italicFormat);
      cursor.insertText(" ", normalFormat);
      cursor.insertText(m_hidppInfo.receiverState);
    }

    cursor.insertBlock();
    cursor.insertText(tr("Presenter state:"), italicFormat);
    cursor.insertText(" ", normalFormat);
    cursor.insertText(m_hidppInfo.presenterState);

    cursor.insertBlock();
    cursor.insertText(tr("Protocol version:"), italicFormat);
    cursor.insertText(" ", normalFormat);
    cursor.insertText(m_hidppInfo.protocolVersion);

    cursor.insertBlock();
    cursor.insertText(tr("Supported features:"), italicFormat);
    cursor.insertText(" ", normalFormat);
    cursor.insertText(m_hidppInfo.hidppFlags.join(", "));

    cursor.movePosition(QTextCursor::MoveOperation::NextBlock);
  }
}

// -------------------------------------------------------------------------------------------------
void DeviceInfoWidget::updateSubdeviceInfo(SubDeviceConnection* sdc)
{
  const auto hdc = qobject_cast<SubHidppConnection*>(sdc);
  m_subDevices[sdc->path()] = SubDeviceInfo{
    QString("[%2%3%4]").arg(
      toString(sdc->mode(), false),
      sdc->isGrabbed() ? ", Grabbed" : "",
      sdc->hasFlags(DeviceFlag::Hidpp) ? ", HID++" : ""),
    hdc != nullptr,
    (hdc != nullptr) ? hdc->hasFlags(DeviceFlag::ReportBattery) : false
  };
}

// -------------------------------------------------------------------------------------------------
void DeviceInfoWidget::initSubdeviceInfo()
{
  m_subDevices.clear();
  m_batteryInfo.clear();
  m_batteryInfoTimer->stop();
  m_hidppInfo.clear();

  for (const auto& sd : m_connection->subDevices())
  {
    const auto& sdc = sd.second;
    if (sdc->path().isEmpty()) { continue; }
    updateSubdeviceInfo(sdc.get());
    connectToSubdeviceUpdates(sdc.get());

    if (const auto hdc = qobject_cast<SubHidppConnection*>(sdc.get()))
    {
      updateHidppInfo(hdc);

      if (hdc->hasFlags(DeviceFlag::ReportBattery)) {
        updateBatteryInfo(hdc);
        hdc->triggerBattyerInfoUpdate();
      }
    }
  }
}

// -------------------------------------------------------------------------------------------------
void DeviceInfoWidget::updateHidppInfo(SubHidppConnection* hdc)
{
  m_hidppInfo.clear();

  if (hdc->busType() == BusType::Usb) {
    m_hidppInfo.receiverState = toString(hdc->receiverState(), false);
  }

  m_hidppInfo.presenterState = toString(hdc->presenterState(), false);

  const auto pv = hdc->protocolVersion();
  m_hidppInfo.protocolVersion = QString("%1.%2").arg(pv.major).arg(pv.minor);

  for (const auto flag : { DeviceFlag::Vibrate
                         , DeviceFlag::ReportBattery
                         , DeviceFlag::NextHold
                         , DeviceFlag::BackHold
                         , DeviceFlag::PointerSpeed
                         , DeviceFlag::ActionButton })
  {
    if (hdc->hasFlags(flag)) { m_hidppInfo.hidppFlags.push_back(toString(flag, false)); }
  }
}

// -------------------------------------------------------------------------------------------------
void DeviceInfoWidget::updateBatteryInfo(SubHidppConnection* hdc)
{
  const auto batteryInfo = hdc->batteryInfo();
  if (batteryInfo.status == HIDPP::BatteryStatus::Discharging)
  {
    m_batteryInfo =  QString("%1% - %2% (%3)").arg(
                QString::number(batteryInfo.currentLevel),
                QString::number(batteryInfo.nextReportedLevel),
                toString(batteryInfo.status));
  } else {
    m_batteryInfo = toString(batteryInfo.status);
  }
}
