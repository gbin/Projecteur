// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

Kirigami.Icon {
    id: compact

    required property var backend
    required property PlasmoidItem plasmoidItem

    readonly property bool timerUrgent: backend
        && backend.timerState === "running"
        && backend.timerRemainingSeconds < 60
    readonly property bool timerWarning: backend
        && backend.timerState === "running"
        && backend.timerRemainingSeconds <= 5 * 60
    readonly property color badgeAccentColor: {
        if (backend && (backend.timerState === "completed" || compact.timerUrgent))
            return Kirigami.Theme.negativeTextColor;
        if (compact.timerWarning)
            return Kirigami.Theme.neutralTextColor;
        return Kirigami.Theme.activeTextColor;
    }

    Layout.minimumWidth: {
        if (Plasmoid.formFactor === PlasmaCore.Types.Horizontal)
            return height;
        return 0;
    }
    Layout.minimumHeight: {
        if (Plasmoid.formFactor === PlasmaCore.Types.Vertical)
            return width;
        return 0;
    }

    source: Plasmoid.icon || "projecteur"
    active: pointerArea.containsMouse
    activeFocusOnTab: true

    Accessible.name: Plasmoid.title
    Accessible.description: plasmoidItem.toolTipSubText
    Accessible.role: Accessible.Button

    Keys.onPressed: event => {
        switch (event.key) {
        case Qt.Key_Space:
        case Qt.Key_Enter:
        case Qt.Key_Return:
        case Qt.Key_Select:
            Plasmoid.activated();
            event.accepted = true;
            break;
        }
    }

    MouseArea {
        id: pointerArea

        property bool wasExpanded: false

        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
        hoverEnabled: true

        onPressed: wasExpanded = compact.plasmoidItem.expanded
        onClicked: mouse => {
            if (mouse.button === Qt.MiddleButton)
                Plasmoid.secondaryActivated();
            else
                compact.plasmoidItem.expanded = !wasExpanded;
        }
    }

    Rectangle {
        id: timerBadge

        readonly property int badgePadding: Math.max(2, Math.round(Kirigami.Units.smallSpacing / 2))

        visible: compact.plasmoidItem.badgeText.length > 0
        z: 1
        x: width >= compact.width ? (compact.width - width) / 2 : compact.width - width
        y: compact.height - height
        implicitWidth: Math.max(implicitHeight, badgeLabel.implicitWidth + badgePadding * 2)
        implicitHeight: badgeLabel.implicitHeight + 2
        radius: height / 2
        color: Kirigami.ColorUtils.tintWithAlpha(
            Kirigami.Theme.backgroundColor, compact.badgeAccentColor, 0.35)
        border.width: 1
        border.color: compact.badgeAccentColor

        Text {
            id: badgeLabel

            anchors.centerIn: parent
            text: compact.plasmoidItem.badgeText
            color: Kirigami.Theme.textColor
            font.family: Kirigami.Theme.smallFont.family
            font.pointSize: Kirigami.Theme.smallFont.pointSize
            font.bold: true
        }
    }
}
