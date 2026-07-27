// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md
# pragma once

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <QAbstractListModel>
#include <QColor>
#include <QVariant>

struct DeviceId;
class InputMapConfig;
class PresetModel;
class ProjecteurConfig;
class QQmlPropertyMap;

// -------------------------------------------------------------------------------------------------
class Settings : public QObject
{
  Q_OBJECT
  Q_PROPERTY(bool showSpotShade READ showSpotShade WRITE setShowSpotShade NOTIFY showSpotShadeChanged)
  Q_PROPERTY(int spotSize READ spotSize WRITE setSpotSize NOTIFY spotSizeChanged)
  Q_PROPERTY(bool showCenterDot READ showCenterDot WRITE setShowCenterDot NOTIFY showCenterDotChanged)
  Q_PROPERTY(int dotSize READ dotSize WRITE setDotSize NOTIFY dotSizeChanged)
  Q_PROPERTY(QColor dotColor READ dotColor WRITE setDotColor NOTIFY dotColorChanged)
  Q_PROPERTY(double dotOpacity READ dotOpacity WRITE setDotOpacity NOTIFY dotOpacityChanged)
  Q_PROPERTY(QColor shadeColor READ shadeColor WRITE setShadeColor NOTIFY shadeColorChanged)
  Q_PROPERTY(double shadeOpacity READ shadeOpacity WRITE setShadeOpacity NOTIFY shadeOpacityChanged)
  Q_PROPERTY(Qt::CursorShape cursor READ cursor WRITE setCursor NOTIFY cursorChanged)
  Q_PROPERTY(QString spotShape READ spotShape WRITE setSpotShape NOTIFY spotShapeChanged)
  Q_PROPERTY(double spotRotation READ spotRotation WRITE setSpotRotation NOTIFY spotRotationChanged)
  Q_PROPERTY(QObject* shapes READ shapeSettingsRootObject CONSTANT)
  Q_PROPERTY(bool spotRotationAllowed READ spotRotationAllowed NOTIFY spotRotationAllowedChanged)
  Q_PROPERTY(bool showBorder READ showBorder WRITE setShowBorder NOTIFY showBorderChanged)
  Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor NOTIFY borderColorChanged)
  Q_PROPERTY(int borderSize READ borderSize WRITE setBorderSize NOTIFY borderSizeChanged)
  Q_PROPERTY(double borderOpacity READ borderOpacity WRITE setBorderOpacity NOTIFY borderOpacityChanged)
  Q_PROPERTY(bool zoomEnabled READ zoomEnabled WRITE setZoomEnabled NOTIFY zoomEnabledChanged)
  Q_PROPERTY(double zoomFactor READ zoomFactor WRITE setZoomFactor NOTIFY zoomFactorChanged)
  Q_PROPERTY(bool multiScreenOverlayEnabled READ multiScreenOverlayEnabled
                  WRITE setMultiScreenOverlayEnabled NOTIFY multiScreenOverlayEnabledChanged)
