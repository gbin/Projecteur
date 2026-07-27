// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md
#pragma once

#include <QDialog>
#include <QProxyStyle>
#include <QToolButton>

#include <memory>

class QComboBox;
class QGroupBox;
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
class PreferencesDialog : public QDialog
{
  Q_OBJECT

public:
  enum class Mode : uint8_t{
    ClosableDialog,
    MinimizeOnlyDialog
  };

  explicit PreferencesDialog(Settings* settings, Spotlight* spotlight,
                             Mode = Mode::ClosableDialog, QWidget* parent = nullptr);
  virtual ~PreferencesDialog() override = default;

  bool dialogActive() const { return m_active; }
  Mode mode() const { return m_dialogMode; }
  void setMode(Mode dialogMode);

signals:
  void dialogActiveChanged(bool active);
  void testButtonClicked();
  void exitApplicationRequested();

protected:
  virtual bool event(QEvent* event) override;
  virtual void closeEvent(QCloseEvent* e) override;
  virtual void keyPressEvent(QKeyEvent* e) override;

private:
  void setDialogActive(bool active);
  void setDialogMode(Mode dialogMode);
  void resetPresetCombo();

  QWidget* createSettingsTabWidget(Settings* settings);
  QGroupBox* createShapeGroupBox(Settings* settings);
  QGroupBox* createSpotGroupBox(Settings* settings);
  QGroupBox* createDotGroupBox(Settings* settings);
  QGroupBox* createBorderGroupBox(Settings* settings);
  QGroupBox* createCursorGroupBox(Settings* settings);
  QWidget* createMultiScreenWidget(Settings* settings);
  QGroupBox* createZoomGroupBox(Settings* settings);
  QWidget* createPresetSelector(Settings* settings);
  QWidget* createLogTabWidget();

private:
  std::unique_ptr<PresetComboCustomStyle> m_presetComboStyle;
  QComboBox* m_presetCombo = nullptr;
  QPushButton* m_closeMinimizeBtn = nullptr;
  DevicesWidget* m_deviceswidget = nullptr;
  bool m_active = false;
  Mode m_dialogMode = Mode::ClosableDialog;
  quint32 m_discardedLogCount = 0;
};
