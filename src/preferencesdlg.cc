// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "preferencesdlg.h"

#include "projecteur-GitVersion.h"  // auto generated version information

#include "deviceswidget.h"
#include "iconwidgets.h"
#include "logging.h"
#include "settings.h"

#include <KActionCollection>
#include <KColorButton>
#include <KFileCustomDialog>
#include <KFileFilter>
#include <KGlobalAccel>
#include <KLazyLocalizedString>
#include <KLocalizedString>
#include <KShortcutsEditor>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QQmlPropertyMap>
#include <QSpinBox>
#include <QStyle>
#include <QUrl>

#include <map>

LOGGING_CATEGORY(preferences, "preferences")

// -------------------------------------------------------------------------------------------------
namespace {
  #define CURSOR_PATH ":/icons/cursors/"
  static const std::map<const QString, const QPair<const KLazyLocalizedString, const Qt::CursorShape>> cursorMap {
    { "", {kli18n("No Cursor"), Qt::BlankCursor}},
    { CURSOR_PATH "cursor-arrow.png", {kli18n("Arrow Cursor"), Qt::ArrowCursor}},
    { CURSOR_PATH "cursor-busy.png", {kli18n("Busy Cursor"), Qt::BusyCursor}},
    { CURSOR_PATH "cursor-cross.png", {kli18n("Cross Cursor"), Qt::CrossCursor}},
    { CURSOR_PATH "cursor-hand.png", {kli18n("Pointing Hand Cursor"), Qt::PointingHandCursor}},
    { CURSOR_PATH "cursor-openhand.png", {kli18n("Open Hand Cursor"), Qt::OpenHandCursor}},
    { CURSOR_PATH "cursor-uparrow.png", {kli18n("Up Arrow Cursor"), Qt::UpArrowCursor}},
    { CURSOR_PATH "cursor-whatsthis.png", {kli18n("What's This Cursor"), Qt::WhatsThisCursor}},
  };
} // end anonymous namespace

// -------------------------------------------------------------------------------------------------
PreferencesDialog::PreferencesDialog(Settings* settings, Spotlight* spotlight,
                                     KActionCollection* actionCollection,
                                     Mode dialogMode, QWidget* parent)
  : KConfigDialog(parent, QStringLiteral("preferences"), settings->configSkeleton())
  , m_settings(settings)
  , m_actionCollection(actionCollection)
  , m_presetComboStyle(std::make_unique<PresetComboCustomStyle>())
{
  setAttribute(Qt::WA_DeleteOnClose, false);
  setWindowTitle(QCoreApplication::applicationName() + " - " + i18n("Preferences"));
  setWindowIcon(QIcon(":/icons/projecteur-tray.svg"));
  setFaceType(KPageDialog::Tabbed);

  setDialogMode(dialogMode);

  const auto settingsWidget = createSettingsTabWidget(settings);
  settingsWidget->setDisabled(settings->overlayDisabled());

  const auto spotlightPage = new QWidget(this);
  const auto spotlightLayout = new QVBoxLayout(spotlightPage);
  const auto overlayCheckBox = new QCheckBox(i18n("Enable spotlight overlay"), spotlightPage);
  overlayCheckBox->setChecked(!settings->overlayDisabled());
  spotlightLayout->addWidget(overlayCheckBox);
  spotlightLayout->addWidget(settingsWidget);

  addPage(spotlightPage, i18n("Spotlight"), QStringLiteral("preferences-desktop-display"),
          QString(), false);
  m_deviceswidget = new DevicesWidget(settings, spotlight, this);
  addPage(m_deviceswidget, i18n("Devices"), QStringLiteral("input-mouse"), QString(), false);
  m_shortcutsEditor = new KShortcutsEditor(
    actionCollection, this, KShortcutsEditor::GlobalAction,
    KShortcutsEditor::LetterShortcutsDisallowed);
  addPage(m_shortcutsEditor, i18n("Shortcuts"), QStringLiteral("configure-shortcuts"),
          QString(), false);
  addPage(createLogTabWidget(), i18n("Log"), QStringLiteral("utilities-log-viewer"),
          QString(), false);

  if (auto* helpButton = buttonBox()->button(QDialogButtonBox::Help)) {
    helpButton->hide();
  }

  connect(overlayCheckBox, &QCheckBox::toggled, this, [settings](bool checked){
    settings->setOverlayDisabled(!checked);
  });

  connect(settings, &Settings::overlayDisabledChanged, this,
  [overlayCheckBox, settingsWidget](bool disabled){
    overlayCheckBox->setChecked(!disabled);
    settingsWidget->setDisabled(disabled);
  });

  const auto modified = [this]() { settingsModified(); };
  connect(settings, &Settings::showSpotShadeChanged, this, modified);
  connect(settings, &Settings::spotSizeChanged, this, modified);
  connect(settings, &Settings::showCenterDotChanged, this, modified);
  connect(settings, &Settings::dotSizeChanged, this, modified);
  connect(settings, &Settings::dotColorChanged, this, modified);
  connect(settings, &Settings::dotOpacityChanged, this, modified);
  connect(settings, &Settings::shadeColorChanged, this, modified);
  connect(settings, &Settings::shadeOpacityChanged, this, modified);
  connect(settings, &Settings::cursorChanged, this, modified);
  connect(settings, &Settings::spotShapeChanged, this, modified);
  connect(settings, &Settings::spotRotationChanged, this, modified);
  connect(settings, &Settings::showBorderChanged, this, modified);
  connect(settings, &Settings::borderColorChanged, this, modified);
  connect(settings, &Settings::borderSizeChanged, this, modified);
  connect(settings, &Settings::borderOpacityChanged, this, modified);
  connect(settings, &Settings::zoomEnabledChanged, this, modified);
  connect(settings, &Settings::zoomFactorChanged, this, modified);
  connect(settings, &Settings::multiScreenOverlayEnabledChanged, this, modified);
  for (const auto& shape : Settings::spotShapes()) {
    if (auto* shapeSettings = settings->shapeSettings(shape.name())) {
      connect(shapeSettings, &QQmlPropertyMap::valueChanged, this, modified);
    }
  }
  connect(m_shortcutsEditor, &KShortcutsEditor::keyChange,
          this, &PreferencesDialog::updateButtons);

  m_appliedSpotlightSettings = settings->spotlightSettings();
  updateButtons();
}

