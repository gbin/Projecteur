// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md
#pragma once

#include "device.h"
#include "hidpp.h"

#include <chrono>
#include <list>
#include <unordered_map>

class QTimer;

// -------------------------------------------------------------------------------------------------
/// Hid++ connection class
class SubHidppConnection : public SubHidrawConnection, public HidppConnectionInterface
{
  Q_OBJECT

public:
  /// Initialization state of the Usb dongle - for bluetooth this will be always initialized.
  enum class ReceiverState : uint8_t { Uninitialized, Initializing, Initialized, Error };
  /// Initialization state of the wireless presenter.
  /// * Uninitialized - no information had been collected and no defaults had been set up
  /// * Uninitialized_Offline - same as above, but online check detected offline device
  /// * Initializing  - currently fetching feature sets and setting defaults and other information
  /// * Initialized_Online  - device initialized and online
  /// * Initialized_Offline - device initialized but offline (only relevant when using usb dongle)
  /// * Error - An error occured during initialization.
  enum class PresenterState : uint8_t { Uninitialized, Uninitialized_Offline, Initializing,
                                        Initialized_Online, Initialized_Offline, Error };

  static std::shared_ptr<SubHidppConnection> create(const DeviceScan::SubDevice& sd,
                                                    const DeviceConnection& dc);

  SubHidppConnection(SubHidrawConnection::Token, const DeviceId&, const DeviceScan::SubDevice&);
  ~SubHidppConnection();

  using SubHidrawConnection::sendData;

  // --- HidppConnectionInterface implementation:

  BusType busType() const override { return m_details.deviceId.busType; }
  uint8_t deviceIndex() const override { return m_deviceIndex; }
  ssize_t sendData(std::vector<uint8_t> msg) override;
  ssize_t sendData(HIDPP::Message msg) override;
  void sendData(std::vector<uint8_t> msg, SendResultCallback resultCb) override;
  void sendData(HIDPP::Message msg, SendResultCallback resultCb) override;
  void sendDataBatch(DataBatch dataBatch, DataBatchResultCallback cb,
                     bool continueOnError = false) override;
  void sendRequest(std::vector<uint8_t> data, RequestResultCallback responseCb) override;
  void sendRequest(HIDPP::Message msg, RequestResultCallback responseCb) override;
  void sendRequestBatch(RequestBatch requestBatch, RequestBatchResultCallback cb,
                        bool continueOnError = false) override;

  void registerNotificationCallback(QObject* obj, HIDPP::Notification notification,
                                    NotificationCallback cb, uint8_t function = 0xff) override;
  void registerNotificationCallback(QObject* obj, uint8_t featureIndex,
                                    NotificationCallback cb, uint8_t function = 0xff) override;
  void unregisterNotificationCallback(QObject* obj, uint8_t featureIndex,
                                      uint8_t function = 0xff) override;
  void unregisterNotificationCallback(QObject* obj, HIDPP::Notification notification,
                                      uint8_t function = 0xff) override;

  // ---

  PresenterState presenterState() const;
  ReceiverState receiverState() const;
  const HIDPP::FeatureSet& featureSet() { return m_featureSet; }

  HIDPP::ProtocolVersion protocolVersion() const;
  void triggerBattyerInfoUpdate();
  const HIDPP::BatteryInfo& batteryInfo() const;

  void sendPing(RequestResultCallback cb);
  void sendVibrateCommand(uint8_t intensity, uint8_t length, RequestResultCallback cb);
  /// Set device pointer speed - speed needs to be in the range [0-9]
  void setPointerSpeed(uint8_t speed, RequestResultCallback cb);

signals:
  void receiverStateChanged(ReceiverState);
  void presenterStateChanged(PresenterState);
  void featureSetInitialized();

  void batteryInfoChanged(const HIDPP::BatteryInfo&);

private:
  void subDeviceInit();
  void initReceiver(std::function<void(ReceiverState)>);
  void initPresenter(std::function<void(PresenterState)>);
  void updateDeviceFlags();
  void registerForUsbNotifications();
  void registerForFeatureNotifications();
  /// Initializes features. Returns a map of initalized features and the result from it.
  void initFeatures(std::function<void(std::map<HIDPP::FeatureCode, MsgResult>&&)> cb);

  void getBatteryLevelStatus(std::function<void(MsgResult, HIDPP::BatteryInfo&&)> cb);

  void setReceiverState(ReceiverState rs);
  void setPresenterState(PresenterState ps);
  void setBatteryInfo(const HIDPP::BatteryInfo& bi);

  void onHidppDataAvailable(int fd);

  void getProtocolVersion(std::function<void(MsgResult, HIDPP::Error, HIDPP::ProtocolVersion)> cb);
  void checkPresenterOnline(std::function<void(bool, HIDPP::ProtocolVersion)> cb);
  void checkAndUpdatePresenterState(std::function<void(PresenterState)> cb);

  void clearTimedOutRequests();
  void findPresenterDeviceIndex(uint8_t candidate, std::function<void(bool)> cb);
  void sendPing(uint8_t deviceIndex, RequestResultCallback cb);

  void sendDataBatch(DataBatch dataBatch, DataBatchResultCallback cb, bool continueOnError,
                     std::vector<MsgResult> results);
  void sendRequestBatch(RequestBatch requestBatch, RequestBatchResultCallback cb,
                        bool continueOnError, std::vector<MsgResult> results);

  HIDPP::FeatureSet m_featureSet;
  HIDPP::ProtocolVersion m_protocolVersion;
  HIDPP::BatteryInfo m_batteryInfo;
  uint8_t m_deviceIndex = HIDPP::DeviceIndex::WirelessDevice1;
  bool m_deviceIndexKnown = true;

  ReceiverState m_receiverState = ReceiverState::Uninitialized;
  PresenterState m_presenterState = PresenterState::Uninitialized;

  /// A request entry for request messages sent to the device.
  struct RequestEntry {
    HIDPP::Message request;
    std::chrono::time_point<std::chrono::steady_clock> validUntil;
    RequestResultCallback callBack;
  };

  std::list<RequestEntry> m_requests;
  QTimer* m_requestCleanupTimer = nullptr;

  struct Subscriber { QObject* object = nullptr; uint8_t function; NotificationCallback cb; };
  std::unordered_map<uint8_t, std::list<Subscriber>> m_notificationSubscribers;
};

const char* toString(SubHidppConnection::ReceiverState rs, bool withClass = true);
const char* toString(SubHidppConnection::PresenterState ps, bool withClass = true);