public:
  explicit Settings(QObject* parent = nullptr);
  explicit Settings(const QString& configFile, QObject* parent = nullptr);
  ~Settings() override;

  void setDefaults();

  bool showSpotShade() const { return m_showSpotShade; }
  void setShowSpotShade(bool show);
  int spotSize() const { return m_spotSize; }
  void setSpotSize(int size);
  bool showCenterDot() const { return m_showCenterDot; }
  void setShowCenterDot(bool show);
  int dotSize() const { return m_dotSize; }
  void setDotSize(int size);
  QColor dotColor() const { return m_dotColor; }
  void setDotColor(const QColor& color);
  double dotOpacity() const { return m_dotOpacity; }
  void setDotOpacity(double opacity);
  QColor shadeColor() const { return m_shadeColor; }
  void setShadeColor(const QColor& color);
  double shadeOpacity() const { return m_shadeOpacity; }
  void setShadeOpacity(double opacity);
  Qt::CursorShape cursor() const { return m_cursor; }
  void setCursor(Qt::CursorShape cursor);
  QString spotShape() const { return m_spotShape; }
  void setSpotShape(const QString& spotShapeQmlComponent);
  double spotRotation() const { return m_spotRotation; }
  void setSpotRotation(double rotation);
  bool spotRotationAllowed() const;
  bool showBorder() const { return m_showBorder; }
  void setShowBorder(bool show);
  void setBorderColor(const QColor& color);
  QColor borderColor() const { return m_borderColor; }
  void setBorderSize(int size);
  int borderSize() const { return m_borderSize; }
  void setBorderOpacity(double opacity);
  double borderOpacity() const { return m_borderOpacity; }
  bool zoomEnabled() const { return m_zoomEnabled; }
  void setZoomEnabled(bool enabled);
  double zoomFactor() const { return m_zoomFactor; }
  void setZoomFactor(double factor);
  bool multiScreenOverlayEnabled() const { return m_multiScreenOverlayEnabled; }
  void setMultiScreenOverlayEnabled(bool enabled);
  bool overlayDisabled() const { return m_overlayDisabled; }
  void setOverlayDisabled(bool disabled);

  template <typename T> struct SettingRange {
    const T min;
    const T max;
  };

  static const SettingRange<int>& spotSizeRange();
  static const SettingRange<int>& dotSizeRange();
  static const SettingRange<double>& dotOpacityRange();
  static const SettingRange<double>& shadeOpacityRange();
  static const SettingRange<double>& spotRotationRange();
  static const SettingRange<int>& borderSizeRange();
  static const SettingRange<double>& borderOpacityRange();
  static const SettingRange<double>& zoomFactorRange();
  static const SettingRange<int>& inputSequenceIntervalRange();

  class SpotShapeSetting {
  public:
    SpotShapeSetting(const QString& displayName, const QString& key, const QVariant& defaultValue,
                     const QVariant& minValue, const QVariant& maxValue, int decimals = 0)
      : m_displayName(displayName), m_settingsKey(key), m_minValue(minValue),
        m_maxValue(maxValue), m_defaultValue(defaultValue), m_decimals(decimals) {}
    const QString& displayName() const { return m_displayName; }
    const QString& settingsKey() const { return m_settingsKey; }
    const QVariant& minValue() const { return m_minValue; }
    const QVariant& maxValue() const { return m_maxValue; }
    const QVariant& defaultValue() const { return m_defaultValue; }
    int decimals() const { return m_decimals; }
  private:
    QString m_displayName;
    QString m_settingsSection;
    QString m_settingsKey;
    QVariant m_minValue = 0;
    QVariant m_maxValue = 100;
    QVariant m_defaultValue = m_minValue;
    int m_decimals = 0;
  };

  class SpotShape {
  public:
    QString qmlComponent() const { return m_qmlComponent; }
    QString name() const { return m_name; }
    QString displayName() const  { return m_displayName; }
    bool allowRotation() const { return m_allowRotation; }
    const QList<SpotShapeSetting>& shapeSettings() const { return m_shapeSettings; }
  private:
    SpotShape(const QString& qmlComponent, const QString& name,
              const QString& displayName, bool allowRotation, QList<SpotShapeSetting> shapeSettings= {})
      : m_qmlComponent(qmlComponent), m_name(name), m_displayName(displayName), m_allowRotation(allowRotation),
        m_shapeSettings(std::move(shapeSettings)){}
    QString m_qmlComponent;
    QString m_name;
    QString m_displayName;
    bool m_allowRotation = true;
    QList<SpotShapeSetting> m_shapeSettings;
    friend class Settings;
  };

  static const QList<SpotShape>& spotShapes();
  QQmlPropertyMap* shapeSettings(const QString& shapeName);

  struct StringProperty
  {
    enum Type { Integer, Double, Bool, StringEnum, Color };
    static QString typeToString(Type type);

    Type type;
    QVariantList range;
    std::function<void(const QString&)> setFunction;
  };

  const std::vector<std::pair<QString, StringProperty>>& stringProperties() const;

  void savePreset(const QString& preset);
  void loadPreset(const QString& preset);
  void removePreset(const QString& preset);
  const std::vector<QString>& presets() const;
  PresetModel* presetModel();

  void setDeviceInputSeqInterval(const DeviceId& dId, int intervalMs);
  int deviceInputSeqInterval(const DeviceId& dId) const;
  void setDeviceInputMapConfig(const DeviceId& dId, const InputMapConfig& imc);
  InputMapConfig getDeviceInputMapConfig(const DeviceId& dId);
  void setDevicePresentationTimerHapticStrength(const DeviceId& dId, int strength);
  int devicePresentationTimerHapticStrength(const DeviceId& dId) const;

  void setPresentationTimerEnabled(bool enabled);
  bool presentationTimerEnabled() const;
  void setPresentationTimerDurationSeconds(int seconds);
  int presentationTimerDurationSeconds() const;