QWidget* PreferencesDialog::createSettingsTabWidget(Settings* settings)
{
  const auto widget = new QWidget(this);
  const auto mainHBox = new QHBoxLayout;
  const auto spotScreenVBoxLeft = new QVBoxLayout();
  spotScreenVBoxLeft->addWidget(createShapeGroupBox(settings));
  spotScreenVBoxLeft->addWidget(createZoomGroupBox(settings));
  spotScreenVBoxLeft->addWidget(createCursorGroupBox(settings));
  spotScreenVBoxLeft->addWidget(createMultiScreenWidget(settings));
  const auto spotScreenVBoxRight = new QVBoxLayout();
  spotScreenVBoxRight->addWidget(createSpotGroupBox(settings));
  spotScreenVBoxRight->addWidget(createDotGroupBox(settings));
  spotScreenVBoxRight->addWidget(createBorderGroupBox(settings));
  mainHBox->addLayout(spotScreenVBoxLeft);
  mainHBox->addLayout(spotScreenVBoxRight);

  const auto presetSelector = createPresetSelector(settings);

  const auto testBtn = new QPushButton(i18n("&Show test..."), widget);
  connect(testBtn, &QPushButton::clicked, this, &PreferencesDialog::testButtonClicked);

  const auto hbox = new QHBoxLayout;
  hbox->addWidget(testBtn);
  hbox->addStretch(1);

  const auto mainVBox = new QVBoxLayout(widget);
  mainVBox->addLayout(mainHBox);
  mainVBox->addWidget(presetSelector);
  mainVBox->addLayout(hbox);

  return widget;
}

// -------------------------------------------------------------------------------------------------
QWidget* PreferencesDialog::createPresetSelector(Settings* settings)
{
  const auto widget = new QFrame(this);
  widget->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  const auto hbox = new QHBoxLayout(widget);
  hbox->addWidget(new QLabel(i18n("Presets"), widget));

  m_presetCombo = new QComboBox(widget);
  m_presetCombo->setModel(settings->presetModel());

  const auto normalComboStyle = m_presetCombo->style();
  m_presetCombo->setStyle(&*m_presetComboStyle); // style when no preset is selected
  m_presetCombo->setInsertPolicy(QComboBox::NoInsert);

  const auto deleteBtn = new IconButton(Font::Icon::trash_can_1, widget);
  deleteBtn->setToolTip(i18n("Delete currently selected preset."));
  deleteBtn->setEnabled(m_presetCombo->currentIndex() > 0);
  const auto newBtn = new IconButton(Font::Icon::plus_5, widget);
  newBtn->setToolTip(i18n("Create new preset from current spotlight settings."));

  const std::vector<QWidget*> widgets{m_presetCombo, deleteBtn, newBtn};
  for (const auto w : widgets) {
    w->setSizePolicy(w->sizePolicy().horizontalPolicy(), QSizePolicy::Minimum);
    hbox->addWidget(w);
  }

  hbox->setStretch(1, 1); // stretch combobox

  connect(m_presetCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), widget,
  [deleteBtn, settings, normalComboStyle, this](int index)
  {
    deleteBtn->setEnabled(index > 0);
    m_presetCombo->setStyle(index == 0 ? &*m_presetComboStyle : normalComboStyle);

    if (index > 0 && !m_presetCombo->currentText().isEmpty()) {
      settings->loadPreset(m_presetCombo->currentText());
    }
  });

  connect(newBtn, &QPushButton::clicked, this, [newBtn, settings, this]()
  {
    newBtn->setEnabled(false);
    m_presetCombo->setEditable(true);
    const auto le = m_presetCombo->lineEdit();
    le->setMaxLength(35);
    le->setCompleter(nullptr);

    connect(le, &QLineEdit::editingFinished, this, [le, settings, newBtn, this]()
    {
      auto text = le->text().trimmed();
      m_presetCombo->setEditable(false);

      if (text.isEmpty()) {
        text = m_presetCombo->currentText().trimmed();
      }

      if (m_presetCombo->findText(text) >= 0) { // Item with same name already exists
        text.append(" (%1)");
        for (int i = 2; i < 1000; ++i) {
          if (m_presetCombo->findText(text.arg(i)) < 0) {
            text = text.arg(i);
            break;
          }
        }
      }

      newBtn->setEnabled(true);
      settings->savePreset(text);
    });

    le->setText(i18n("New Preset"));
    le->setFocus();
    le->selectAll();
  });

  connect(deleteBtn, &QPushButton::clicked, this, [this, settings]()
  {
    if (m_presetCombo->currentIndex() < 0) { return; }
    settings->removePreset(m_presetCombo->currentText());
  });

  connect(settings, &Settings::presetLoaded, this,
  [normalComboStyle, deleteBtn, this](const QString& preset)
  {
    const auto idx = m_presetCombo->findText(preset);
    if (idx >= 0 && idx != m_presetCombo->currentIndex())
    {
      m_presetCombo->blockSignals(true);
      m_presetCombo->setCurrentIndex(idx);
      m_presetCombo->blockSignals(false);
      m_presetCombo->setStyle(idx == 0 ? &*m_presetComboStyle : normalComboStyle);
      deleteBtn->setEnabled(idx > 0);
    }
  });

  return widget;
}

