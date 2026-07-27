// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.extras as PlasmaExtras
import org.kde.plasma.plasmoid

PlasmaExtras.Representation {
    id: root

    required property var backend
    required property PlasmoidItem plasmoidItem

    implicitWidth: Kirigami.Units.gridUnit * 22
    implicitHeight: content.implicitHeight + header.implicitHeight
    focus: true
    collapseMarginsHint: true

    ColumnLayout {
        id: content

        spacing: Kirigami.Units.largeSpacing

        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            margins: Kirigami.Units.largeSpacing
        }

        PlasmaExtras.Heading {
            Layout.fillWidth: true
            level: 4
            text: i18n("Connected presenters")
        }

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            visible: !root.backend || root.backend.connectedDevices.length === 0
            text: i18n("No compatible presenter connected")
            opacity: 0.7
            wrapMode: Text.WordWrap
        }

        Repeater {
            model: root.backend ? root.backend.connectedDevices : []

            delegate: RowLayout {
                required property string modelData

                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    source: "input-mouse-symbolic"
                    implicitWidth: Kirigami.Units.iconSizes.smallMedium
                    implicitHeight: implicitWidth
                }

                PlasmaComponents3.Label {
                    Layout.fillWidth: true
                    text: modelData
                    elide: Text.ElideRight
                }

            }

        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            PlasmaComponents3.Label {
                text: i18n("Preset:")
            }

            PlasmaComponents3.ComboBox {
                id: presetCombo

                Layout.fillWidth: true
                enabled: root.backend && root.backend.serviceAvailable
                model: [i18n("Current Settings")].concat(root.backend ? root.backend.presets : [])
                currentIndex: {
                    if (!root.backend || root.backend.currentPreset.length === 0)
                        return 0;

                    const index = root.backend.presets.indexOf(root.backend.currentPreset);
                    return index < 0 ? 0 : index + 1;
                }
                onActivated: (index) => {
                    if (index > 0)
                        root.backend.loadPreset(root.backend.presets[index - 1]);

                }
            }

        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            PlasmaComponents3.Button {
                Layout.fillWidth: true
                enabled: root.backend && root.backend.serviceAvailable
                text: root.backend && root.backend.spotlightActive ? i18n("Hide Spotlight") : i18n("Test Spotlight")
                icon.name: root.backend && root.backend.spotlightActive ? "visibility-hidden-symbolic" : "visibility-symbolic"
                onClicked: root.backend.setSpotlightActive(!root.backend.spotlightActive)
            }

            PlasmaComponents3.Button {
                Layout.fillWidth: true
                enabled: root.backend && root.backend.serviceAvailable
                text: i18n("Preferences…")
                icon.name: "configure-symbolic"
                onClicked: {
                    root.plasmoidItem.expanded = false;
                    root.backend.showPreferences();
                }
            }

        }

    }

    header: PlasmaExtras.PlasmoidHeading {
        id: header

        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            PlasmaComponents3.Switch {
                text: i18n("Enable Spotlight")
                icon.name: "projecteur"
                checked: root.backend ? root.backend.overlayEnabled : false
                enabled: root.backend && root.backend.serviceAvailable
                onToggled: root.backend.setOverlayEnabled(checked)
            }

            Item {
                Layout.fillWidth: true
            }

            PlasmaComponents3.ToolButton {
                text: i18n("About Projecteur")
                icon.name: "help-about-symbolic"
                display: PlasmaComponents3.AbstractButton.IconOnly
                onClicked: {
                    root.plasmoidItem.expanded = false;
                    root.backend.showAbout();
                }

                PlasmaComponents3.ToolTip {
                    text: parent.text
                }

            }

            PlasmaComponents3.ToolButton {
                text: i18n("Quit Projecteur")
                icon.name: "application-exit-symbolic"
                display: PlasmaComponents3.AbstractButton.IconOnly
                onClicked: {
                    root.plasmoidItem.expanded = false;
                    root.backend.quitProjecteur();
                }

                PlasmaComponents3.ToolTip {
                    text: parent.text
                }

            }

        }

    }

}
