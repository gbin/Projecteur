// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "settings.h"

#include "device.h"
#include "deviceinput.h"
#include "logging.h"

#include <algorithm>
#include <utility>

#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <QQmlPropertyMap>
#include <QSettings>

LOGGING_CATEGORY(lcSettings, "settings")

namespace {
  // -----------------------------------------------------------------------------------------------
  namespace settings {
    constexpr char showSpotShade[] = "showSpotShade";
    constexpr char spotSize[] = "spotSize";
    constexpr char showCenterDot[] = "showCenterDot";
    constexpr char dotSize[] = "dotSize";
    constexpr char dotColor[] = "dotColor";
    constexpr char dotOpacity[] = "dotOpacity";
    constexpr char shadeColor[] = "shadeColor";
    constexpr char shadeOpacity[] = "shadeOpacity";
    constexpr char cursor[] = "cursor";
    constexpr char spotShape[] = "spotShape";
    constexpr char spotRotation[] ="spotRotation";
    constexpr char showBorder[] = "showBorder";
    constexpr char borderColor[] ="borderColor";
    constexpr char borderSize[] = "borderSize";
    constexpr char borderOpacity[] = "borderOpacity";
    constexpr char zoomEnabled[] = "enableZoom";
    constexpr char zoomFactor[] = "zoomFactor";
    constexpr char multiScreenOverlay[] = "multiScreenOverlay";
    constexpr char presentationTimerEnabled[] = "presentationTimerEnabled";
    constexpr char presentationTimerDurationSeconds[] = "presentationTimerDurationSeconds";

    // -- device specific
    constexpr char inputSequenceInterval[] = "inputSequenceInterval";
    constexpr char inputMapConfig[] = "inputMapConfig";
    constexpr char presentationTimerHapticStrength[] = "presentationTimerHapticStrength";
    constexpr char legacyVibrationIntensity[] = "vibrationIntensity";

    namespace defaultValue {
      constexpr bool showSpotShade = true;
      constexpr int spotSize = 32;
      constexpr bool showCenterDot = false;
      constexpr int dotSize = 5;
      constexpr auto dotColor = Qt::red;
      constexpr double dotOpacity = 0.8;
      constexpr char shadeColor[] = "#222222";
      constexpr double shadeOpacity = 0.3;
      constexpr Qt::CursorShape cursor = Qt::BlankCursor;
      constexpr char spotShape[] = "spotshapes/Circle.qml";
      constexpr double spotRotation = 0.0;
      constexpr bool showBorder = true;
      constexpr auto borderColor = "#73d216"; // some kind of neon-like-green
      constexpr int borderSize = 4;
      constexpr double borderOpacity = 0.8;
      constexpr bool zoomEnabled = false;
      constexpr double zoomFactor = 2.0;
      constexpr bool multiScreenOverlay = false;
      constexpr bool presentationTimerEnabled = false;
      constexpr int presentationTimerDurationSeconds = 15 * 60;

      // -- device specific defaults
      constexpr int inputSequenceInterval = 250;
      constexpr int presentationTimerHapticStrength = 50;
    } // end namespace defaultValue

    namespace ranges {
      constexpr Settings::SettingRange<int> spotSize{ 5, 100 };
      constexpr Settings::SettingRange<int> dotSize{ 3, 100 };
      constexpr Settings::SettingRange<double> dotOpacity{ 0.0, 1.0 };
      constexpr Settings::SettingRange<double> shadeOpacity{ 0.0, 1.0 };
      constexpr Settings::SettingRange<double> spotRotation{ 0.0, 360.0 };
      constexpr Settings::SettingRange<int> borderSize{ 0, 100 };
      constexpr Settings::SettingRange<double> borderOpacity{ 0.0, 1.0 };
      constexpr Settings::SettingRange<double> zoomFactor{ 1.5, 20.0 };

      constexpr Settings::SettingRange<int> inputSequenceInterval{ 100, 950 };
    } // end namespace ranges
  } // end namespace settings

  // -----------------------------------------------------------------------------------------------
  bool toBool(const QString& value) {
    return (value.toLower() == "true" || value.toLower() == "on" || value.toInt() > 0);
  }

  // -----------------------------------------------------------------------------------------------
  #define SETTINGS_PRESET_PREFIX "Preset_"
  QString presetSection(const QString& preset, bool withSeparator = true) {
     return QString(SETTINGS_PRESET_PREFIX "%1%2").arg(preset).arg(withSeparator ? "/" : "");
  }

  // -----------------------------------------------------------------------------------------------
  QString settingsKey(const DeviceId& dId, const QString& key) {
    return QString("Device_%1_%2/%3")
      .arg(logging::hexId(dId.vendorId), logging::hexId(dId.productId), key);
  }

  // -------------------------------------------------------------------------------------------------
  auto loadPresets(QSettings* settings)
  {
    std::vector<QString> presets;
    for (const auto& group: settings->childGroups()) {
      if (group.startsWith(SETTINGS_PRESET_PREFIX)) {
        presets.emplace_back(group.mid(sizeof(SETTINGS_PRESET_PREFIX)-1));
      }
    }
    std::sort(presets.begin(), presets.end());
    return presets;
  }
} // end anonymous namespace