// -------------------------------------------------------------------------------------------------
QGroupBox* PreferencesDialog::createShapeGroupBox(Settings* settings)
{
  const auto shapeGroup = new QGroupBox(i18n("Shape Settings"), this);

  const auto spotSizeSpinBox = new QSpinBox(this);
  spotSizeSpinBox->setMaximum(settings->spotSizeRange().max);
  spotSizeSpinBox->setMinimum(settings->spotSizeRange().min);
  spotSizeSpinBox->setValue(settings->spotSize());
  const auto spotsizeHBox = new QHBoxLayout;
  spotsizeHBox->addWidget(spotSizeSpinBox);
  spotsizeHBox->addWidget(new QLabel(QString("% ")+i18n("of screen height")));
  connect(spotSizeSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
          settings, &Settings::setSpotSize);
  connect(settings, &Settings::spotSizeChanged, spotSizeSpinBox, &QSpinBox::setValue);
  connect(settings, &Settings::spotSizeChanged, this, &PreferencesDialog::resetPresetCombo);

  const auto spotGrid = new QGridLayout(shapeGroup);
  spotGrid->addWidget(new QLabel(i18n("Spot Size"), this), 0, 0);
  spotGrid->addLayout(spotsizeHBox, 0, 1);

  // Spotlight shape setting
  const auto shapeCombo = new QComboBox(this);
  for (const auto& shape : settings->spotShapes()) {
    shapeCombo->addItem(shape.displayName(), shape.qmlComponent());
  }
  connect(settings, &Settings::spotShapeChanged, shapeCombo,
  [shapeCombo, this](const QString& spotShape){
    const int idx = shapeCombo->findData(spotShape);
    if (idx != -1) {
      shapeCombo->setCurrentIndex(idx);
    }
    resetPresetCombo();
  });
  emit settings->spotShapeChanged(settings->spotShape());
  spotGrid->addWidget(new QLabel(i18n("Shape"), this), 4, 0);
  spotGrid->addWidget(shapeCombo, 4, 1);

  // Spotlight rotation setting
  const auto shapeRotationSb = new QDoubleSpinBox(this);
  shapeRotationSb->setMaximum(settings->spotRotationRange().max);
  shapeRotationSb->setMinimum(settings->spotRotationRange().min);
  shapeRotationSb->setDecimals(1);
  shapeRotationSb->setSingleStep(1.0);
  shapeRotationSb->setValue(settings->spotRotation());
  connect(shapeRotationSb, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
          settings, &Settings::setSpotRotation);
  connect(settings, &Settings::spotRotationChanged, shapeRotationSb, &QDoubleSpinBox::setValue);
  connect(settings, &Settings::spotRotationChanged, this, &PreferencesDialog::resetPresetCombo);
  const auto shapeRotationLabel = new QLabel(i18n("Rotation"), this);
  spotGrid->addWidget(shapeRotationLabel, 5, 0);
  spotGrid->addWidget(shapeRotationSb, 5, 1);

  // Function for updating all spotlight shape related widgets
  auto updateShapeSettingsWidgets = [settings, shapeCombo, shapeRotationSb, shapeRotationLabel, spotGrid, this]()
  {
    if (shapeCombo->currentIndex() == -1) { return; }
    const QString shapeQml = shapeCombo->itemData(shapeCombo->currentIndex()).toString();
    const auto& shapes = settings->spotShapes();
    auto it = std::find_if(shapes.cbegin(), shapes.cend(), [&shapeQml](const Settings::SpotShape& s) {
      return shapeQml == s.qmlComponent();
    });

    constexpr int startRow = 100;
    constexpr int maxRows = 10;

    for (int row = startRow; row < startRow + maxRows; ++row) {
      if (const auto li = spotGrid->itemAtPosition(row, 0)) {
        if (const auto w = li->widget()) {
          w->hide();
          w->deleteLater();
        }
      }
      if (const auto li = spotGrid->itemAtPosition(row, 1)) {
        if (const auto w = li->widget()) {
          w->hide();
          w->deleteLater();
        }
      }
    }

    if (it != shapes.cend())
    {
      shapeRotationLabel->setVisible(it->allowRotation());
      shapeRotationSb->setVisible(it->allowRotation());
      const auto& shape = *it;
      int row = startRow;
      for (const auto& s : it->shapeSettings())
      {
        if (row >= startRow + maxRows) { break; }
        spotGrid->addWidget(new QLabel(s.displayName(), this),row, 0);
        if (s.defaultValue().metaType().id() == QMetaType::Int)
        {
          const auto spinbox = new QSpinBox(this);
          spinbox->setMaximum(s.maxValue().toInt());
          spinbox->setMinimum(s.minValue().toInt());
          spinbox->setValue(s.defaultValue().toInt());
          spotGrid->addWidget(spinbox, row, 1);

          const auto pm = settings->shapeSettings(shape.name());
          if (pm && pm->property(s.settingsKey().toLocal8Bit()).isValid())
          {
            spinbox->setValue(pm->property(s.settingsKey().toLocal8Bit()).toInt());
            connect(spinbox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), pm,
            [s, pm](int newValue){
              pm->setProperty(s.settingsKey().toLocal8Bit(), newValue);
            });
            connect(pm, &QQmlPropertyMap::valueChanged, spinbox,
            [s, spinbox, this](const QString& key, const QVariant& value)
            {
              if (key != s.settingsKey() || !value.isValid()) { return; }
              spinbox->setValue(value.toInt());
              resetPresetCombo();
            });
          }
        }
        ++row;
      }
    }
  };

  connect(shapeCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
  [settings, shapeCombo, updateShapeSettingsWidgets](int index)
  {
    const QString shapeQml = shapeCombo->itemData(index).toString();
    settings->setSpotShape(shapeQml);
    updateShapeSettingsWidgets();
  });

  updateShapeSettingsWidgets();

  spotGrid->addWidget(new QWidget(this), 200, 0);
  spotGrid->setRowStretch(200, 200);

  spotGrid->setColumnStretch(1, 1);
  return shapeGroup;
}

