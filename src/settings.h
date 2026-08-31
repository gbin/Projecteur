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
class KCoreConfigSkeleton;
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
  Q_PROPERTY(QString zoomMode READ zoomMode WRITE setZoomMode NOTIFY zoomModeChanged)
  Q_PROPERTY(QString pointerMode READ pointerMode WRITE setPointerMode NOTIFY pointerModeChanged)
  Q_PROPERTY(int laserSize READ laserSize WRITE setLaserSize NOTIFY laserSizeChanged)
  Q_PROPERTY(QColor laserColor READ laserColor WRITE setLaserColor NOTIFY laserColorChanged)
  Q_PROPERTY(double laserOpacity READ laserOpacity WRITE setLaserOpacity NOTIFY laserOpacityChanged)
  Q_PROPERTY(bool laserGlow READ laserGlow WRITE setLaserGlow NOTIFY laserGlowChanged)
  Q_PROPERTY(int laserGlowSize READ laserGlowSize WRITE setLaserGlowSize NOTIFY laserGlowSizeChanged)
  Q_PROPERTY(double laserGlowOpacity READ laserGlowOpacity WRITE setLaserGlowOpacity NOTIFY laserGlowOpacityChanged)
  Q_PROPERTY(QColor laserGlowColor READ laserGlowColor WRITE setLaserGlowColor NOTIFY laserGlowColorChanged)
  Q_PROPERTY(bool laserTrail READ laserTrail WRITE setLaserTrail NOTIFY laserTrailChanged)
  Q_PROPERTY(int laserTrailTime READ laserTrailTime WRITE setLaserTrailTime NOTIFY laserTrailTimeChanged)
  Q_PROPERTY(int laserTrailWidth READ laserTrailWidth WRITE setLaserTrailWidth NOTIFY laserTrailWidthChanged)
  Q_PROPERTY(QColor laserTrailColor READ laserTrailColor WRITE setLaserTrailColor NOTIFY laserTrailColorChanged)
  Q_PROPERTY(double laserTrailOpacity READ laserTrailOpacity WRITE setLaserTrailOpacity NOTIFY laserTrailOpacityChanged)
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
  QString zoomMode() const { return m_zoomMode; }
  void setZoomMode(const QString& mode);
  QString pointerMode() const { return m_pointerMode; }
  void setPointerMode(const QString& mode);
  static bool isPointerMode(const QString& mode);
  int laserSize() const { return m_laserSize; }
  void setLaserSize(int size);
  QColor laserColor() const { return m_laserColor; }
  void setLaserColor(const QColor& color);
  double laserOpacity() const { return m_laserOpacity; }
  void setLaserOpacity(double opacity);
  bool laserGlow() const { return m_laserGlow; }
  void setLaserGlow(bool glow);
  int laserGlowSize() const { return m_laserGlowSize; }
  void setLaserGlowSize(int size);
  double laserGlowOpacity() const { return m_laserGlowOpacity; }
  void setLaserGlowOpacity(double opacity);
  QColor laserGlowColor() const { return m_laserGlowColor; }
  void setLaserGlowColor(const QColor& color);
  bool laserTrail() const { return m_laserTrail; }
  void setLaserTrail(bool trail);
  int laserTrailTime() const { return m_laserTrailTime; }
  void setLaserTrailTime(int timeMs);
  int laserTrailWidth() const { return m_laserTrailWidth; }
  void setLaserTrailWidth(int width);
  QColor laserTrailColor() const { return m_laserTrailColor; }
  void setLaserTrailColor(const QColor& color);
  double laserTrailOpacity() const { return m_laserTrailOpacity; }
  void setLaserTrailOpacity(double opacity);
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
  static const SettingRange<int>& laserSizeRange();
  static const SettingRange<double>& laserOpacityRange();
  static const SettingRange<int>& laserGlowSizeRange();
  static const SettingRange<double>& laserGlowOpacityRange();
  static const SettingRange<int>& laserTrailTimeRange();
  static const SettingRange<int>& laserTrailWidthRange();
  static const SettingRange<double>& laserTrailOpacityRange();
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

  using SpotlightSettings = QVariantMap;
  SpotlightSettings spotlightSettings() const;
  static SpotlightSettings defaultSpotlightSettings();
  void setSpotlightSettings(const SpotlightSettings& values);
  KCoreConfigSkeleton* configSkeleton() const;

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
  void zoomModeChanged(const QString& mode);
  void pointerModeChanged(const QString& mode);
  void laserSizeChanged(int size);
  void laserColorChanged(const QColor& color);
  void laserOpacityChanged(double opacity);
  void laserGlowChanged(bool glow);
  void laserGlowSizeChanged(int size);
  void laserGlowOpacityChanged(double opacity);
  void laserGlowColorChanged(const QColor& color);
  void laserTrailChanged(bool trail);
  void laserTrailTimeChanged(int timeMs);
  void laserTrailWidthChanged(int width);
  void laserTrailColorChanged(const QColor& color);
  void laserTrailOpacityChanged(double opacity);
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
  QString m_zoomMode = QStringLiteral("smooth");
  QString m_pointerMode = QStringLiteral("spotlight");
  int m_laserSize = 16;
  QColor m_laserColor;
  double m_laserOpacity = 0.9;
  bool m_laserGlow = true;
  int m_laserGlowSize = 10;
  double m_laserGlowOpacity = 0.6;
  QColor m_laserGlowColor;
  bool m_laserTrail = false;
  int m_laserTrailTime = 400;
  int m_laserTrailWidth = 4;
  QColor m_laserTrailColor;
  double m_laserTrailOpacity = 0.8;
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