// -------------------------------------------------------------------------------------------------
Settings::Settings(QObject* parent)
  : QObject(parent)
  , m_settings(new QSettings(QCoreApplication::applicationName(),
                             QCoreApplication::applicationName(), this))
  , m_presetModel(new PresetModel(loadPresets(m_settings), this))
  , m_shapeSettingsRoot(QQmlPropertyMap::create(this))
{
  init();
}

// -------------------------------------------------------------------------------------------------
Settings::Settings(const QString& configFile, QObject* parent)
  : QObject(parent)
  , m_settings(new QSettings(configFile, QSettings::NativeFormat, this))
  , m_presetModel(new PresetModel(loadPresets(m_settings), this))
  , m_shapeSettingsRoot(QQmlPropertyMap::create(this))
{
  init();
}

// -------------------------------------------------------------------------------------------------
Settings::~Settings() = default;

// -------------------------------------------------------------------------------------------------
void Settings::init()
{
  const QFileInfo fi(m_settings->fileName());

  if (!fi.isReadable()) {
    logError(lcSettings) << tr("Settings file '%1' not readable.").arg(m_settings->fileName());
  }

  if (!fi.isWritable()) {
    logWarning(lcSettings) << tr("Settings file '%1' not writable.").arg(m_settings->fileName());
  }

  shapeSettingsInitialize();
  load();
  initializeStringProperties();
}

// -------------------------------------------------------------------------------------------------
void Settings::initializeStringProperties()
{
  auto& map = m_stringPropertyMap;
  // -- spot settings
  map.emplace_back( "spot.overlay", StringProperty{ StringProperty::Bool, {false, true},
                    [this](const QString& value){ setOverlayDisabled(!toBool(value)); } } );
  map.emplace_back( "spot.multi-screen", StringProperty{ StringProperty::Bool, {false, true},
                    [this](const QString& value){ setMultiScreenOverlayEnabled(toBool(value)); } } );
  map.emplace_back( "spot.size", StringProperty{ StringProperty::Integer,
                    {::settings::ranges::spotSize.min, ::settings::ranges::spotSize.max},
                    [this](const QString& value){ setSpotSize(value.toInt()); } } );
  map.emplace_back( "spot.rotation", StringProperty{ StringProperty::Double,
                    {::settings::ranges::spotRotation.min, ::settings::ranges::spotRotation.max},
                    [this](const QString& value){ setSpotRotation(value.toDouble()); } } );
  QVariantList shapesList;
  for (const auto& shape : spotShapes()) { shapesList.push_back(shape.name()); }
  map.emplace_back( "spot.shape", StringProperty{ StringProperty::StringEnum, shapesList,
    [this](const QString& value){
       for (const auto& shape : spotShapes()) {
         if (shape.name().toLower() == value.toLower()) {
           setSpotShape(shape.qmlComponent());
           break;
         }
       }
    }
  } );

  for (const auto& shape : spotShapes())
  {
    for (const auto& shapeSetting : shape.shapeSettings())
    {
      const auto pm = shapeSettings(shape.name());
      if (!pm || !pm->property(shapeSetting.settingsKey().toLocal8Bit()).isValid()) { continue; }

      if (shapeSetting.defaultValue().metaType().id() != QMetaType::Int) { continue; }

      const auto stringProperty = QString("spot.shape.%1.%2").arg(shape.name().toLower())
                                                             .arg(shapeSetting.settingsKey().toLower());
      map.emplace_back( stringProperty, StringProperty{ StringProperty::Integer,
                         {shapeSetting.minValue().toInt(), shapeSetting.maxValue().toInt()},
                         [pm, shapeSetting](const QString& value) {
                           const int newValue = qMin(qMax(shapeSetting.minValue().toInt(), value.toInt()),
                                                     shapeSetting.maxValue().toInt());
                           pm->setProperty(shapeSetting.settingsKey().toLocal8Bit(), newValue);
                         } } );
    }
  }

  // --- shade
  map.emplace_back( "shade", StringProperty{ StringProperty::Bool, {false, true},
                    [this](const QString& value){ setShowSpotShade(toBool(value)); } } );
  map.emplace_back( "shade.opacity", StringProperty{ StringProperty::Double,
                    {::settings::ranges::shadeOpacity.min, ::settings::ranges::shadeOpacity.max},
                    [this](const QString& value){ setShadeOpacity(value.toDouble()); } } );
  map.emplace_back( "shade.color", StringProperty{ StringProperty::Color, {},
                    [this](const QString& value){ setShadeColor(QColor(value)); } } );
  // --- center dot
  map.emplace_back( "dot", StringProperty{ StringProperty::Bool, {false, true},
                    [this](const QString& value){ setShowCenterDot(toBool(value)); } } );
  map.emplace_back( "dot.size", StringProperty{ StringProperty::Integer,
                    {::settings::ranges::dotSize.min, ::settings::ranges::dotSize.max},
                    [this](const QString& value){ setDotSize(value.toInt()); } } );
  map.emplace_back( "dot.color", StringProperty{ StringProperty::Color, {},
                    [this](const QString& value){ setDotColor(QColor(value)); } } );
  map.emplace_back( "dot.opacity", StringProperty{ StringProperty::Double,
                    {::settings::ranges::dotOpacity.min, ::settings::ranges::dotOpacity.max},
                    [this](const QString& value){ setDotOpacity(value.toDouble()); } } );
  // --- border
  map.emplace_back( "border", StringProperty{ StringProperty::Bool, {false, true},
                    [this](const QString& value){ setShowBorder(toBool(value)); } } );
  map.emplace_back( "border.size", StringProperty{ StringProperty::Integer,
                    {::settings::ranges::borderSize.min, ::settings::ranges::borderSize.max},
                    [this](const QString& value){ setBorderSize(value.toInt()); } } );
  map.emplace_back( "border.color", StringProperty{ StringProperty::Color, {},
                    [this](const QString& value){ setBorderColor(QColor(value)); } } );
  map.emplace_back( "border.opacity", StringProperty{ StringProperty::Double,
                    {::settings::ranges::borderOpacity.min, ::settings::ranges::borderOpacity.max},
                    [this](const QString& value){ setBorderOpacity(value.toDouble()); } } );
  // --- zoom
  map.emplace_back( "zoom", StringProperty{ StringProperty::Bool, {false, true},
                    [this](const QString& value){ setZoomEnabled(toBool(value)); } } );
  map.emplace_back( "zoom.factor", StringProperty{ StringProperty::Double,
                    {::settings::ranges::zoomFactor.min, ::settings::ranges::zoomFactor.max},
                    [this](const QString& value){ setZoomFactor(value.toDouble()); } } );
}