// -------------------------------------------------------------------------------------------------
QGroupBox* PreferencesDialog::createSpotGroupBox(Settings* settings)
{
  const auto spotGroup = new QGroupBox(i18n("Show Spotlight Shade"), this);
  spotGroup->setCheckable(true);
  spotGroup->setChecked(settings->showSpotShade());
  connect(spotGroup, &QGroupBox::toggled, settings, &Settings::setShowSpotShade);
  connect(settings, &Settings::showSpotShadeChanged, spotGroup, &QGroupBox::setChecked);
  connect(settings, &Settings::showSpotShadeChanged, this, &PreferencesDialog::resetPresetCombo);

  const auto spotGrid = new QGridLayout(spotGroup);

  // Shade color setting
  const auto shadeColor = new KColorButton(settings->shadeColor(), this);
  shadeColor->setAccessibleName(i18n("Shade Color"));
  connect(shadeColor, &KColorButton::changed, settings, &Settings::setShadeColor);
  connect(settings, &Settings::shadeColorChanged, shadeColor, &KColorButton::setColor);
  connect(settings, &Settings::shadeColorChanged, this, &PreferencesDialog::resetPresetCombo);
  spotGrid->addWidget(new QLabel(i18n("Shade Color"), this), 1, 0);
  spotGrid->addWidget(shadeColor, 1, 1);

  // Spotlight shade opacity setting
  const auto shadeOpacitySb = new QDoubleSpinBox(this);
  shadeOpacitySb->setMaximum(settings->shadeOpacityRange().max);
  shadeOpacitySb->setMinimum(settings->shadeOpacityRange().min);
  shadeOpacitySb->setDecimals(2);
  shadeOpacitySb->setSingleStep(0.1);
  shadeOpacitySb->setValue(settings->shadeOpacity());
  connect(shadeOpacitySb, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
          settings, &Settings::setShadeOpacity);
  connect(settings, &Settings::shadeOpacityChanged, shadeOpacitySb, &QDoubleSpinBox::setValue);
  connect(settings, &Settings::shadeOpacityChanged, this, &PreferencesDialog::resetPresetCombo);
  spotGrid->addWidget(new QLabel(i18n("Shade Opacity"), this), 2, 0);
  spotGrid->addWidget(shadeOpacitySb, 2, 1);

  spotGrid->addWidget(new QWidget(this), 100, 0);
  spotGrid->setRowStretch(100, 100);

  spotGrid->setColumnStretch(1, 1);
  return spotGroup;
}