signals:
  void showSpotShadeChanged(bool show);
  void spotSizeChanged(int size);
  void dotSizeChanged(int size);
  void showCenterDotChanged(bool show);
  void dotColorChanged(const QColor& color);
  void dotOpacityChanged(double opacity);
  void shadeColorChanged(const QColor& color);
  void shadeOpacityChanged(double opcacity);
  void cursorChanged(Qt::CursorShape cursor);
  void spotShapeChanged(const QString& spotShapeQmlComponent);
  void spotRotationChanged(double rotation);
  void spotRotationAllowedChanged(bool allowed);
  void showBorderChanged(bool show);
  void borderColorChanged(const QColor& color);
  void borderSizeChanged(int size);
  void borderOpacityChanged(double opacity);
  void zoomEnabledChanged(bool enabled);
  void zoomFactorChanged(double zoomFactor);
  void multiScreenOverlayEnabledChanged(bool enabled);
  void overlayDisabledChanged(bool disabled);

  void presetLoaded(const QString& preset);

private:
  std::unique_ptr<ProjecteurConfig> m_config;

  PresetModel* m_presetModel = nullptr;
  std::map<QString, QQmlPropertyMap*> m_shapeSettings;
  QQmlPropertyMap* m_shapeSettingsRoot = nullptr;

  int m_spotSize = 30; ///< Spot size in percentage of available screen height, but at least 50 pixels.
  int m_dotSize = 5; ///< Center Dot Size (3-100 pixels)
  QColor m_dotColor;
  double m_dotOpacity = 0.8;
  QColor m_shadeColor;
  double m_shadeOpacity = 0.3;
  Qt::CursorShape m_cursor = Qt::BlankCursor;
  QString m_spotShape;
  double m_spotRotation = 0.0;
  QColor m_borderColor;
  int m_borderSize = 3;
  double m_borderOpacity = 0.8;
  bool m_zoomEnabled = false;
  double m_zoomFactor = 2.0;
  bool m_showSpotShade = true;
  bool m_showCenterDot = false;
  bool m_spotRotationAllowed = false;
  bool m_showBorder = false;
  bool m_multiScreenOverlayEnabled = false;
  bool m_overlayDisabled = false;

  std::vector<std::pair<QString, StringProperty>> m_stringPropertyMap;

private:
  void init();
  QVariant readValue(const QString& path, const QVariant& defaultValue = {}) const;
  void writeValue(const QString& path, const QVariant& value);
  bool contains(const QString& path) const;
  void remove(const QString& path);
  QString configFileName() const;
  void save();
  void sync();
  void load(const QString& preset = QString());
  QObject* shapeSettingsRootObject();
  void shapeSettingsPopulateRoot();
  void shapeSettingsInitialize();
  void shapeSettingsSetDefaults();
  void shapeSettingsLoad(const QString& preset = QString());
  void shapeSettingsSavePreset(const QString& preset);
  void setSpotRotationAllowed(bool allowed);
  void initializeStringProperties();
};

// -------------------------------------------------------------------------------------------------
class PresetModel : public QAbstractListModel
{
  Q_OBJECT

public:
  PresetModel(QObject* parent = nullptr);
  PresetModel(std::vector<QString>&& presets, QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

  const auto& presets() const { return m_presets; }
  bool hasPreset(const QString& preset) const;

private:
  friend class Settings;

  void addPreset(const QString& preset);
  void removePreset(const QString& preset);
  std::vector<QString> m_presets;
};