// -------------------------------------------------------------------------------------------------
const std::vector<std::pair<QString, Settings::StringProperty>>& Settings::stringProperties() const
{
  return m_stringPropertyMap;
}

// -------------------------------------------------------------------------------------------------
const Settings::SettingRange<int>& Settings::spotSizeRange() { return ::settings::ranges::spotSize; }
const Settings::SettingRange<int>& Settings::dotSizeRange() { return ::settings::ranges::dotSize; }
const Settings::SettingRange<double>& Settings::dotOpacityRange() { return settings::ranges::dotOpacity; }
const Settings::SettingRange<double>& Settings::shadeOpacityRange() { return ::settings::ranges::shadeOpacity; }
const Settings::SettingRange<double>& Settings::spotRotationRange() { return ::settings::ranges::spotRotation; }
const Settings::SettingRange<int>& Settings::borderSizeRange() { return settings::ranges::borderSize; }
const Settings::SettingRange<double>& Settings::borderOpacityRange() { return settings::ranges::borderOpacity; }
const Settings::SettingRange<double>& Settings::zoomFactorRange() { return settings::ranges::zoomFactor; }
const Settings::SettingRange<int>& Settings::inputSequenceIntervalRange() { return settings::ranges::inputSequenceInterval; }

// -------------------------------------------------------------------------------------------------
const QList<Settings::SpotShape>& Settings::spotShapes()
{
  static const QList<SpotShape> shapes{
    SpotShape(::settings::defaultValue::spotShape, "Circle", tr("Circle"), false),
    SpotShape("spotshapes/Square.qml", "Square", tr("(Rounded) Square"), true,
      {SpotShapeSetting(tr("Border-radius (%)"), "radius", 20, 0, 100, 0)} ),
    SpotShape("spotshapes/Star.qml", "Star", tr("Star"), true,
      {SpotShapeSetting(tr("Star points"), "points", 5, 3, 100, 0),
       SpotShapeSetting(tr("Inner radius (%)"), "innerRadius", 50, 5, 100, 0)} ),
    SpotShape("spotshapes/Ngon.qml", "Ngon", tr("N-gon"), true,
      {SpotShapeSetting(tr("Sides"), "sides", 3, 3, 100, 0)} ) };
  return shapes;
}

// -------------------------------------------------------------------------------------------------
void Settings::setDefaults()
{
  setShowSpotShade(settings::defaultValue::showSpotShade);
  setSpotSize(settings::defaultValue::spotSize);
  setShowCenterDot(settings::defaultValue::showCenterDot);
  setDotSize(settings::defaultValue::dotSize);
  setDotColor(QColor(settings::defaultValue::dotColor));
  setDotOpacity(settings::defaultValue::dotOpacity);
  setShadeColor(QColor(settings::defaultValue::shadeColor));
  setShadeOpacity(settings::defaultValue::shadeOpacity);
  setCursor(settings::defaultValue::cursor);
  setSpotShape(settings::defaultValue::spotShape);
  setSpotRotation(settings::defaultValue::spotRotation);
  setShowBorder(settings::defaultValue::showBorder);
  setBorderColor(settings::defaultValue::borderColor);
  setBorderSize(settings::defaultValue::borderSize);
  setBorderOpacity(settings::defaultValue::borderOpacity);
  setZoomEnabled(settings::defaultValue::zoomEnabled);
  setZoomFactor(settings::defaultValue::zoomFactor);
  setMultiScreenOverlayEnabled(settings::defaultValue::multiScreenOverlay);
  shapeSettingsSetDefaults();
}

// -------------------------------------------------------------------------------------------------
void Settings::shapeSettingsSetDefaults()
{
  for (const auto& shape : spotShapes())
  {
    for (const auto& settingDefinition : shape.shapeSettings())
    {
      if (auto propertyMap = shapeSettings(shape.name()))
      {
        const QString& key = settingDefinition.settingsKey();
        if (propertyMap->property(key.toLocal8Bit()).isValid()) {
          propertyMap->setProperty(key.toLocal8Bit(), settingDefinition.defaultValue());
        } else {
          propertyMap->insert(key, settingDefinition.defaultValue());
        }
      }
    }
  }
  shapeSettingsPopulateRoot();
}

