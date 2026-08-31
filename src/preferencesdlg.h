// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md
#pragma once

#include <KConfigDialog>

#include <QProxyStyle>
#include <QToolButton>
#include <QVariantMap>

#include <memory>

class QComboBox;
class QGroupBox;
class KActionCollection;
class KShortcutsEditor;
class Settings;
class Spotlight;
class DevicesWidget;

// -------------------------------------------------------------------------------------------------
class PresetComboCustomStyle : public QProxyStyle
{
public:
  void drawControl(QStyle::ControlElement element, const QStyleOption* option,
                   QPainter* painter, const QWidget* widget = nullptr) const override;
};

// -------------------------------------------------------------------------------------------------
class PreferencesDialog : public KConfigDialog
{
  Q_OBJECT

public:
  enum class Mode : uint8_t{
    ClosableDialog,
    MinimizeOnlyDialog
  };

  explicit PreferencesDialog(Settings* settings, Spotlight* spotlight,
                             KActionCollection* actionCollection,
                             Mode = Mode::ClosableDialog, QWidget* parent = nullptr);
  virtual ~PreferencesDialog() override = default;

  bool dialogActive() const { return m_active; }
  Mode mode() const { return m_dialogMode; }
  void setMode(Mode dialogMode);

public slots:
  void accept() override;
  void reject() override;

signals:
  void dialogActiveChanged(bool active);
  void testButtonClicked();
  void exitApplicationRequested();

protected slots:
  void updateSettings() override;
  void updateWidgets() override;
  void updateWidgetsDefault() override;

protected:
  bool event(QEvent* event) override;
  void closeEvent(QCloseEvent* e) override;
  void keyPressEvent(QKeyEvent* e) override;
  bool hasChanged() override;
  bool isDefault() override;

private:
  void setDialogActive(bool active);
  void setDialogMode(Mode dialogMode);
  void settingsModified();
  void restoreAppliedSettings();
  bool shortcutsAreDefault() const;
  void resetPresetCombo();

  QWidget* createSettingsTabWidget(Settings* settings);
  QWidget* createLaserTabWidget(Settings* settings);
  QGroupBox* createPointerModeGroupBox(Settings* settings);
  QGroupBox* createLaserDotGroupBox(Settings* settings);
  QGroupBox* createLaserGlowGroupBox(Settings* settings);
  QGroupBox* createLaserTrailGroupBox(Settings* settings);
  QGroupBox* createShapeGroupBox(Settings* settings);
  QGroupBox* createSpotGroupBox(Settings* settings);
  QGroupBox* createDotGroupBox(Settings* settings);
  QGroupBox* createBorderGroupBox(Settings* settings);
  QGroupBox* createCursorGroupBox(Settings* settings);
  QWidget* createMultiScreenWidget(Settings* settings);
  QGroupBox* createZoomGroupBox(Settings* settings);
  QWidget* createPresetSelector(Settings* settings);

private:
  Settings* const m_settings;
  KActionCollection* const m_actionCollection;
  QVariantMap m_appliedSpotlightSettings;
  std::unique_ptr<PresetComboCustomStyle> m_presetComboStyle;
  QComboBox* m_presetCombo = nullptr;
  std::vector<QComboBox*> m_presetCombos;
  DevicesWidget* m_deviceswidget = nullptr;
  KShortcutsEditor* m_shortcutsEditor = nullptr;
  bool m_active = false;
  Mode m_dialogMode = Mode::ClosableDialog;
};
