import QtQuick
import QtQuick.Effects
import QtQuick.Window
import org.kde.layershell as LayerShell

Window {
    id: root

    required property QtObject backend

    visible: backend.overlayActive
    width: Screen.width
    height: Screen.height
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus

    LayerShell.Window.scope: "projecteur-rust-overlay-preview"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop
                               | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft
                               | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: 0
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone
    LayerShell.Window.activateOnShow: false

    readonly property real spotDiameter: Math.max(50, Math.min(height, height * backend.spotSize / 100))
    property real presenterX: width / 2
    property real presenterY: height / 2
    readonly property real activeX: backend.presenterConnected
                                    ? presenterX
                                    : (pointer.containsMouse ? pointer.mouseX : width / 2)
    readonly property real activeY: backend.presenterConnected
                                    ? presenterY
                                    : (pointer.containsMouse ? pointer.mouseY : height / 2)
    readonly property real spotX: activeX - spotDiameter / 2
    readonly property real spotY: activeY - spotDiameter / 2

    Connections {
        target: backend

        function onMotionSerialChanged() {
            root.presenterX = Math.max(0, Math.min(root.width,
                                                   root.presenterX + backend.pointerDeltaX))
            root.presenterY = Math.max(0, Math.min(root.height,
                                                   root.presenterY + backend.pointerDeltaY))
        }
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: backend.cursor
        onClicked: backend.overlayActive = false
    }

    Rectangle {
        visible: backend.showSpotShade
        color: backend.shadeColor
        opacity: backend.shadeOpacity
        anchors { top: parent.top; bottom: aperture.top; left: parent.left; right: parent.right }
    }

    Rectangle {
        visible: backend.showSpotShade
        color: backend.shadeColor
        opacity: backend.shadeOpacity
        anchors { top: aperture.bottom; bottom: parent.bottom; left: parent.left; right: parent.right }
    }

    Rectangle {
        visible: backend.showSpotShade
        color: backend.shadeColor
        opacity: backend.shadeOpacity
        anchors { top: aperture.top; bottom: aperture.bottom; left: parent.left; right: aperture.left }
    }

    Rectangle {
        visible: backend.showSpotShade
        color: backend.shadeColor
        opacity: backend.shadeOpacity
        anchors { top: aperture.top; bottom: aperture.bottom; left: aperture.right; right: parent.right }
    }

    Rectangle {
        id: aperture
        x: root.spotX
        y: root.spotY
        width: root.spotDiameter
        height: width
        radius: width / 2
        color: "transparent"
        border.width: backend.showBorder ? Math.max(1, backend.borderSize) : 1
        border.color: backend.showBorder ? backend.borderColor : "white"
        opacity: backend.showBorder ? backend.borderOpacity : 0.7
    }

    Rectangle {
        id: centerShade
        anchors.fill: aperture
        visible: false
        color: backend.shadeColor
        layer.enabled: true
    }

    Rectangle {
        id: circularMask
        anchors.fill: aperture
        visible: false
        radius: width / 2
        layer.enabled: true
    }

    MultiEffect {
        anchors.fill: aperture
        visible: backend.showSpotShade
        source: centerShade
        opacity: backend.shadeOpacity
        maskEnabled: true
        maskInverted: true
        maskSource: circularMask
        enabled: false
    }

    Rectangle {
        anchors.centerIn: aperture
        visible: backend.showCenterDot
        width: backend.dotSize
        height: width
        radius: width / 2
        color: backend.dotColor
        opacity: backend.dotOpacity
    }

    Rectangle {
        visible: backend.previewTimeoutEnabled
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 24
        width: safetyText.implicitWidth + 32
        height: safetyText.implicitHeight + 20
        radius: 8
        color: "#d0202020"

        Text {
            id: safetyText
            anchors.centerIn: parent
            color: "white"
            text: qsTr("Move the pointer to test the aperture — click to close (12-second timeout)")
        }
    }

    Timer {
        interval: 12000
        running: root.visible && backend.previewTimeoutEnabled
        onTriggered: backend.overlayActive = false
    }
}