// -------------------------------------------------------------------------------------------------
void Settings::shapeSettingsLoad(const QString& preset)
{
  const auto section = preset.size() ? presetSection(preset) : "";

  for (const auto& shape : spotShapes())
  {
    for (const auto& settingDefinition : shape.shapeSettings())
    {
      if (auto propertyMap = shapeSettings(shape.name()))
      {
        const QString& key = settingDefinition.settingsKey();
        const QString settingsKey = section + QString("Shape.%1/%2").arg(shape.name()).arg(key);
        const QVariant loadedValue = m_settings->value(settingsKey, settingDefinition.defaultValue());

        if (settingDefinition.defaultValue().metaType().id() == QMetaType::Int // Currently only int shape settings supported
            && settingDefinition.defaultValue() != loadedValue) {
          logDebug(lcSettings) << QString("spot.shape.%1.%2 = ").arg(shape.name().toLower(), key) << loadedValue.toInt();
        }

        if (propertyMap->property(key.toLocal8Bit()).isValid()) {
          propertyMap->setProperty(key.toLocal8Bit(), loadedValue);
        } else {
          propertyMap->insert(key, loadedValue);
        }
      }
    }
  }
  shapeSettingsPopulateRoot();
}

// -------------------------------------------------------------------------------------------------
void Settings::shapeSettingsSavePreset(const QString& preset)
{
  const auto section = preset.size() ? presetSection(preset) : "";

  for (const auto& shape : spotShapes())
  {
    for (const auto& settingDefinition : shape.shapeSettings())
    {
      if (auto propertyMap = shapeSettings(shape.name()))
      {
        const QString& key = settingDefinition.settingsKey();
        const QString settingsKey = section + QString("Shape.%1/%2").arg(shape.name()).arg(key);
        m_settings->setValue(settingsKey, propertyMap->property(key.toLocal8Bit()));
      }
    }
  }
}

// -------------------------------------------------------------------------------------------------
void Settings::shapeSettingsInitialize()
{
  for (const auto& shape : spotShapes())
  {
    if (shape.shapeSettings().size() && m_shapeSettings.count(shape.name()) == 0)
    {
      auto pm = QQmlPropertyMap::create(this);
      connect(pm, &QQmlPropertyMap::valueChanged, this,
      [this, shape, pm](const QString& key, const QVariant& value)
      {
        const auto& s = shape.shapeSettings();
        auto it = std::find_if(s.cbegin(), s.cend(), [&key](const SpotShapeSetting& sss) {
          return key == sss.settingsKey();
        });

        if (it != s.cend())
        {
          if (it->defaultValue().metaType().id() == QMetaType::Int)
          {
            const auto setValue = value.toInt();
            const auto min = it->minValue().toInt();
            const auto max = it->maxValue().toInt();
            const auto newValue = qMin(qMax(min, setValue), max);
            if (newValue != setValue) {
              pm->setProperty(key.toLocal8Bit(), newValue);
            }
            logDebug(lcSettings) << QString("spot.shape.%1.%2 = ").arg(shape.name().toLower(), it->settingsKey())
                                 << setValue;
            m_settings->setValue(QString("Shape.%1/%2").arg(shape.name()).arg(key), newValue);
          }
        }
      });
      m_shapeSettings.emplace(shape.name(), pm);
    }
  }
  shapeSettingsPopulateRoot();
}

// -------------------------------------------------------------------------------------------------
void Settings::loadPreset(const QString& preset)
{
  if (m_presetModel->hasPreset(preset))
  {
    load(preset);
    emit presetLoaded(preset);
  }
}

// -------------------------------------------------------------------------------------------------
void Settings::removePreset(const QString& preset)
{
  m_presetModel->removePreset(preset);
  m_settings->remove(presetSection(preset, false));
}

// -------------------------------------------------------------------------------------------------
const std::vector<QString>& Settings::presets() const
{
  return m_presetModel->presets();
}

// -------------------------------------------------------------------------------------------------
PresetModel* Settings::presetModel()
{
  return m_presetModel;
}

// -------------------------------------------------------------------------------------------------
void Settings::load(const QString& preset)
{
  logDebug(lcSettings) << tr("Loading values from config:") << m_settings->fileName()
                       << (preset.size() ? QString("(%1)").arg(preset) : "");

  const auto s = preset.size() ? presetSection(preset) : "";
  setShowSpotShade(m_settings->value(s+::settings::showSpotShade, settings::defaultValue::showSpotShade).toBool());
  setSpotSize(m_settings->value(s+::settings::spotSize, settings::defaultValue::spotSize).toInt());
  setShowCenterDot(m_settings->value(s+::settings::showCenterDot, settings::defaultValue::showCenterDot).toBool());
  setDotSize(m_settings->value(s+::settings::dotSize, settings::defaultValue::dotSize).toInt());
  setDotColor(m_settings->value(s+::settings::dotColor, QColor(settings::defaultValue::dotColor)).value<QColor>());
  setDotOpacity(m_settings->value(s+::settings::dotOpacity, settings::defaultValue::dotOpacity).toDouble());
  setShadeColor(m_settings->value(s+::settings::shadeColor, QColor(settings::defaultValue::shadeColor)).value<QColor>());
  setShadeOpacity(m_settings->value(s+::settings::shadeOpacity, settings::defaultValue::shadeOpacity).toDouble());
  setCursor(static_cast<Qt::CursorShape>(m_settings->value(s+::settings::cursor, static_cast<int>(settings::defaultValue::cursor)).toInt()));
  setSpotShape(m_settings->value(s+::settings::spotShape, settings::defaultValue::spotShape).toString());
  setSpotRotation(m_settings->value(s+::settings::spotRotation, settings::defaultValue::spotRotation).toDouble());
  setShowBorder(m_settings->value(s+::settings::showBorder, settings::defaultValue::showBorder).toBool());
  setBorderColor(m_settings->value(s+::settings::borderColor, QColor(settings::defaultValue::borderColor)).value<QColor>());
  setBorderSize(m_settings->value(s+::settings::borderSize, settings::defaultValue::borderSize).toInt());
  setBorderOpacity(m_settings->value(s+::settings::borderOpacity, settings::defaultValue::borderOpacity).toDouble());
  setZoomEnabled(m_settings->value(s+::settings::zoomEnabled, settings::defaultValue::zoomEnabled).toBool());
  setZoomFactor(m_settings->value(s+::settings::zoomFactor, settings::defaultValue::zoomFactor).toDouble());
  setMultiScreenOverlayEnabled(m_settings->value(s+::settings::multiScreenOverlay, settings::defaultValue::multiScreenOverlay).toBool());
  shapeSettingsLoad(preset);
}

