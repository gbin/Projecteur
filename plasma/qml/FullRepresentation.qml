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

    function batteryLevel(index) {
        if (!root.backend || index >= root.backend.connectedDeviceBatteryLevels.length)
            return -1;

        return root.backend.connectedDeviceBatteryLevels[index];
    }

    function batteryStatus(index) {
        if (!root.backend || index >= root.backend.connectedDeviceBatteryStatuses.length)
            return "";

        return root.backend.connectedDeviceBatteryStatuses[index];
    }

    function batteryIconName(index) {
        const level = batteryLevel(index);
        const status = batteryStatus(index);
        if (status === "invalid-battery" || status === "thermal-error" || status === "charging-error")
            return "dialog-warning-symbolic";

        if (level < 0)
            return "";

        const charging = status === "charging" || status === "almost-full" || status === "slow-charging";
        const suffix = charging ? "-charging-symbolic" : "-symbolic";
        if (level <= 5)
            return "battery-empty" + suffix;
        if (level <= 10)
            return "battery-caution" + suffix;
        if (level <= 20)
            return "battery-low" + suffix;

        const roundedLevel = Math.min(100, Math.max(0, Math.round(level / 10) * 10));
        return "battery-" + roundedLevel.toString().padStart(3, "0") + suffix;
    }

    function batteryToolTip(index) {
        const level = batteryLevel(index);
        const status = batteryStatus(index);
        if (status === "invalid-battery")
            return i18n("Battery error");
        if (status === "thermal-error")
            return i18n("Battery temperature error");
        if (status === "charging-error")
            return i18n("Battery charging error");
        if (level < 0)
            return "";

        let state = "";
        if (status === "charging")
            state = i18n("Charging");
        else if (status === "almost-full")
            state = i18n("Almost full");
        else if (status === "full")
            state = i18n("Full");
        else if (status === "slow-charging")
            state = i18n("Charging slowly");
        else if (status === "discharging")
            state = i18n("Discharging");

        return state.length > 0 ? i18n("Battery: %1% (%2)", level, state)
                                : i18n("Battery: %1%", level);
    }

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
                required property int index
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

                Kirigami.Icon {
                    id: batteryIcon

                    readonly property string iconName: root.batteryIconName(index)
                    readonly property string toolTipText: root.batteryToolTip(index)

                    visible: iconName.length > 0
                    source: iconName
                    implicitWidth: Kirigami.Units.iconSizes.smallMedium
                    implicitHeight: implicitWidth
                    Accessible.name: toolTipText

                    MouseArea {
                        id: batteryHover

                        anchors.fill: parent
                        acceptedButtons: Qt.NoButton
                        hoverEnabled: true
                    }

                    PlasmaComponents3.ToolTip {
                        text: batteryIcon.toolTipText
                        visible: batteryHover.containsMouse && text.length > 0
                    }
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

        }

    }

}