// -------------------------------------------------------------------------------------------------
QGroupBox* PreferencesDialog::createDotGroupBox(Settings* settings)
{
  const auto dotGroup = new QGroupBox(i18n("Show Center Dot"), this);
  dotGroup->setCheckable(true);
  dotGroup->setChecked(settings->showCenterDot());
  connect(dotGroup, &QGroupBox::toggled, settings, &Settings::setShowCenterDot);
  connect(settings, &Settings::showCenterDotChanged, dotGroup, &QGroupBox::setChecked);
  connect(settings, &Settings::showCenterDotChanged, this, &PreferencesDialog::resetPresetCombo);

  const auto dotSizeSpinBox = new QSpinBox(this);
  dotSizeSpinBox->setMaximum(settings->dotSizeRange().max);
  dotSizeSpinBox->setMinimum(settings->dotSizeRange().min);
  dotSizeSpinBox->setValue(settings->dotSize());
  auto dotsizeHBox = new QHBoxLayout;
  dotsizeHBox->addWidget(dotSizeSpinBox);
  dotsizeHBox->addWidget(new QLabel(i18n("pixel")));
  connect(dotSizeSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
          settings, &Settings::setDotSize);
  connect(settings, &Settings::dotSizeChanged, dotSizeSpinBox, &QSpinBox::setValue);
  connect(settings, &Settings::dotSizeChanged, this, &PreferencesDialog::resetPresetCombo);

  const auto dotGrid = new QGridLayout(dotGroup);
  dotGrid->addWidget(new QLabel(i18n("Dot Size"), this), 0, 0);
  dotGrid->addLayout(dotsizeHBox, 0, 1);

  const auto dotColor = new KColorButton(settings->dotColor(), this);
  dotColor->setAccessibleName(i18n("Dot Color"));
  connect(dotColor, &KColorButton::changed, settings, &Settings::setDotColor);
  connect(settings, &Settings::dotColorChanged, dotColor, &KColorButton::setColor);
  connect(settings, &Settings::dotColorChanged, this, &PreferencesDialog::resetPresetCombo);
  dotGrid->addWidget(new QLabel(i18n("Dot Color"), this), 1, 0);
  dotGrid->addWidget(dotColor, 1, 1);


  // Spotlight dot opacity setting
  const auto dotOpacitySb = new QDoubleSpinBox(this);
  dotOpacitySb->setMaximum(settings->dotOpacityRange().max);
  dotOpacitySb->setMinimum(settings->dotOpacityRange().min);
  dotOpacitySb->setDecimals(2);
  dotOpacitySb->setSingleStep(0.1);
  dotOpacitySb->setValue(settings->dotOpacity());
  connect(dotOpacitySb, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
          settings, &Settings::setDotOpacity);
  connect(settings, &Settings::borderOpacityChanged, dotOpacitySb, &QDoubleSpinBox::setValue);
  connect(settings, &Settings::borderOpacityChanged, this, &PreferencesDialog::resetPresetCombo);
  dotGrid->addWidget(new QLabel(i18n("Dot Opacity"), this), 2, 0);
  dotGrid->addWidget(dotOpacitySb, 2, 1);

  dotGrid->addWidget(new QWidget(this), 100, 0);
  dotGrid->setRowStretch(100, 100);

  dotGrid->setColumnStretch(1, 1);
  return dotGroup;
}

// -------------------------------------------------------------------------------------------------
QGroupBox* PreferencesDialog::createBorderGroupBox(Settings* settings)
{
  const auto borderGroup = new QGroupBox(i18n("Show Border"), this);
  borderGroup->setCheckable(true);
  borderGroup->setChecked(settings->showBorder());
  connect(borderGroup, &QGroupBox::toggled, settings, &Settings::setShowBorder);
  connect(settings, &Settings::showBorderChanged, borderGroup, &QGroupBox::setChecked);
  connect(settings, &Settings::showBorderChanged, this, &PreferencesDialog::resetPresetCombo);

  const auto borderSizeSpinBox = new QSpinBox(this);
  borderSizeSpinBox->setMaximum(settings->borderSizeRange().max);
  borderSizeSpinBox->setMinimum(settings->borderSizeRange().min);
  borderSizeSpinBox->setValue(settings->borderSize());
  auto bordersizeHBox = new QHBoxLayout;
  bordersizeHBox->addWidget(borderSizeSpinBox);
  bordersizeHBox->addWidget(new QLabel(i18n("% of spotsize")));
  connect(borderSizeSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
          settings, &Settings::setBorderSize);
  connect(settings, &Settings::borderSizeChanged, borderSizeSpinBox, &QSpinBox::setValue);
  connect(settings, &Settings::borderSizeChanged, this, &PreferencesDialog::resetPresetCombo);

  const auto borderGrid = new QGridLayout(borderGroup);
  borderGrid->addWidget(new QLabel(i18n("Border Size"), this), 0, 0);
  borderGrid->addLayout(bordersizeHBox, 0, 1);

  const auto borderColor = new KColorButton(settings->borderColor(), this);
  borderColor->setAccessibleName(i18n("Border Color"));
  connect(borderColor, &KColorButton::changed, settings, &Settings::setBorderColor);
  connect(settings, &Settings::borderColorChanged, borderColor, &KColorButton::setColor);
  connect(settings, &Settings::borderColorChanged, this, &PreferencesDialog::resetPresetCombo);
  borderGrid->addWidget(new QLabel(i18n("Border Color"), this), 1, 0);
  borderGrid->addWidget(borderColor, 1, 1);

  // Spotlight border opacity setting
  const auto borderOpacitySb = new QDoubleSpinBox(this);
  borderOpacitySb->setMaximum(settings->borderOpacityRange().max);
  borderOpacitySb->setMinimum(settings->borderOpacityRange().min);
  borderOpacitySb->setDecimals(2);
  borderOpacitySb->setSingleStep(0.1);
  borderOpacitySb->setValue(settings->borderOpacity());
  connect(borderOpacitySb, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
          settings, &Settings::setBorderOpacity);
  connect(settings, &Settings::borderOpacityChanged, borderOpacitySb, &QDoubleSpinBox::setValue);
  connect(settings, &Settings::borderOpacityChanged, this, &PreferencesDialog::resetPresetCombo);
  borderGrid->addWidget(new QLabel(i18n("Border Opacity"), this), 2, 0);
  borderGrid->addWidget(borderOpacitySb, 2, 1);

  borderGrid->addWidget(new QWidget(this), 100, 0);
  borderGrid->setRowStretch(100, 100);

  borderGrid->setColumnStretch(1, 1);
  return borderGroup;
}