// -------------------------------------------------------------------------------------------------
void Settings::savePreset(const QString& preset)
{
  const auto section = presetSection(preset);

  m_settings->setValue(section+::settings::showSpotShade, m_showSpotShade);
  m_settings->setValue(section+::settings::spotSize, m_spotSize);
  m_settings->setValue(section+::settings::showCenterDot, m_showCenterDot);
  m_settings->setValue(section+::settings::dotSize, m_dotSize);
  m_settings->setValue(section+::settings::dotColor, m_dotColor);
  m_settings->setValue(section+::settings::dotOpacity, m_dotOpacity);
  m_settings->setValue(section+::settings::shadeColor, m_shadeColor);
  m_settings->setValue(section+::settings::shadeOpacity, m_shadeOpacity);
  m_settings->setValue(section+::settings::cursor, static_cast<int>(m_cursor));
  m_settings->setValue(section+::settings::spotShape, m_spotShape);
  m_settings->setValue(section+::settings::spotRotation, m_spotRotation);
  m_settings->setValue(section+::settings::showBorder, m_showBorder);
  m_settings->setValue(section+::settings::borderColor, m_borderColor);
  m_settings->setValue(section+::settings::borderSize, m_borderSize);
  m_settings->setValue(section+::settings::borderOpacity, m_borderOpacity);
  m_settings->setValue(section+::settings::zoomEnabled, m_zoomEnabled);
  m_settings->setValue(section+::settings::zoomFactor, m_zoomFactor);
  m_settings->setValue(section+::settings::multiScreenOverlay, m_multiScreenOverlayEnabled);
  shapeSettingsSavePreset(preset);

  m_presetModel->addPreset(preset);
  emit presetLoaded(preset);
}

// -------------------------------------------------------------------------------------------------
void Settings::setShowSpotShade(bool show)
{
  if (show == m_showSpotShade) { return; }

  m_showSpotShade = show;
  m_settings->setValue(::settings::showSpotShade, m_showSpotShade);
  logDebug(lcSettings) << "shade =" << m_showSpotShade;
  emit showSpotShadeChanged(m_showSpotShade);
}

// -------------------------------------------------------------------------------------------------
void Settings::setSpotSize(int size)
{
  if (size == m_spotSize) { return; }

  m_spotSize = qMin(qMax(::settings::ranges::spotSize.min, size), ::settings::ranges::spotSize.max);
  m_settings->setValue(::settings::spotSize, m_spotSize);
  logDebug(lcSettings) << "spot.size =" << m_spotSize;
  emit spotSizeChanged(m_spotSize);
}

// -------------------------------------------------------------------------------------------------
void Settings::setShowCenterDot(bool show)
{
  if (show == m_showCenterDot) { return; }

  m_showCenterDot = show;
  m_settings->setValue(::settings::showCenterDot, m_showCenterDot);
  logDebug(lcSettings) << "dot =" << m_showCenterDot;
  emit showCenterDotChanged(m_showCenterDot);
}

// -------------------------------------------------------------------------------------------------
void Settings::setDotSize(int size)
{
  if (size == m_dotSize) { return; }

  m_dotSize = qMin(qMax(::settings::ranges::dotSize.min, size), ::settings::ranges::dotSize.max);
  m_settings->setValue(::settings::dotSize, m_dotSize);
  logDebug(lcSettings) << "dot.size =" << m_dotSize;
  emit dotSizeChanged(m_dotSize);
}

// -------------------------------------------------------------------------------------------------
void Settings::setDotColor(const QColor& color)
{
  if (color == m_dotColor) { return; }

  m_dotColor = color;
  m_settings->setValue(::settings::dotColor, m_dotColor);
  logDebug(lcSettings) << "dot.color =" << m_dotColor.name();
  emit dotColorChanged(m_dotColor);
}

// -------------------------------------------------------------------------------------------------
void Settings::setDotOpacity(double opacity)
{
  if (opacity > m_dotOpacity || opacity < m_dotOpacity)
  {
    m_dotOpacity = qMin(qMax(::settings::ranges::dotOpacity.min, opacity), ::settings::ranges::dotOpacity.max);
    m_settings->setValue(::settings::dotOpacity, m_dotOpacity);
    logDebug(lcSettings) << "dot.opacity = " << m_dotOpacity;
    emit dotOpacityChanged(m_dotOpacity);
  }
}

