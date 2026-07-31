// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "device-hidpp.h"

#include "deviceinput.h"
#include "enum-helper.h"
#include "logging.h"

#include <unistd.h>

#include <QSocketNotifier>
#include <QTimer>

DECLARE_LOGGING_CATEGORY(hid)

// -------------------------------------------------------------------------------------------------
SubHidppConnection::SubHidppConnection(SubHidrawConnection::Token token,
                                       const DeviceId& id, const DeviceScan::SubDevice& sd)
  : SubHidrawConnection(token, id, sd)
  , m_featureSet(this)
  , m_requestCleanupTimer(new QTimer(this))
{
  constexpr int cleanUpTimerInterval = 500;
  m_requestCleanupTimer->setInterval(cleanUpTimerInterval);
  m_requestCleanupTimer->setSingleShot(false);
  connect(m_requestCleanupTimer, &QTimer::timeout, this, &SubHidppConnection::clearTimedOutRequests);
}

// -------------------------------------------------------------------------------------------------
SubHidppConnection::~SubHidppConnection() = default;

// -------------------------------------------------------------------------------------------------
const char* toString(SubHidppConnection::ReceiverState s, bool withClass)
{
  using ReceiverState = SubHidppConnection::ReceiverState;
  switch (s) {
    ENUM_CASE_STRINGIFY3(ReceiverState, Uninitialized, withClass);
    ENUM_CASE_STRINGIFY3(ReceiverState, Initializing, withClass);
    ENUM_CASE_STRINGIFY3(ReceiverState, Initialized, withClass);
    ENUM_CASE_STRINGIFY3(ReceiverState, Error, withClass);
  }
  return "ReceiverState::(unknown)";
}

const char* toString(SubHidppConnection::PresenterState s, bool withClass)
{
  using PresenterState = SubHidppConnection::PresenterState;
  switch (s) {
    ENUM_CASE_STRINGIFY3(PresenterState, Uninitialized, withClass);
    ENUM_CASE_STRINGIFY3(PresenterState, Uninitialized_Offline, withClass);
    ENUM_CASE_STRINGIFY3(PresenterState, Initializing, withClass);
    ENUM_CASE_STRINGIFY3(PresenterState, Initialized_Online, withClass);
    ENUM_CASE_STRINGIFY3(PresenterState, Initialized_Offline, withClass);
    ENUM_CASE_STRINGIFY3(PresenterState, Error, withClass);
  }
  return "PresenterState::(unknown)";
}

// -------------------------------------------------------------------------------------------------
ssize_t SubHidppConnection::sendData(std::vector<uint8_t> data) {
  return sendData(HIDPP::Message(std::move(data)));
}