// -------------------------------------------------------------------------------------------------
QGroupBox* PreferencesDialog::createZoomGroupBox(Settings* settings)
{
  const auto zoomGroup = new QGroupBox(i18n("Enable Zoom"), this);
  zoomGroup->setCheckable(true);
  zoomGroup->setChecked(settings->zoomEnabled());
  connect(zoomGroup, &QGroupBox::toggled, settings, &Settings::setZoomEnabled);
  connect(settings, &Settings::zoomEnabledChanged, zoomGroup, &QGroupBox::setChecked);
  connect(settings, &Settings::zoomEnabledChanged, this, &PreferencesDialog::resetPresetCombo);

  const auto zoomGrid = new QGridLayout(zoomGroup);

  // zoom level setting
  const auto zoomLevelSb = new QDoubleSpinBox(this);
  zoomLevelSb->setMaximum(settings->zoomFactorRange().max);
  zoomLevelSb->setMinimum(settings->zoomFactorRange().min);
  zoomLevelSb->setDecimals(2);
  zoomLevelSb->setSingleStep(0.1);
  zoomLevelSb->setValue(settings->zoomFactor());
  connect(zoomLevelSb, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
          settings, &Settings::setZoomFactor);
  connect(settings, &Settings::zoomFactorChanged, zoomLevelSb, &QDoubleSpinBox::setValue);
  connect(settings, &Settings::zoomFactorChanged, this, &PreferencesDialog::resetPresetCombo);
  zoomGrid->addWidget(new QLabel(i18n("Zoom Level"), this), 0, 0);
  zoomGrid->addWidget(zoomLevelSb, 0, 1);
  zoomGrid->setColumnStretch(1, 1);
  return zoomGroup;
}

// -------------------------------------------------------------------------------------------------
QGroupBox* PreferencesDialog::createCursorGroupBox(Settings* settings)
{
  const auto cursorGroup = new QGroupBox(i18n("Cursor Settings"), this);
  cursorGroup->setCheckable(false);
  const auto grid = new QGridLayout(cursorGroup);

  const auto cursorCb = new QComboBox(this);
  for (const auto& item : cursorMap) {
    cursorCb->addItem(
      QIcon(item.first), item.second.first.toString(), static_cast<int>(item.second.second));
  }
  connect(settings, &Settings::cursorChanged, cursorCb, [cursorCb, this](int cursor){
    const int idx = cursorCb->findData(cursor);
    cursorCb->setCurrentIndex((idx == -1) ? Qt::BlankCursor : idx);
    resetPresetCombo();
  });
  emit settings->cursorChanged(settings->cursor()); // set initial value
  connect(cursorCb, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
  [settings, cursorCb](int index) {
    settings->setCursor(static_cast<Qt::CursorShape>(cursorCb->itemData(index).toInt()));
  });

  grid->addWidget(new QLabel(i18n("Cursor"), this), 0, 0);
  grid->addWidget(cursorCb, 0, 1);
  grid->setColumnStretch(1, 1);
  return cursorGroup;
}

// -------------------------------------------------------------------------------------------------
QWidget* PreferencesDialog::createMultiScreenWidget(Settings* settings)
{
  const auto cb = new QCheckBox(i18n("Enable multi-screen overlay"), this);
  cb->setChecked(settings->multiScreenOverlayEnabled());
  connect(cb, &QCheckBox::toggled, settings, &Settings::setMultiScreenOverlayEnabled);
  connect(settings, &Settings::multiScreenOverlayEnabledChanged, cb, &QCheckBox::setChecked);
  connect(settings, &Settings::multiScreenOverlayEnabledChanged, this, &PreferencesDialog::resetPresetCombo);
  return cb;
}