// -------------------------------------------------------------------------------------------------
void Settings::setShadeColor(const QColor& color)
{
  if (color == m_shadeColor) { return; }

  m_shadeColor = color;
  m_settings->setValue(::settings::shadeColor, m_shadeColor);
  logDebug(lcSettings) << "shade.color =" << m_shadeColor.name();
  emit shadeColorChanged(m_shadeColor);
}

// -------------------------------------------------------------------------------------------------
void Settings::setShadeOpacity(double opacity)
{
  if (opacity > m_shadeOpacity || opacity < m_shadeOpacity)
  {
    m_shadeOpacity = qMin(qMax(::settings::ranges::shadeOpacity.min, opacity), ::settings::ranges::shadeOpacity.max);
    m_settings->setValue(::settings::shadeOpacity, m_shadeOpacity);
    logDebug(lcSettings) << "shade.opacity = " << m_shadeOpacity;
    emit shadeOpacityChanged(m_shadeOpacity);
  }
}

// -------------------------------------------------------------------------------------------------
void Settings::setCursor(Qt::CursorShape cursor)
{
  if (cursor == m_cursor) { return; }

  m_cursor = qMin(qMax(static_cast<Qt::CursorShape>(0), cursor), Qt::LastCursor);
  m_settings->setValue(::settings::cursor, static_cast<int>(m_cursor));
  logDebug(lcSettings) << "cursor = " << m_cursor;
  emit cursorChanged(m_cursor);
}

// -------------------------------------------------------------------------------------------------
void Settings::setSpotShape(const QString& spotShapeQmlComponent)
{
  if (m_spotShape == spotShapeQmlComponent) { return; }

  const auto it = std::find_if(spotShapes().cbegin(), spotShapes().cend(),
  [&spotShapeQmlComponent](const SpotShape& s) {
    return s.qmlComponent() == spotShapeQmlComponent;
  });

  if (it != spotShapes().cend()) {
    m_spotShape = it->qmlComponent();
    m_settings->setValue(::settings::spotShape, m_spotShape);
    logDebug(lcSettings) << "spot.shape = " << m_spotShape;
    emit spotShapeChanged(m_spotShape);
    setSpotRotationAllowed(it->allowRotation());
  }
}

// -------------------------------------------------------------------------------------------------
void Settings::setSpotRotation(double rotation)
{
  if (rotation > m_spotRotation || rotation < m_spotRotation)
  {
    m_spotRotation = qMin(qMax(::settings::ranges::spotRotation.min, rotation), ::settings::ranges::spotRotation.max);
    m_settings->setValue(::settings::spotRotation, m_spotRotation);
    logDebug(lcSettings) << "spot.rotation = " << m_spotRotation;
    emit spotRotationChanged(m_spotRotation);
  }
}

// -------------------------------------------------------------------------------------------------
QObject* Settings::shapeSettingsRootObject()
{
  return m_shapeSettingsRoot;
}

// -------------------------------------------------------------------------------------------------
QQmlPropertyMap* Settings::shapeSettings(const QString &shapeName)
{
  const auto it = m_shapeSettings.find(shapeName);
  if (it != m_shapeSettings.cend()) {
    return it->second;
  }
  return nullptr;
}

// -------------------------------------------------------------------------------------------------
void Settings::shapeSettingsPopulateRoot()
{
  for (const auto& item : m_shapeSettings)
  {
    if (m_shapeSettingsRoot->property(item.first.toLocal8Bit()).isValid()) {
      m_shapeSettingsRoot->setProperty(item.first.toLocal8Bit(), QVariant::fromValue(item.second));
    } else {
      m_shapeSettingsRoot->insert(item.first, QVariant::fromValue(item.second));
    }
  }
}

// -------------------------------------------------------------------------------------------------
bool Settings::spotRotationAllowed() const
{
  return m_spotRotationAllowed;
}

// -------------------------------------------------------------------------------------------------
void Settings::setSpotRotationAllowed(bool allowed)
{
  if (allowed == m_spotRotationAllowed) { return; }

  m_spotRotationAllowed = allowed;
  emit spotRotationAllowedChanged(allowed);
}

// -------------------------------------------------------------------------------------------------
void Settings::setShowBorder(bool show)
{
  if (show == m_showBorder) { return; }

  m_showBorder = show;
  m_settings->setValue(::settings::showBorder, m_showBorder);
  logDebug(lcSettings) << "border = " << m_showBorder;
  emit showBorderChanged(m_showBorder);
}

// -------------------------------------------------------------------------------------------------
void Settings::setBorderColor(const QColor& color)
{
  if (color == m_borderColor) { return; }

  m_borderColor = color;
  m_settings->setValue(::settings::borderColor, m_borderColor);
  logDebug(lcSettings) << "border.color = " << m_borderColor.name();
  emit borderColorChanged(m_borderColor);
}

// -------------------------------------------------------------------------------------------------
void Settings::setBorderSize(int size)
{
  if (size == m_borderSize) { return; }

  m_borderSize = qMin(qMax(::settings::ranges::borderSize.min, size), ::settings::ranges::borderSize.max);
  m_settings->setValue(::settings::borderSize, m_borderSize);
  logDebug(lcSettings) << "border.size = " << m_borderSize;
  emit borderSizeChanged(m_borderSize);
}