// -------------------------------------------------------------------------------------------------
ssize_t SubHidppConnection::sendData(HIDPP::Message msg)
{
  constexpr ssize_t errorResult = -1;
  if (!msg.isValid()) {
    return errorResult;
  }

  // If the message has the device index 0xff it is meant for USB dongle.
  // We should not be send it, when the device is connected via bluetooth.
  //
  // The Logitech Spotlight (USB) can receive data in two different lengths:
  //   1. Short (7 byte long starting with 0x10)
  //   2. Long (20 byte long starting with 0x11)
  // However, the bluetooth connection only accepts data in long (20 byte) messages.

  if (busType() == BusType::Bluetooth)
  {
    if (msg.deviceIndex() == HIDPP::DeviceIndex::DefaultDevice) {
      logWarn(hid) << tr("Invalid message device index in data '%1' for device connected "
                         "via bluetooth.").arg(msg.hex());
      return errorResult;
    }

    // For bluetooth always convert to a long message if we have a short message
    msg.convertToLong();
  }

  return SubHidrawConnection::sendData(msg.data(), msg.size());
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::sendData(std::vector<uint8_t> data, SendResultCallback resultCb) {
  sendData(HIDPP::Message(std::move(data)), std::move(resultCb));
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::sendData(HIDPP::Message msg, SendResultCallback resultCb)
{
  postSelf([this, msg = std::move(msg), cb = std::move(resultCb)]() mutable {
    // Check for valid message format
    if (!msg.isValid()) {
      if (cb) { cb(MsgResult::InvalidFormat); }
      return;
    }

    if (busType() == BusType::Bluetooth) {
      // For bluetooth always convert to a long message if we have a short message
      msg.convertToLong();
    }

    const auto result = SubHidrawConnection::sendData(msg.data(), msg.size());
    if (cb) {
      const bool success = (result >= 0 && static_cast<size_t>(result) == msg.size());
      cb(success ? MsgResult::Ok : MsgResult::WriteError);
    }
  });
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::sendRequest(std::vector<uint8_t> data, RequestResultCallback responseCb) {
  sendRequest(HIDPP::Message(std::move(data)), std::move(responseCb));
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::sendRequest(HIDPP::Message msg, RequestResultCallback responseCb)
{
  postSelf([this, msg = std::move(msg), cb = std::move(responseCb)]() mutable
  {
    // Check for valid message format
    if (!msg.isValid()) {
      if (cb) { cb(MsgResult::InvalidFormat, HIDPP::Message()); }
      return;
    }

    // Device index sanity check
    const uint8_t dIdx = msg.deviceIndex();
    const bool isValidDeviceIndex = (dIdx == HIDPP::DeviceIndex::CordedDevice
      || dIdx == HIDPP::DeviceIndex::DefaultDevice
      || (dIdx >= HIDPP::DeviceIndex::WirelessDevice1 && dIdx <= HIDPP::DeviceIndex::WirelessDevice6));

    if (!isValidDeviceIndex)
    {
      logWarn(hid) << tr("Invalid device index (%1) in message for '%2'")
                      .arg(msg.deviceIndex()).arg(path());
      if (cb) { cb(MsgResult::InvalidFormat, HIDPP::Message()); }
      return;
    }

    if (busType() == BusType::Bluetooth) {
      // For bluetooth always convert to a long message if we have a short message
      msg.convertToLong();
    }

    sendData(msg, makeSafeCallback([this, msg](MsgResult result)
    {
      // If data was sent successfully the request will be handled when the reply arrives or
      // the request times out -> return
      if (result == MsgResult::Ok) { return; }

      // error result, find our message in the request list
      auto it = std::find_if(m_requests.begin(), m_requests.end(),
                             [&msg](const RequestEntry& entry) { return entry.request == msg; });

      if (it == m_requests.end()) {
        logDebug(hid) << "Send request write error without matching request queue entry.";
        return;
      }

      if (it->callBack) { it->callBack(result, HIDPP::Message()); }
      m_requests.erase(it);
    }));

    constexpr uint64_t hidppMsgTimeoutMs = 4000;

    // Place request in request list with a timeout
    m_requests.emplace_back(RequestEntry{
      std::move(msg), std::chrono::steady_clock::now() + std::chrono::milliseconds{hidppMsgTimeoutMs},
      std::move(cb)});

    // Run cleanup timer if not already active
    if (!m_requestCleanupTimer->isActive()) { m_requestCleanupTimer->start(); }
  });
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::sendDataBatch(DataBatch dataBatch, DataBatchResultCallback cb,
                                       bool continueOnError) {
  std::vector<MsgResult> results;
  results.reserve(dataBatch.size());
  sendDataBatch(std::move(dataBatch), std::move(cb), continueOnError, std::move(results));
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::sendDataBatch(DataBatch dataBatch, DataBatchResultCallback cb,
                                       bool continueOnError, std::vector<MsgResult> results)
{
  postSelf([this, batch = std::move(dataBatch), batchCb = std::move(cb),
            results = std::move(results), coe = continueOnError]() mutable
  {
    if (batch.empty()) {
      if (batchCb) { batchCb(std::move(results)); }
      return;
    }

    // Get item from queue and pop
    DataBatchItem queueItem(std::move(batch.front()));
    batch.pop();

    // Process queue item
    sendData(std::move(queueItem.message), makeSafeCallback(
    [this, batch = std::move(batch), results = std::move(results), coe,
     batchCb = std::move(batchCb), resultCb = std::move(queueItem.callback)]
    (MsgResult result) mutable
    {
      // Add result to results vector
      results.push_back(result);
      // If a result callback is set invoke it
      if (resultCb) { resultCb(result); }

      // If batch is empty or we got an error result and don't want to continue on
      // error (coe)
      if (batch.empty() || (result != MsgResult::Ok && !coe)) {
        if (batchCb) { batchCb(std::move(results)); }
        return;
      }

      // continue processing the rest of the batch
      sendDataBatch(std::move(batch), std::move(batchCb), coe, std::move(results));
    }));
  });
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::sendRequestBatch(RequestBatch requestBatch, RequestBatchResultCallback cb,
                                          bool continueOnError) {
  std::vector<MsgResult> results;
  results.reserve(requestBatch.size());
  sendRequestBatch(std::move(requestBatch), std::move(cb), continueOnError, std::move(results));
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::sendRequestBatch(RequestBatch requestBatch, RequestBatchResultCallback cb,
                                          bool continueOnError, std::vector<MsgResult> results)
{
  postSelf([this, batch = std::move(requestBatch), batchCb = std::move(cb),
            results = std::move(results), coe = continueOnError]() mutable
  {
    if (batch.empty()) {
      if (batchCb) { batchCb(std::move(results)); }
      return;
    }

    // Get item from queue and pop
    RequestBatchItem queueItem(std::move(batch.front()));
    batch.pop();

    // Process queue item
    sendRequest(std::move(queueItem.message), makeSafeCallback(
    [this, batch = std::move(batch), results = std::move(results), coe,
     batchCb = std::move(batchCb), resultCb = std::move(queueItem.callback)]
    (MsgResult result, HIDPP::Message&& replyMessage) mutable
    {
      // Add result to results vector
      results.push_back(result);
      // If a result callback is set invoke it
      if (resultCb) { resultCb(result, std::move(replyMessage)); }

      // If batch is empty or we got an error result and don't want to continue on
      // error (coe)
      if (batch.empty() || (result != MsgResult::Ok && !coe)) {
        if (batchCb) { batchCb(std::move(results)); }
        return;
      }

      // continue processing the rest of the batch
      sendRequestBatch(std::move(batch), std::move(batchCb), coe, std::move(results));
    }, true));
  });
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::registerNotificationCallback(QObject* obj, uint8_t featureIndex,
                                                      NotificationCallback cb, uint8_t function)
{
  if (obj == nullptr || !cb) { return; }

  postSelf([this, obj, featureIndex, function, cb=std::move(cb)]() mutable
  {
    auto& callbackList = m_notificationSubscribers[featureIndex];
    callbackList.emplace_back(Subscriber{obj, function, std::move(cb)});

    if (obj != this)
    {
      connect(obj, &QObject::destroyed, this, [this, obj, featureIndex, function]()
      {
        auto& callbackList = m_notificationSubscribers[featureIndex];
        callbackList.remove_if([obj, function](const Subscriber& item){
          return (item.object == obj && item.function == function);
        });
      });
    }
  });
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::registerNotificationCallback(QObject* obj, HIDPP::Notification n,
                                                      NotificationCallback cb, uint8_t function)
{
  registerNotificationCallback(obj, to_integral(n), std::move(cb), function);
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::unregisterNotificationCallback(QObject* obj,
                                                        uint8_t featureIndex,
                                                        uint8_t function)
{
  postSelf([this, obj, featureIndex, function](){
    auto& callbackList = m_notificationSubscribers[featureIndex];
    callbackList.remove_if([obj, function](const Subscriber& item){
      if (item.object == obj) {
        if (function > 15 || item.function == function) { return true; }
      }
      return false;
    });
  });
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::unregisterNotificationCallback(QObject* obj,
                                                        HIDPP::Notification n,
                                                        uint8_t function)
{
  unregisterNotificationCallback(obj, to_integral(n), function);
}

// -------------------------------------------------------------------------------------------------
std::shared_ptr<SubHidppConnection> SubHidppConnection::create(const DeviceScan::SubDevice& sd,
                                                               const DeviceConnection& dc) {
  const int devfd = openHidrawSubDevice(sd, dc.deviceId());
  if (devfd == -1) { return std::shared_ptr<SubHidppConnection>(); }

  auto connection = std::make_shared<SubHidppConnection>(Token{}, dc.deviceId(), sd);
  if (dc.hasHidppSupport()) { connection->m_details.deviceFlags |= DeviceFlag::Hidpp; }

  connection->createSocketNotifiers(devfd, sd.deviceFile);
  connection->m_inputMapper = dc.inputMapper();

  connect(connection->socketReadNotifier(), &QSocketNotifier::activated, &*connection,
          &SubHidppConnection::onHidppDataAvailable);

  connection->postTask([c = &*connection]() { c->subDeviceInit(); });
  return connection;
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::sendVibrateCommand(uint8_t intensity, uint8_t length,
                                            RequestResultCallback cb)
{
  const uint8_t pcIndex = m_featureSet.featureIndex(HIDPP::FeatureCode::PresenterControl);

  if (pcIndex == 0)
  {
    if (cb) { cb(MsgResult::FeatureNotSupported, HIDPP::Message()); }
    return;
  }

  // Logitech Spotlight:
  //                                        present
  //                                        controlID   len         intensity
  // unsigned char vibrate[] = {0x10, 0x01, 0x09, 0x1d, 0x00, 0xe8, 0x80};

  length = length > 10 ? 10 : length; // length should be between 0 to 10.

  using namespace HIDPP;

  Message vibrateMsg(Message::Type::Long, m_deviceIndex, pcIndex, 1, {
    length, 0xe8, intensity
  });

  sendRequest(std::move(vibrateMsg), std::move(cb));
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::getBatteryLevelStatus(
  std::function<void(MsgResult, HIDPP::BatteryInfo&&)> cb)
{
  using namespace HIDPP;

  const auto batteryIndex = m_featureSet.featureIndex(FeatureCode::BatteryStatus);
  if (batteryIndex == 0)
  {
    if (cb) { cb(MsgResult::FeatureNotSupported, {}); }
    return;
  }

  Message batteryReqMsg(Message::Type::Short, m_deviceIndex, batteryIndex, 0);
  sendRequest(std::move(batteryReqMsg), [cb=std::move(cb)](MsgResult res, Message&& msg) mutable
  {
    if (!cb) { return; }

    auto batteryInfo = (res != MsgResult::Ok) ? BatteryInfo{}
                                              : BatteryInfo{msg[4],
                                                            msg[5],
                                                            to_enum<BatteryStatus>(msg[6])};
    cb(res, std::move(batteryInfo));
  });
}

// -------------------------------------------------------------------------------------------------
HIDPP::BatteryStatus SubHidppConnection::toBatteryStatus1004(uint8_t chargingStatus)
{
  using BatteryStatus = HIDPP::BatteryStatus;
  switch (chargingStatus) {
    case 0: return BatteryStatus::Discharging;
    case 1: return BatteryStatus::Charging;
    case 2: return BatteryStatus::SlowCharging;
    case 3: return BatteryStatus::Full;
    case 4: return BatteryStatus::ChargingError;
    default: return BatteryStatus::InvalidBattery;
  }
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::getUnifiedBatteryLevel(
  std::function<void(MsgResult, HIDPP::BatteryInfo&&)> cb)
{
  using namespace HIDPP;

  const auto ubIndex = m_featureSet.featureIndex(FeatureCode::UnifiedBattery);
  if (ubIndex == 0)
  {
    if (cb) { cb(MsgResult::FeatureNotSupported, {}); }
    return;
  }

  // UnifiedBattery (0x1004) get status command (function 0x01) response:
  //   msg[4] = state of charge in percent
  //   msg[5] = battery level flags (critical/low/good/full)
  //   msg[6] = charging status
  //   msg[7] = external power source indicator
  Message batteryReqMsg(Message::Type::Long, m_deviceIndex, ubIndex, 0x01, Message::Data{0,0,0,0});
  sendRequest(std::move(batteryReqMsg), [cb=std::move(cb)](MsgResult res, Message&& msg) mutable
  {
    if (!cb) { return; }

    BatteryInfo batteryInfo;
    if (res == MsgResult::Ok) {
      batteryInfo = BatteryInfo{msg[4], 0, toBatteryStatus1004(msg[6])};
    }
    cb(res, std::move(batteryInfo));
  });
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::setPointerSpeed(uint8_t speed,
                                         std::function<void(MsgResult, HIDPP::Message&&)> cb)
{
  const uint8_t psIndex = m_featureSet.featureIndex(HIDPP::FeatureCode::PointerSpeed);
  if (psIndex == 0x00)
  {
    if (cb) { cb(MsgResult::FeatureNotSupported, HIDPP::Message()); }
    return;
  }

  speed = (speed > 0x09) ? 0x09 : speed; // speed should be in range of 0-9
  // Pointer speed sent to the device with values 0x10 - 0x19
  const uint8_t pointerSpeed = 0x10 & speed;

  sendRequest(
    HIDPP::Message(HIDPP::Message::Type::Long, m_deviceIndex,
                   psIndex, 1, HIDPP::Message::Data{pointerSpeed}),
    std::move(cb)
  );
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::setReceiverState(ReceiverState rs)
{
  if (rs == m_receiverState) { return; }

  logDebug(hid) << tr("Receiver state (%1) changes from %3 to %4")
                   .arg(path()).arg(toString(m_receiverState), toString(rs));
  m_receiverState = rs;
  emit receiverStateChanged(m_receiverState);
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::setPresenterState(PresenterState ps)
{
  if (ps == m_presenterState) { return; }

  logDebug(hid) << tr("Presenter state (%1) changes from %2 to %3")
                   .arg(path()).arg(toString(m_presenterState), toString(ps));
  m_presenterState = ps;
  emit presenterStateChanged(m_presenterState);
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::setBatteryInfo(const HIDPP::BatteryInfo& bi)
{
  if (m_batteryInfo == bi) { return; }

  m_batteryInfo = bi;
  emit batteryInfoChanged(m_batteryInfo);
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::initReceiver(std::function<void(ReceiverState)> cb)
{
  postSelf([this, cb=std::move(cb)]() mutable {
    if (m_receiverState == ReceiverState::Initializing
        || m_receiverState == ReceiverState::Initialized)
    {
      logDebug(hid) << "Cannot init receiver when initializing or already initialized.";
      if (cb) { cb(m_receiverState); }
      return;
    }

    setReceiverState(ReceiverState::Initializing);

    if (busType() != BusType::Usb)
    {
      // If bus type is not USB return immediately with success result and initialized state
      setReceiverState(ReceiverState::Initialized);
      if (cb) { cb(m_receiverState); }
      return;
    }

    using namespace HIDPP;
    using Type = HIDPP::Message::Type;

    int index = -1;

    RequestBatch batch{{
      RequestBatchItem{
        // Reset device: get rid of any device configuration by other programs
        Message(Type::Short, DeviceIndex::DefaultDevice, Commands::GetRegister, 0, 0, {}),
        [index=++index](MsgResult result, HIDPP::Message&& /* msg */) {
          if (result == MsgResult::Ok) { return; }
          logWarn(hid) << tr("Usb receiver init error; step %1: %2")
            .arg(index).arg(toString(result));
        }
      },
      RequestBatchItem{
        // Turn off software bit and keep the wireless notification bit on
        Message(Type::Short, DeviceIndex::DefaultDevice, Commands::SetRegister, 0, 0,
                {0x00, 0x01, 0x00}),
        [index=++index](MsgResult result, HIDPP::Message&& /* msg */) {
          if (result == MsgResult::Ok) { return; }
          logWarn(hid) << tr("Usb receiver init error; step %1: %2")
            .arg(index).arg(toString(result));
        }
      },
      RequestBatchItem{
        // Initialize USB dongle
        Message(Type::Short, DeviceIndex::DefaultDevice, Commands::GetRegister, 0, 2, {}),
        [index=++index](MsgResult result, HIDPP::Message&& /* msg */) {
          if (result == MsgResult::Ok) { return; }
          logWarn(hid) << tr("Usb receiver init error; step %1: %2")
            .arg(index).arg(toString(result));
        }
      },
      RequestBatchItem{
        // ---
        Message(Type::Short, DeviceIndex::DefaultDevice, Commands::SetRegister, 0, 2,
                {0x02, 0x00, 0x00}),
        [index=++index](MsgResult result, HIDPP::Message&& /* msg */) {
          if (result == MsgResult::Ok) { return; }
          logWarn(hid) << tr("Usb receiver init error; step %1: %2")
            .arg(index).arg(toString(result));
        }
      },
      RequestBatchItem{
        // Now enable both software and wireless notification bit
        Message(Type::Short, DeviceIndex::DefaultDevice, Commands::SetRegister, 0, 0,
                {0x00, 0x09, 0x00}),
        [index=++index](MsgResult result, HIDPP::Message&& /* msg */) {
          if (result == MsgResult::Ok) { return; }
          logWarn(hid) << tr("Usb receiver init error; step %1: %2")
            .arg(index).arg(toString(result));
        }
      },
    }};

    sendRequestBatch(std::move(batch),
    makeSafeCallback([this, cb=std::move(cb)](std::vector<MsgResult>&& results)
    {
      setReceiverState(results.back() == MsgResult::Ok ? ReceiverState::Initialized
                                                       : ReceiverState::Error);
      if (cb) { cb(m_receiverState); }
    }, false));
  });
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::initPresenter(std::function<void(PresenterState)> cb)
{
  postSelf([this, cb=std::move(cb)]() mutable {
    if (m_presenterState == PresenterState::Initializing
        || m_presenterState == PresenterState::Initialized_Offline
        || m_presenterState == PresenterState::Initialized_Online)
    {
      logDebug(hid) << "Cannot init presenter when offline, initializing or already initialized.";
      if (cb) { cb(m_presenterState); }
      return;
    }

    setPresenterState(PresenterState::Initializing);

    m_featureSet.initFromDevice(deviceId(), makeSafeCallback(
    [this, cb=std::move(cb)](HIDPP::FeatureSet::State state) mutable
    {
      using FState = HIDPP::FeatureSet::State;
      switch (state)
      {
        case FState::Error: {
          setPresenterState(PresenterState::Error);
          break;
        }
        case FState::Uninitialized:
        case FState::Initializing: {
          logError(hid) << tr("Unexpected state from feature set.");
          setPresenterState(PresenterState::Error);
          break;
        }
        case FState::Initialized:
        {
          logDebug(hid) << tr("Received %1 supported features from device. (%2)")
                           .arg(m_featureSet.featureCount()).arg(path());

          registerForFeatureNotifications();
          updateDeviceFlags();
          initFeatures(makeSafeCallback(
          [this, cb=std::move(cb)](std::map<HIDPP::FeatureCode, MsgResult>&& resultMap)
          {
            if (!resultMap.empty()) {
              for (const auto& res : resultMap) {
                logDebug(hid) << tr("InitFeature result %1 => %2").arg(toString(res.first)).arg(toString(res.second));
              }
            }
            emit featureSetInitialized();
            setPresenterState(PresenterState::Initialized_Online);
            if (cb) { cb(m_presenterState); }
          }));
          return;
        }
      }
      if (cb) { cb(m_presenterState); }
    }));
  });
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::initFeatures(
  std::function<void(std::map<HIDPP::FeatureCode, MsgResult>&&)> cb)
{
  using namespace HIDPP;
  using ResultMap = std::map<HIDPP::FeatureCode, MsgResult>;

  RequestBatch batch;
  auto resultMap = std::make_shared<ResultMap>();

  // Reset spotlight device, if supported
  if (const auto resetFeatureIndex = m_featureSet.featureIndex(FeatureCode::Reset))
  {
    batch.emplace(RequestBatchItem {
      Message(Message::Type::Long, m_deviceIndex, resetFeatureIndex, 1),
      [resultMap](MsgResult res, Message&& /* msg */) {
        resultMap->emplace(FeatureCode::Reset, res);
      }
    });
  }

  // Enable Next and back button on hold functionality.
  if (const auto contrFeatureIndex = m_featureSet.featureIndex(FeatureCode::ReprogramControlsV4))
  {
    if (hasFlags(DeviceFlags::NextHold))
    {
      batch.emplace(RequestBatchItem {
        Message(Message::Type::Long, m_deviceIndex, contrFeatureIndex, 3,
                Message::Data{0x00, 0xda, 0x33}),
        [resultMap](MsgResult res, Message&& /* msg */) {
          resultMap->emplace(FeatureCode::ReprogramControlsV4, res);
        }
      });
    }

    if (hasFlags(DeviceFlags::BackHold))
    {
      batch.emplace(RequestBatchItem {
        Message(Message::Type::Long, m_deviceIndex, contrFeatureIndex, 3,
                Message::Data{0x00, 0xdc, 0x33}),
        [resultMap](MsgResult res, Message&& /* msg */) {
          resultMap->emplace(FeatureCode::ReprogramControlsV4, res);
        }
      });
    }

    // Enable 'Action Button' on hold/tap functionality for the Logitech Spotlight 2.
    // The device reports the action button as control id 0x00fb. If the device
    // does not support this control, the request will fail and the ActionButton
    // device flag stays unset.
    batch.emplace(RequestBatchItem {
      Message(Message::Type::Long, m_deviceIndex, contrFeatureIndex, 3,
              Message::Data{0x00, 0xfb, 0x03}),
      [resultMap, this](MsgResult res, Message&& /* msg */) {
        if (res == MsgResult::Ok) {
          setFlags(DeviceFlags::ActionButton, true);
          logDebug(hid) << tr("Subdevice '%1' reported 'Action Button' control support.")
                           .arg(path());
        } else {
          setFlags(DeviceFlags::ActionButton, false);
        }
        resultMap->emplace(FeatureCode::ReprogramControlsV4, res);
      }
    });
  }

  if (const auto psFeatureIndex = m_featureSet.featureIndex(FeatureCode::PointerSpeed))
  {
    // Reset pointer speed to 0x14 - the device accepts values from 0x10 to 0x19
    batch.emplace(RequestBatchItem {
      HIDPP::Message(HIDPP::Message::Type::Long, m_deviceIndex,
                     psFeatureIndex, 1, HIDPP::Message::Data{0x14}),
      [resultMap](MsgResult res, Message&& /* msg */) {
        resultMap->emplace(FeatureCode::PointerSpeed, res);
      }
    });
  }

  sendRequestBatch(std::move(batch),
  [resultMap=std::move(resultMap), cb=std::move(cb)](std::vector<MsgResult>&& /* msg */) mutable {
    if (cb) { cb(std::move(*resultMap)); }
  });
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::updateDeviceFlags()
{
  DeviceFlags featureFlagsSet = DeviceFlag::NoFlags;
  DeviceFlags featureFlagsUnset = DeviceFlag::NoFlags;

  if (m_featureSet.featureCodeSupported(HIDPP::FeatureCode::PresenterControl)) {
    featureFlagsSet |= DeviceFlag::Vibrate;
    logDebug(hid) << tr("Subdevice '%1' reported %2 support.")
                     .arg(path()).arg(toString(HIDPP::FeatureCode::PresenterControl));
  } else {
    featureFlagsUnset |= DeviceFlag::Vibrate;
  }

  if (m_featureSet.featureCodeSupported(HIDPP::FeatureCode::BatteryStatus)) {
    featureFlagsSet |= DeviceFlag::ReportBattery;
    logDebug(hid) << tr("Subdevice '%1' reported %2 support.")
                     .arg(path()).arg(toString(HIDPP::FeatureCode::BatteryStatus));
  }
  else if (m_featureSet.featureCodeSupported(HIDPP::FeatureCode::UnifiedBattery)) {
    featureFlagsSet |= DeviceFlag::ReportBattery;
    logDebug(hid) << tr("Subdevice '%1' reported %2 support.")
                     .arg(path()).arg(toString(HIDPP::FeatureCode::UnifiedBattery));
  }
  else {
    featureFlagsUnset |= DeviceFlag::ReportBattery;
  }

  InputMapper::SpecialMoveInputs specialMoveInputs;
  if (m_featureSet.featureCodeSupported(HIDPP::FeatureCode::ReprogramControlsV4)) {
    featureFlagsSet |= DeviceFlags::NextHold;
    featureFlagsSet |= DeviceFlags::BackHold;
    specialMoveInputs.emplace_back(SpecialKeys::eventSequenceInfo(SpecialKeys::Key::NextHoldMove));
    specialMoveInputs.emplace_back(SpecialKeys::eventSequenceInfo(SpecialKeys::Key::BackHoldMove));
    logDebug(hid) << tr("Subdevice '%1' reported %2 support.")
                     .arg(path()).arg(toString(HIDPP::FeatureCode::ReprogramControlsV4));
  }
  else {
    featureFlagsUnset |= DeviceFlags::NextHold;
    featureFlagsUnset |= DeviceFlags::BackHold;
    featureFlagsUnset |= DeviceFlags::ActionButton;
  }
  m_inputMapper->setSpecialMoveInputs(std::move(specialMoveInputs));

  if (m_featureSet.featureCodeSupported(HIDPP::FeatureCode::PointerSpeed)) {
    featureFlagsSet |= DeviceFlags::PointerSpeed;
    logDebug(hid) << tr("Subdevice '%1' reported %2 support.")
                     .arg(path()).arg(toString(HIDPP::FeatureCode::PointerSpeed));
  }
  else {
    featureFlagsUnset |= DeviceFlags::BackHold;
  }

  setFlags(featureFlagsUnset, false);
  setFlags(featureFlagsSet, true);
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::registerForFeatureNotifications()
{
  using namespace HIDPP;

  // Logitech button next and back press and hold + movement
  if (const auto rcIndex = m_featureSet.featureIndex(FeatureCode::ReprogramControlsV4))
  {
    registerNotificationCallback(this, rcIndex, makeSafeCallback([](Message&& msg)
    {
      // Logitech Spotlight:
      //   * Next Button = 0xda
      //   * Back Button = 0xdc
      // Logitech Spotlight 2:
      //   * Next Button = 0xd9 (short press), 0xda (hold)
      //   * Back Button = 0xdb (short press), 0xdc (hold)
      //   * Action Button = 0xfb (short press), 0xfc (hold)
      // Byte 5 and 7 indicate pressed buttons
      // Back and next can be pressed at the same time

      constexpr uint8_t ButtonNext = 0xda;
      constexpr uint8_t ButtonBack = 0xdc;
      constexpr uint8_t ButtonAction = 0xfb;
      const auto isNextPressed = msg[5] == ButtonNext || msg[7] == ButtonNext;
      const auto isBackPressed = msg[5] == ButtonBack || msg[7] == ButtonBack;
      const auto isActionPressed = msg[5] == ButtonAction || msg[7] == ButtonAction;
      logDebug(hid) << tr("Buttons pressed: Next = %1, Back = %2, Action = %3")
                       .arg(isNextPressed).arg(isBackPressed).arg(isActionPressed);

    }), 0 /* function 0 */);

    // Handling of move events by button hold is done in spotlight.cc
    // The following commented out code is kept as example

    // registerNotificationCallback(this, rcIndex, makeSafeCallback([this](Message&& msg) {
    //   byte 4 : -1 for left movement, 0 for right movement
    //   byte 5 : horizontal movement speed -128 to 127
    //   byte 6 : -1 for up movement, 0 for down movement
    //   byte 7 : vertical movement speed -128 to 127
    // }), 1 /* function 1 */);
  }

  if (const auto batIndex = m_featureSet.featureIndex(FeatureCode::BatteryStatus))
  {
    // A device can send a battery status spontaneously to the software.
    registerNotificationCallback(this, batIndex, makeSafeCallback([this](Message&& msg) {
      setBatteryInfo(BatteryInfo{msg[4], msg[5], to_enum<BatteryStatus>(msg[6])});
    }), 0 /* function 0 */);  }

  if (const auto ubIndex = m_featureSet.featureIndex(FeatureCode::UnifiedBattery))
  {
    // A device with a unified battery can send a battery status event spontaneously.
    registerNotificationCallback(this, ubIndex, makeSafeCallback([this](Message&& msg) {
      setBatteryInfo(BatteryInfo{msg[4], 0, toBatteryStatus1004(msg[6])});
    }), 0 /* event 0 */);
  }
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::registerForUsbNotifications()
{
  // Register for device connection notifications from the usb receiver
  registerNotificationCallback(this, HIDPP::Notification::DeviceConnection, makeSafeCallback(
  [this](HIDPP::Message&& msg)
  {
    const bool linkEstablished = !static_cast<bool>(msg[4] & (1<<6));
    logDebug(hid) << tr("%1, link established = %2")
      .arg(toString(HIDPP::Notification::DeviceConnection)).arg(linkEstablished);

    if (!linkEstablished) {
      if (m_presenterState == PresenterState::Initialized_Online) {
        setPresenterState(PresenterState::Initialized_Offline);
      }
      logInfo(hid) << tr("HID++ device '%1' went offline.").arg(path());
      return;
    }

    if (m_presenterState == PresenterState::Uninitialized_Offline
        || m_presenterState == PresenterState::Initialized_Offline
        || m_presenterState == PresenterState::Uninitialized
        || m_presenterState == PresenterState::Error)
    {
      logInfo(hid) << tr("HID++ device '%1' came online.").arg(path());
      checkAndUpdatePresenterState(makeSafeCallback([](PresenterState /* ps */) {
        //...
      }));
    }
  }));
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::subDeviceInit()
{
  if (!hasFlags(DeviceFlag::Hidpp)) { return; }

  registerForUsbNotifications();

  // Init receiver - will return almost immediately for bluetooth connections
  initReceiver(makeSafeCallback([this](ReceiverState rs)
  {
    Q_UNUSED(rs);
    // Independent of the receiver init result, try to initialize the
    // presenter device HID++ features and more
    checkAndUpdatePresenterState(makeSafeCallback([](PresenterState /* ps */) {
      //...
    }));
  }));
}

// -------------------------------------------------------------------------------------------------
SubHidppConnection::ReceiverState SubHidppConnection::receiverState() const {
  return m_receiverState;
}

// -------------------------------------------------------------------------------------------------
SubHidppConnection::PresenterState SubHidppConnection::presenterState() const {
  return m_presenterState;
}

// -------------------------------------------------------------------------------------------------
HIDPP::ProtocolVersion SubHidppConnection::protocolVersion() const {
  return m_protocolVersion;
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::triggerBattyerInfoUpdate()
{
  using namespace HIDPP;

  if (m_featureSet.featureCodeSupported(FeatureCode::UnifiedBattery)) {
    getUnifiedBatteryLevel(makeSafeCallback([this](MsgResult res, BatteryInfo&& bi)
    {
      if (res != MsgResult::Ok) {
        return;
      }
      setBatteryInfo(bi);
    }));
    return;
  }

  getBatteryLevelStatus(makeSafeCallback([this](MsgResult res, BatteryInfo&& bi)
  {
    if (res != MsgResult::Ok) {
      return;
    }

    setBatteryInfo(bi);
  }));
}

// -------------------------------------------------------------------------------------------------
const HIDPP::BatteryInfo& SubHidppConnection::batteryInfo() const {
  return m_batteryInfo;
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::sendPing(RequestResultCallback cb)
{
  using namespace HIDPP;
  // Ping wireless device - same as requesting protocol version
  Message pingMsg(Message::Type::Short, m_deviceIndex, 0, 1, getRandomPingPayload());
  sendRequest(std::move(pingMsg), std::move(cb));
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::getProtocolVersion(std::function<void(MsgResult, HIDPP::Error,
                                            HIDPP::ProtocolVersion)> cb)
{
  sendPing([cb=std::move(cb)](MsgResult res, HIDPP::Message msg) {
    if (cb) {
      auto pv = (res == MsgResult::Ok) ? HIDPP::ProtocolVersion{ msg[4], msg[5] }
                                       : HIDPP::ProtocolVersion();
      logDebug(hid) << tr("getProtocolVersion() => %1, version = %2.%3")
                       .arg(toString(res)).arg(pv.major).arg(pv.minor);
      cb(res, (res == MsgResult::HidppError) ? msg.errorCode()
                                             : HIDPP::Error::NoError, pv);
    }
  });
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::discoverDeviceIndex(std::function<void(bool, HIDPP::ProtocolVersion)> cb)
{
  using namespace HIDPP;

  postSelf([this, cb=std::move(cb)]() mutable
  {
    // For bluetooth connections the device is always the default wireless device.
    if (busType() != BusType::Usb)
    {
      m_deviceIndex = DeviceIndex::WirelessDevice1;
      getProtocolVersion(makeSafeCallback(
      [cb=std::move(cb)](MsgResult res, HIDPP::Error err, HIDPP::ProtocolVersion pv) mutable
      {
        if (cb) {
          cb(MsgResult::Ok == res && err == HIDPP::Error::NoError, std::move(pv));
        }
      }));
      return;
    }

    // Probe all wireless device slots of the usb receiver with a ping request.
    // Only the slot that has an online device will answer with an error free reply.
    RequestBatch batch;
    auto responses = std::make_shared<std::vector<std::pair<uint8_t, ProtocolVersion>>>();
    for (uint8_t idx = DeviceIndex::WirelessDevice1; idx <= DeviceIndex::WirelessDevice6; ++idx)
    {
      batch.emplace(RequestBatchItem{
        Message(Message::Type::Short, idx, 0, 1, getRandomPingPayload()),
        [responses, idx](MsgResult res, Message&& msg)
        {
          if (res == MsgResult::Ok)
          {
            responses->emplace_back(idx, ProtocolVersion{msg[4], msg[5]});
          }
        }
      });
    }

    sendRequestBatch(std::move(batch), makeSafeCallback(
    [this, responses, cb=std::move(cb)](std::vector<MsgResult>&& results) mutable
    {
      Q_UNUSED(results);
      if (!responses->empty())
      {
        const auto deviceIndex = responses->front().first;
        const auto pv = responses->front().second;
        m_deviceIndex = deviceIndex;
        logInfo(hid) << tr("Found HID++ wireless device at device index %1 (%2).")
                        .arg(m_deviceIndex).arg(path());
        if (cb) { cb(true, pv); }
        return;
      }
      if (cb) { cb(false, ProtocolVersion{}); }
    }), true /* continueOnError */);
  });
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::checkPresenterOnline(std::function<void(bool, HIDPP::ProtocolVersion)> cb)
{
  discoverDeviceIndex(makeSafeCallback(
  [cb=std::move(cb)](bool deviceOnline, HIDPP::ProtocolVersion pv) mutable
  {
    if (cb) { cb(deviceOnline, std::move(pv)); }
  }));
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::checkAndUpdatePresenterState(std::function<void(PresenterState)> cb)
{
  postSelf([this, cb=std::move(cb)]() mutable
  {
    if (m_presenterState == PresenterState::Initializing)
    {
      if (cb) { cb(m_presenterState); }
      return;
    }

    checkPresenterOnline(makeSafeCallback(
    [this, cb=std::move(cb)](bool isOnline, HIDPP::ProtocolVersion pv) mutable
    {
      if (!isOnline)
      {
        switch (m_presenterState)
        {
          case PresenterState::Initialized_Online:  // [[fallthrough]];
          case PresenterState::Initialized_Offline: {
            setPresenterState(PresenterState::Initialized_Offline);
            break;
          }
          case PresenterState::Error: // [[fallthrough]];
          case PresenterState::Initializing: break;
          case PresenterState::Uninitialized_Offline: // [[fallthrough]];
          case PresenterState::Uninitialized: {
            setPresenterState(PresenterState::Uninitialized_Offline);
          }
        }
        if (cb) { cb(m_presenterState); }
        return;
      }

      // device is online, set protocol version and init device feature table if necessary.
      m_protocolVersion = pv;

      if (m_presenterState == PresenterState::Uninitialized
          || m_presenterState == PresenterState::Uninitialized_Offline
          || m_presenterState == PresenterState::Error)
      {
        if (m_protocolVersion.smallerThan(2, 0))
        {
          logWarn(hid) << tr("Hid++ version < 2.0 not supported. (%1)").arg(path());
          setPresenterState(PresenterState::Error);
          if (cb) { cb(m_presenterState); }
          return;
        }

        initPresenter(std::move(cb));
      }
      else if (m_presenterState == PresenterState::Initialized_Offline)
      {
        initFeatures(makeSafeCallback(
        [this, cb=std::move(cb)](std::map<HIDPP::FeatureCode, MsgResult>&& resultMap)
        {
          if (!resultMap.empty()) {
            for (const auto& res : resultMap) {
              logDebug(hid) << tr("InitFeature result %1 => %2").arg(toString(res.first)).arg(toString(res.second));
            }
          }
          setPresenterState(PresenterState::Initialized_Online);
          if (cb) { cb(m_presenterState); }
        }));
      }
      else if (m_presenterState == PresenterState::Initialized_Online)
      {
        setPresenterState(PresenterState::Initialized_Online);
        if (cb) { cb(m_presenterState); }
      }
    }));
  });
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::onHidppDataAvailable(int fd)
{
  // size_t{HIDPP::Message } .. to make clang-tidy happy
  HIDPP::Message msg(std::vector<uint8_t>(size_t{HIDPP::Message::LONG_MSG_SIZE}));
  const auto res = ::read(fd, msg.data(), msg.dataSize());
  if (res < 0) {
    if (errno != EAGAIN) {
      emit socketReadError(errno);
    }
    return;
  }

  if (!msg.isValid())
  {
    if (msg[0] == 0x02) {
      // just ignore regular HID reports from the Logitech Spotlight
    }
    else {
      logDebug(hid) << tr("Received invalid HID++ message '%1' from %2").arg(msg.hex(), path());
    }
    return;
  }

  if (msg.isError()) {
    // Find first matching request for the incoming error reply
    const auto it =
      std::find_if(m_requests.begin(), m_requests.end(), [&msg](const RequestEntry& requestEntry) {
        return msg.isErrorResponseTo(requestEntry.request);
      });

    if (it != m_requests.end())
    {
      logDebug(hid) << tr("Received hiddpp error with code = %1 on")
                       .arg(to_integral(msg.errorCode())) << path() << "(" << msg.hex() << ")";
      if (it->callBack) {
        it->callBack(MsgResult::HidppError, std::move(msg));
      }
      m_requests.erase(it);
    }
    else {
      logWarn(hid) << tr("Received error hidpp message '%1' "
                         "without matching request.").arg(qPrintable(msg.hex()));
    }
    return;
  }

  // Find first matching request for the incoming reply
  const auto it =
    std::find_if(m_requests.begin(), m_requests.end(), [&msg](const RequestEntry& requestEntry) {
      return msg.isResponseTo(requestEntry.request);
    });

  if (it != m_requests.end())
  {
    // Found matching request
    logDebug(hid) << tr("Received %1 bytes on").arg(msg.size()) << path()
                  << "(" << msg.hex() << ")";
    if (it->callBack) {
      it->callBack(MsgResult::Ok, std::move(msg));
    }
    m_requests.erase(it);
  }
  else if (msg.softwareId() == 0 || msg.subId() < 0x80)
  {
    // Event/Notification
    logDebug(hid) << tr("Received notification (%1) on %2").arg(msg.hex()).arg(path());

    // Notify subscribers
    const auto& callbackList = m_notificationSubscribers[msg.featureIndex()];
    for ( const auto& subscriber : callbackList) {
      if (subscriber.function > 15 || subscriber.function == msg.function()) {
        subscriber.cb(msg);
      }
    }
  }
  else
  {
    logWarn(hid) << tr("Received hidpp message "
                       "'%1' without matching request.").arg(msg.hex());
  }
}

// -------------------------------------------------------------------------------------------------
void SubHidppConnection::clearTimedOutRequests() {
  const auto now = std::chrono::steady_clock::now();
  m_requests.remove_if([&now](const RequestEntry& entry) {
    if (now <= entry.validUntil) {
      return false;
    }
    if (entry.callBack) {
      entry.callBack(MsgResult::Timeout, HIDPP::Message());
    }
    return true;
  });

  if (m_requests.empty()) {
    m_requestCleanupTimer->stop();
  }
}