// -------------------------------------------------------------------------------------------------
QWidget* PreferencesDialog::createLogTabWidget()
{
  const auto widget = new QWidget(this);
  const auto mainVBox = new QVBoxLayout(widget);

  const auto te = new QPlainTextEdit(widget);
  te->setReadOnly(true);
  te->setWordWrapMode(QTextOption::NoWrap);
  te->setMaximumBlockCount(1000);
  te->setFont([te]()
  {
    auto font = te->font();
    font.setPointSize(font.pointSize() - 1);
    return font;
  }());
  logging::registerTextEdit(te);

  // Count discarded logs
  connect(te, &QPlainTextEdit::blockCountChanged, this,
  [maxBlockCount=te->maximumBlockCount(), this](int newBlockCount) {
    if (newBlockCount > maxBlockCount) {
      m_discardedLogCount += (newBlockCount-maxBlockCount);
    }
  });

  const auto lvlHBox = new QHBoxLayout();
  lvlHBox->addWidget(new QLabel(i18n("Log Level"), widget));
  // Log level combo box
  const auto logLvlCombo = new QComboBox(widget);
  logLvlCombo->addItem(i18n("Debug"), static_cast<int>(logging::level::debug));
  logLvlCombo->addItem(i18n("Info"), static_cast<int>(logging::level::info));
  logLvlCombo->addItem(i18n("Warning"), static_cast<int>(logging::level::warning));
  logLvlCombo->addItem(i18n("Error"), static_cast<int>(logging::level::error));
  lvlHBox->addWidget(logLvlCombo);

  const int idx = logLvlCombo->findData(static_cast<int>(logging::currentLevel()));
  logLvlCombo->setCurrentIndex((idx == -1) ? 0 : idx);

  connect(logLvlCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
  [logLvlCombo, te](int index) {
    const auto lvl = static_cast<logging::level>(logLvlCombo->itemData(index).toInt());
    te->appendPlainText(i18n("--- Setting new log level: %1", logging::levelToString(lvl)));
    logging::setCurrentLevel(lvl);
  });

  const auto saveLogBtn = new QPushButton(i18n("&Save log..."), this);
  saveLogBtn->setToolTip(i18n("Save log to file."));
  connect(saveLogBtn, &QPushButton::clicked, this, [this, te]()
  {
    static auto saveDir = QDir::homePath();
    const auto defaultName = QString("projecteur_%1.log")
                             .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm"));

    const auto defaultFile = QDir(saveDir).filePath(defaultName);
    KFileCustomDialog saveDialog(QUrl::fromLocalFile(defaultFile), this);
    saveDialog.setWindowTitle(i18n("Save Log File"));
    saveDialog.setOperationMode(KFileWidget::Saving);
    saveDialog.fileWidget()->setMode(KFile::File | KFile::LocalOnly);
    saveDialog.fileWidget()->setConfirmOverwrite(true);
    saveDialog.fileWidget()->setFilters({
      KFileFilter(i18n("Log Files"), {QStringLiteral("*.log"), QStringLiteral("*.txt")}, {})
    });
    if (saveDialog.exec() != QDialog::Accepted) { return; }

    const auto logFile = saveDialog.fileWidget()->selectedFile();
    if (logFile.isEmpty()) { return; }
    saveDir = QFileInfo(logFile).path();

    QFile f(logFile);
    if (f.open(QIODevice::WriteOnly))
    {
      f.write(QString("%1 %2\n").arg(QCoreApplication::applicationName())
              .arg(projecteur::version_string()).toLocal8Bit());
      f.write(QString(" - git-branch: %1, git-hash: %2\n").arg(projecteur::version_branch())
              .arg(projecteur::version_shorthash()).toLocal8Bit());
      f.write(QString(" - qt-version: (build: %1, runtime: %2)\n").arg(QT_VERSION_STR)
              .arg(qVersion()).toLocal8Bit());
      f.write(QString("\n------------------------------------------------------------\n").toLocal8Bit());
      if (m_discardedLogCount > 0) {
        f.write(i18np("Discarded one previous log entry.",
                     "Discarded %1 previous log entries.", m_discardedLogCount).toLocal8Bit());
        f.write(QString("\n------------------------------------------------------------\n").toLocal8Bit());
      }
      f.write(te->toPlainText().toLocal8Bit());
      logInfo(preferences) << i18n("Log saved to: ") << logFile;
    }
    else {
      logError(preferences) << i18n("Could not open '%1' for writing.", logFile);
    }
  });

  lvlHBox->addWidget(saveLogBtn);
  lvlHBox->setStretch(0, 0);
  lvlHBox->setStretch(1, 1);
  lvlHBox->setStretch(2, 1);

  mainVBox->addLayout(lvlHBox);
  mainVBox->addWidget(te);
  return widget;
}

// -------------------------------------------------------------------------------------------------
void PreferencesDialog::setMode(Mode dialogMode)
{
  if (m_dialogMode == dialogMode) {
    return;
  }

  setDialogMode(dialogMode);
}

// -------------------------------------------------------------------------------------------------
void PreferencesDialog::setDialogMode(Mode dialogMode)
{
  m_dialogMode = dialogMode;

  if (dialogMode == Mode::ClosableDialog)
  {
    setWindowFlags(Qt::Dialog);
  }
  else if (dialogMode == Mode::MinimizeOnlyDialog)
  {
    setWindowFlags(Qt::Window);
    setWindowFlags(windowFlags() & ~Qt::WindowMaximizeButtonHint);
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);
  }
}