// -------------------------------------------------------------------------------------------------
void Settings::setBorderOpacity(double opacity)
{
  if (opacity > m_borderOpacity || opacity < m_borderOpacity)
  {
    m_borderOpacity = qMin(qMax(::settings::ranges::borderOpacity.min, opacity), ::settings::ranges::borderOpacity.max);
    m_settings->setValue(::settings::borderOpacity, m_borderOpacity);
    logDebug(lcSettings) << "border.opacity = " << m_borderOpacity;
    emit borderOpacityChanged(m_borderOpacity);
  }
}

// -------------------------------------------------------------------------------------------------
void Settings::setZoomEnabled(bool enabled)
{
  if (enabled == m_zoomEnabled) { return; }

  m_zoomEnabled = enabled;
  m_settings->setValue(::settings::zoomEnabled, m_zoomEnabled);
  logDebug(lcSettings) << "zoom = " << m_zoomEnabled;
  emit zoomEnabledChanged(m_zoomEnabled);
}

// -------------------------------------------------------------------------------------------------
void Settings::setZoomFactor(double factor)
{
  if (factor > m_zoomFactor || factor < m_zoomFactor)
  {
    m_zoomFactor = qMin(qMax(::settings::ranges::zoomFactor.min, factor), ::settings::ranges::zoomFactor.max);
    m_settings->setValue(::settings::zoomFactor, m_zoomFactor);
    logDebug(lcSettings) << "zoom.factor = " << m_zoomFactor;
    emit zoomFactorChanged(m_zoomFactor);
  }
}

// -------------------------------------------------------------------------------------------------
void Settings::setMultiScreenOverlayEnabled(bool enabled)
{
    if (m_multiScreenOverlayEnabled == enabled) { return; }
    m_multiScreenOverlayEnabled = enabled;
    m_settings->setValue(::settings::multiScreenOverlay, m_multiScreenOverlayEnabled);
    logDebug(lcSettings) << "multi-screen-overlay = " << m_multiScreenOverlayEnabled;
    emit multiScreenOverlayEnabledChanged(m_multiScreenOverlayEnabled);
}

// -------------------------------------------------------------------------------------------------
void Settings::setOverlayDisabled(bool disabled)
{
  if (m_overlayDisabled == disabled) { return; }
  m_overlayDisabled = disabled;
  emit overlayDisabledChanged(m_overlayDisabled);
}

// -------------------------------------------------------------------------------------------------
QString Settings::StringProperty::typeToString(Type type)
{
  switch(type) {
  case Type::Bool: return "Bool";
  case Type::Color: return "Color";
  case Type::Double: return "Double";
  case Type::Integer: return "Integer";
  case Type::StringEnum: return "Value";
  }
  return QString();
}

// -------------------------------------------------------------------------------------------------
void Settings::setDeviceInputSeqInterval(const DeviceId& dId, int intervalMs)
{
  const auto v = qMin(qMax(::settings::ranges::inputSequenceInterval.min, intervalMs),
                           ::settings::ranges::inputSequenceInterval.max);
  m_settings->setValue(settingsKey(dId, ::settings::inputSequenceInterval), v);
}

// -------------------------------------------------------------------------------------------------
int Settings::deviceInputSeqInterval(const DeviceId& dId) const
{
  const auto value = m_settings->value(settingsKey(dId, ::settings::inputSequenceInterval),
                                       ::settings::defaultValue::inputSequenceInterval).toInt();
  return qMin(qMax(::settings::ranges::inputSequenceInterval.min, value),
                   ::settings::ranges::inputSequenceInterval.max);
}

// -------------------------------------------------------------------------------------------------
void Settings::setDeviceInputMapConfig(const DeviceId& dId, const InputMapConfig& imc)
{
  const int sizeBefore = m_settings->value(settingsKey(dId, ::settings::inputMapConfig)
                                           + "/size", 0).toInt();
  m_settings->beginWriteArray(settingsKey(dId, ::settings::inputMapConfig), imc.size());
  int index = 0;
  for (const auto& item : imc)
  {
    m_settings->setArrayIndex(index++);
    m_settings->setValue("deviceSequence", QVariant::fromValue(item.first));
    m_settings->setValue("mappedAction", QVariant::fromValue(item.second));
  }
  m_settings->endArray();

  // Remove old entries...
  m_settings->beginGroup(settingsKey(dId, ::settings::inputMapConfig));
  for (; index < sizeBefore; ++index) {
    m_settings->remove(QString::number(index+1));
  }
  m_settings->endGroup();
}

// -------------------------------------------------------------------------------------------------
InputMapConfig Settings::getDeviceInputMapConfig(const DeviceId& dId)
{
  InputMapConfig cfg;

  const int size = m_settings->beginReadArray(settingsKey(dId, ::settings::inputMapConfig));
  for (int i = 0; i < size; ++i)
  {
    m_settings->setArrayIndex(i);
    const auto seq = m_settings->value("deviceSequence");
    if (!seq.canConvert<KeyEventSequence>()) { continue; }
    const auto conf = m_settings->value("mappedAction");
    if (!conf.canConvert<MappedAction>()) { continue; }
    auto mappedAction = qvariant_cast<MappedAction>(conf);
    if (mappedAction.action->type() == Action::Type::ScrollHorizontal) {
      mappedAction.action = GlobalActions::scrollHorizontal();
    } else if (mappedAction.action->type() == Action::Type::ScrollVertical) {
      mappedAction.action = GlobalActions::scrollVertical();
    } else if (mappedAction.action->type() == Action::Type::VolumeControl) {
      mappedAction.action = GlobalActions::volumeControl();
    }
    cfg.emplace(qvariant_cast<KeyEventSequence>(seq), std::move(mappedAction));
  }
  m_settings->endArray();

  return cfg;
}

// -------------------------------------------------------------------------------------------------
void Settings::setDevicePresentationTimerHapticStrength(const DeviceId& dId, int strength)
{
  m_settings->setValue(
    settingsKey(dId, ::settings::presentationTimerHapticStrength),
    std::clamp(strength, 0, 100));
}

// -------------------------------------------------------------------------------------------------
int Settings::devicePresentationTimerHapticStrength(const DeviceId& dId) const
{
  const auto deviceKey = settingsKey(dId, ::settings::presentationTimerHapticStrength);
  if (m_settings->contains(deviceKey)) {
    return std::clamp(m_settings->value(deviceKey).toInt(), 0, 100);
  }

  const auto legacyDeviceKey = settingsKey(dId, ::settings::legacyVibrationIntensity);
  if (m_settings->contains(legacyDeviceKey)) {
    const int intensity = std::clamp(m_settings->value(legacyDeviceKey).toInt(), 0, 255);
    return (intensity * 100 + 127) / 255;
  }

  // Preserve the value from the short-lived global setting as the initial value
  // for devices that do not have an older per-device vibration setting.
  if (m_settings->contains(::settings::presentationTimerHapticStrength)) {
    return std::clamp(
      m_settings->value(::settings::presentationTimerHapticStrength).toInt(), 0, 100);
  }

  return ::settings::defaultValue::presentationTimerHapticStrength;
}

// -------------------------------------------------------------------------------------------------
void Settings::setPresentationTimerEnabled(bool enabled)
{
  m_settings->setValue(::settings::presentationTimerEnabled, enabled);
}

// -------------------------------------------------------------------------------------------------
bool Settings::presentationTimerEnabled() const
{
  return m_settings->value(
    ::settings::presentationTimerEnabled,
    ::settings::defaultValue::presentationTimerEnabled).toBool();
}

// -------------------------------------------------------------------------------------------------
void Settings::setPresentationTimerDurationSeconds(int seconds)
{
  m_settings->setValue(::settings::presentationTimerDurationSeconds, seconds);
}

// -------------------------------------------------------------------------------------------------
int Settings::presentationTimerDurationSeconds() const
{
  return m_settings->value(
    ::settings::presentationTimerDurationSeconds,
    ::settings::defaultValue::presentationTimerDurationSeconds).toInt();
}

// -------------------------------------------------------------------------------------------------
PresetModel::PresetModel(QObject* parent)
  : PresetModel({}, parent)
{}

// -------------------------------------------------------------------------------------------------
PresetModel::PresetModel(std::vector<QString>&& presets, QObject* parent)
  : QAbstractListModel(parent)
  , m_presets(std::move(presets))
{
  std::sort(m_presets.begin(), m_presets.end());
}

// -------------------------------------------------------------------------------------------------
int PresetModel::rowCount(const QModelIndex& parent) const
{
  return (parent == QModelIndex()) ? m_presets.size() + 1 : 0;
}

// -------------------------------------------------------------------------------------------------
QVariant PresetModel::data(const QModelIndex& index, int role) const
{
  if (index.row() > static_cast<int>(m_presets.size())) {
    return QVariant();
  }

  if (role == Qt::DisplayRole)
  {
    if (index.row() == 0) {
      return tr("Current Settings");
    }

    return m_presets[index.row()-1];
  }

  if (role == Qt::FontRole && index.row() == 0)
  {
    QFont f;
    f.setItalic(true);
    return f;
  }

  if (role == Qt::ForegroundRole && index.row() == 0) {
    return QColor(QGuiApplication::palette().color(QPalette::Disabled, QPalette::Text));
  }

  return QVariant();
}

// -------------------------------------------------------------------------------------------------
void PresetModel::addPreset(const QString& preset)
{
  const auto lb = std::lower_bound(m_presets.begin(), m_presets.end(), preset);
  if (lb != m_presets.end() && *lb == preset) { return; } // Already exists

  const auto insertRow = std::distance(m_presets.begin(), lb) + 1;
  beginInsertRows(QModelIndex(), insertRow, insertRow);
  m_presets.emplace(lb, preset);
  endInsertRows();
}

// -------------------------------------------------------------------------------------------------
bool PresetModel::hasPreset(const QString& preset) const
{
  return (std::find(m_presets.cbegin(), m_presets.cend(), preset) != m_presets.cend());
}

// -------------------------------------------------------------------------------------------------
void PresetModel::removePreset(const QString& preset)
{
  const auto r = std::equal_range(m_presets.begin(), m_presets.end(), preset);
  const auto count = std::distance(r.first, r.second);
  if (count == 0) { return; }

  const auto startRow = std::distance(m_presets.begin(), r.first) + 1;

  beginRemoveRows(QModelIndex(), startRow, startRow + count - 1);
  m_presets.erase(r.first, r.second);
  endRemoveRows();
}