// -------------------------------------------------------------------------------------------------
void PreferencesDialog::settingsModified()
{
  if (isVisible()) {
    updateButtons();
  } else {
    m_appliedSpotlightSettings = m_settings->spotlightSettings();
  }
}

// -------------------------------------------------------------------------------------------------
void PreferencesDialog::restoreAppliedSettings()
{
  m_shortcutsEditor->undo();
  m_settings->setSpotlightSettings(m_appliedSpotlightSettings);
  resetPresetCombo();
  updateButtons();
}

// -------------------------------------------------------------------------------------------------
bool PreferencesDialog::shortcutsAreDefault() const
{
  for (const auto* action : m_actionCollection->actions()) {
    if (KGlobalAccel::self()->shortcut(action)
        != KGlobalAccel::self()->defaultShortcut(action)) {
      return false;
    }
  }
  return true;
}

// -------------------------------------------------------------------------------------------------
void PreferencesDialog::updateSettings()
{
  KConfigDialog::updateSettings();
  m_shortcutsEditor->save();
  m_appliedSpotlightSettings = m_settings->spotlightSettings();
}

// -------------------------------------------------------------------------------------------------
void PreferencesDialog::updateWidgets()
{
  KConfigDialog::updateWidgets();
  restoreAppliedSettings();
}

// -------------------------------------------------------------------------------------------------
void PreferencesDialog::updateWidgetsDefault()
{
  KConfigDialog::updateWidgetsDefault();
  m_settings->setDefaults();
  m_shortcutsEditor->allDefault();
  resetPresetCombo();
}

// -------------------------------------------------------------------------------------------------
bool PreferencesDialog::hasChanged()
{
  return KConfigDialog::hasChanged()
         || m_settings->spotlightSettings() != m_appliedSpotlightSettings
         || m_shortcutsEditor->isModified();
}

// -------------------------------------------------------------------------------------------------
bool PreferencesDialog::isDefault()
{
  return KConfigDialog::isDefault()
         && m_settings->spotlightSettings() == Settings::defaultSpotlightSettings()
         && shortcutsAreDefault();
}

// -------------------------------------------------------------------------------------------------
void PreferencesDialog::accept()
{
  if (m_dialogMode == Mode::MinimizeOnlyDialog) {
    updateSettings();
    updateButtons();
    showMinimized();
    return;
  }
  KConfigDialog::accept();
}

// -------------------------------------------------------------------------------------------------
void PreferencesDialog::reject()
{
  restoreAppliedSettings();
  if (m_dialogMode == Mode::MinimizeOnlyDialog) {
    showMinimized();
    return;
  }
  KConfigDialog::reject();
}

// -------------------------------------------------------------------------------------------------
void PreferencesDialog::resetPresetCombo()
{
  if (m_presetCombo) { m_presetCombo->setCurrentIndex(0); }
}

// -------------------------------------------------------------------------------------------------
void PreferencesDialog::setDialogActive(bool active)
{
  if (active == m_active) {
    return;
  }

  m_active = active;
  emit dialogActiveChanged(active);
}

// -------------------------------------------------------------------------------------------------
bool PreferencesDialog::event(QEvent* e)
{
  if (e->type() == QEvent::WindowActivate) {
    setDialogActive(true);
  }
  else if (e->type() == QEvent::WindowDeactivate) {
    setDialogActive(false);
  }
  return KConfigDialog::event(e);
}

// -------------------------------------------------------------------------------------------------
void PreferencesDialog::closeEvent(QCloseEvent* e)
{
  if (m_dialogMode == Mode::MinimizeOnlyDialog) {
    emit exitApplicationRequested();
    return;
  }
  restoreAppliedSettings();
  KConfigDialog::closeEvent(e);
}

// -------------------------------------------------------------------------------------------------
void PreferencesDialog::keyPressEvent(QKeyEvent* e)
{
  if (m_dialogMode == Mode::MinimizeOnlyDialog)
  {
    if (e->key() == Qt::Key_Escape)
    {
      this->showMinimized();
      return;
    }
  }
  KConfigDialog::keyPressEvent(e);
}

// -------------------------------------------------------------------------------------------------
void PresetComboCustomStyle::drawControl(QStyle::ControlElement element, const QStyleOption* option,
                                         QPainter* painter, const QWidget* widget) const
{
  if (element == QStyle::CE_ComboBoxLabel)
  {
    auto fnt = painter->font();
    fnt.setItalic(true);
    painter->setFont(fnt);

    if (option->type == QStyleOption::SO_ComboBox)
    {
      auto custom = *static_cast<const QStyleOptionComboBox*>(option);
      custom.palette.setColor(QPalette::ButtonText,
                              option->palette.color(QPalette::Disabled, QPalette::ButtonText));
      QProxyStyle::drawControl(element, &custom, painter, widget);
      return;
    }
  }
  QProxyStyle::drawControl(element, option, painter, widget);
}
