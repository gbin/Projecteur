pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import QtQuick.Window
import org.kde.pipewire as KPipeWire
import org.kde.layershell as LayerShell

Window {
    id: root
    required property QtObject backend
    required property bool screenEnabled
    required property real presenterGlobalX
    required property real presenterGlobalY

    visible: backend.overlayActive && !backend.overlayDisabled
    opacity: contentEnabled ? 1 : 0
    width: Screen.width
    height: Screen.height
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus

    LayerShell.Window.scope: "projecteur-rust-overlay"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: 0
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone
    LayerShell.Window.activateOnShow: false

    readonly property real deviceScale: screen ? screen.devicePixelRatio : 1
    readonly property bool contentEnabled: backend.multiScreenOverlay
                                            || (backend.presenterConnected
                                                ? screenEnabled : pointer.containsMouse)
    readonly property real spotDiameter: Math.max(50, Math.min(height, height * backend.spotSize / 100))
    readonly property int captureX: screen ? Math.round(screen.virtualX) : 0
    readonly property int captureY: screen ? Math.round(screen.virtualY) : 0
    readonly property int captureWidth: screen ? Math.round(screen.width) : 0
    readonly property int captureHeight: screen ? Math.round(screen.height) : 0
    readonly property real activeX: backend.presenterConnected ? presenterGlobalX - screen.virtualX
                                    : (pointer.containsMouse ? pointer.mouseX : width / 2)
    readonly property real activeY: backend.presenterConnected ? presenterGlobalY - screen.virtualY
                                    : (pointer.containsMouse ? pointer.mouseY : height / 2)

    function snap(value) { return Math.round(value * deviceScale) / deviceScale }

    function ensureDesktopStream() {
        if (visible && contentEnabled && backend.zoomEnabled)
            backend.requestScreenCapture(captureX, captureY, captureWidth, captureHeight)
    }

    onVisibleChanged: ensureDesktopStream()
    onContentEnabledChanged: ensureDesktopStream()
    onScreenChanged: ensureDesktopStream()
    Component.onCompleted: ensureDesktopStream()

    Item {
        id: desktopCaptureSource
        width: root.width
        height: root.height

        KPipeWire.PipeWireSourceItem {
            id: desktopStream
            anchors.fill: parent
            enabled: false
            allowDmaBuf: true
            objectSerial: {
                const generation = backend.captureGeneration
                return backend.captureObjectSerial(
                    root.captureX, root.captureY, root.captureWidth, root.captureHeight)
            }
            nodeId: {
                const generation = backend.captureGeneration
                return objectSerial === 0
                        ? backend.captureNodeId(
                            root.captureX, root.captureY, root.captureWidth, root.captureHeight)
                        : 0
            }
        }

        Image {
            id: desktopSnapshot
            anchors.fill: parent
            visible: !desktopStream.ready
            cache: false
            source: {
                const generation = backend.captureGeneration
                const path = backend.captureSnapshotSource(
                    root.captureX, root.captureY, root.captureWidth, root.captureHeight)
                return path.length > 0 ? "file://" + path : ""
            }
        }
    }

    Connections {
        target: backend
        function onMotionSerialChanged() {
            liveIdleTimer.restart()
        }
        function onZoomEnabledChanged() {
            root.ensureDesktopStream()
        }
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: backend.cursor
        onClicked: backend.overlayActive = false
    }

    Item {
        id: rotated
        anchors.centerIn: parent
        width: backend.spotRotation === 0 ? root.width : Math.max(root.width, root.height) * 1.5
        height: backend.spotRotation === 0 ? root.height : width
        rotation: backend.spotShape.indexOf("Circle") >= 0 ? 0 : backend.spotRotation

        Rectangle {
            id: aperture
            property real pointerDx: root.activeX - root.width / 2
            property real pointerDy: root.activeY - root.height / 2
            property real inverseAngle: -rotated.rotation * Math.PI / 180
            property real translatedX: rotated.width / 2
                                      + pointerDx * Math.cos(inverseAngle)
                                      - pointerDy * Math.sin(inverseAngle)
            property real translatedY: rotated.height / 2
                                      + pointerDx * Math.sin(inverseAngle)
                                      + pointerDy * Math.cos(inverseAngle)
            x: root.snap(translatedX - width / 2)
            y: root.snap(translatedY - height / 2)
            width: root.spotDiameter
            height: width
            visible: false
            color: backend.shadeColor
            layer.enabled: true
        }

        ShapeMask {
            id: apertureMask
            anchors.fill: aperture
            visible: false
            layer.enabled: true
            shape: backend.spotShape
            squareRadius: backend.squareRadius
            starPoints: backend.starPoints
            starInnerRadius: backend.starInnerRadius
            ngonSides: backend.ngonSides
        }

        ShaderEffectSource {
            id: zoomTexture
            anchors.fill: aperture
            visible: false
            sourceItem: desktopCaptureSource
            hideSource: true
            sourceRect: Qt.rect(
                root.activeX - aperture.width / (2 * backend.zoomFactor),
                root.activeY - aperture.height / (2 * backend.zoomFactor),
                aperture.width / backend.zoomFactor,
                aperture.height / backend.zoomFactor)
            textureSize: Qt.size(
                Math.max(1, Math.round(aperture.width * root.deviceScale)),
                Math.max(1, Math.round(aperture.height * root.deviceScale)))
            smooth: backend.zoomMode !== "pixel"
            live: true
        }

        ShaderEffect {
            id: textZoom
            anchors.fill: aperture
            visible: false
            property variant source: zoomTexture
            property size outputSize: Qt.size(
                Math.max(1, Math.round(width * root.deviceScale)),
                Math.max(1, Math.round(height * root.deviceScale)))
            fragmentShader: "qrc:/projecteur/shaders/textzoom.frag.qsb"
        }

        MultiEffect {
            anchors.fill: aperture
            source: backend.zoomMode === "text" ? textZoom : zoomTexture
            visible: backend.zoomEnabled
            maskEnabled: true
            maskSource: apertureMask
            enabled: false
        }

        MultiEffect {
            id: centerShade
            anchors.fill: aperture
            source: aperture
            visible: backend.showSpotShade
            opacity: backend.shadeOpacity
            maskEnabled: true
            maskInverted: true
            maskSource: apertureMask
            enabled: false
        }

        Rectangle { visible: centerShade.visible; color: backend.shadeColor; opacity: backend.shadeOpacity
            anchors { top: parent.top; bottom: aperture.top; left: parent.left; right: parent.right } }
        Rectangle { visible: centerShade.visible; color: backend.shadeColor; opacity: backend.shadeOpacity
            anchors { top: aperture.bottom; bottom: parent.bottom; left: parent.left; right: parent.right } }
        Rectangle { visible: centerShade.visible; color: backend.shadeColor; opacity: backend.shadeOpacity
            anchors { top: aperture.top; bottom: aperture.bottom; left: parent.left; right: aperture.left } }
        Rectangle { visible: centerShade.visible; color: backend.shadeColor; opacity: backend.shadeOpacity
            anchors { top: aperture.top; bottom: aperture.bottom; left: aperture.right; right: parent.right } }

        ShapeMask {
            id: borderSource
            anchors.fill: aperture
            visible: false
            layer.enabled: true
            fillColor: backend.borderColor
            shape: backend.spotShape
            squareRadius: backend.squareRadius
            starPoints: backend.starPoints
            starInnerRadius: backend.starInnerRadius
            ngonSides: backend.ngonSides
        }

        Item {
            id: innerBorderMask
            anchors.fill: aperture
            visible: false
            layer.enabled: true
            ShapeMask {
                anchors.centerIn: parent
                width: parent.width; height: parent.height
                scale: Math.max(0, 100 - backend.borderSize) / 100
                shape: backend.spotShape
                squareRadius: backend.squareRadius
                starPoints: backend.starPoints
                starInnerRadius: backend.starInnerRadius
                ngonSides: backend.ngonSides
            }
        }

        MultiEffect {
            anchors.fill: aperture
            source: borderSource
            visible: backend.showBorder && backend.borderSize > 0
            opacity: backend.borderOpacity
            maskEnabled: true
            maskInverted: true
            maskSource: innerBorderMask
            enabled: false
        }
    }

    Item {
        id: trailHistory
        readonly property real currentX: root.activeX
        readonly property real currentY: root.activeY
        property real point1X: currentX; property real point1Y: currentY
        property real point2X: currentX; property real point2Y: currentY
        property real point3X: currentX; property real point3Y: currentY
        property real point4X: currentX; property real point4Y: currentY
        property real point5X: currentX; property real point5Y: currentY
        property real point6X: currentX; property real point6Y: currentY
        function reset() {
            point1X = currentX; point1Y = currentY; point2X = currentX; point2Y = currentY
            point3X = currentX; point3Y = currentY; point4X = currentX; point4Y = currentY
            point5X = currentX; point5Y = currentY; point6X = currentX; point6Y = currentY
        }
        Timer {
            interval: 24
            repeat: true
            running: root.visible && backend.showCenterDot && backend.dotTrailEnabled
            onRunningChanged: if (running) trailHistory.reset()
            onTriggered: {
                const dx = trailHistory.currentX - trailHistory.point1X
                const dy = trailHistory.currentY - trailHistory.point1Y
                if (dx * dx + dy * dy > 90000) { trailHistory.reset(); return }
                trailHistory.point6X = trailHistory.point5X; trailHistory.point6Y = trailHistory.point5Y
                trailHistory.point5X = trailHistory.point4X; trailHistory.point5Y = trailHistory.point4Y
                trailHistory.point4X = trailHistory.point3X; trailHistory.point4Y = trailHistory.point3Y
                trailHistory.point3X = trailHistory.point2X; trailHistory.point3Y = trailHistory.point2Y
                trailHistory.point2X = trailHistory.point1X; trailHistory.point2Y = trailHistory.point1Y
                trailHistory.point1X = trailHistory.currentX; trailHistory.point1Y = trailHistory.currentY
            }
        }
    }

    ShaderEffect {
        readonly property real margin: Math.max(4, backend.dotSize)
        readonly property real minimumX: Math.min(trailHistory.currentX, trailHistory.point1X,
            trailHistory.point2X, trailHistory.point3X, trailHistory.point4X, trailHistory.point5X, trailHistory.point6X)
        readonly property real maximumX: Math.max(trailHistory.currentX, trailHistory.point1X,
            trailHistory.point2X, trailHistory.point3X, trailHistory.point4X, trailHistory.point5X, trailHistory.point6X)
        readonly property real minimumY: Math.min(trailHistory.currentY, trailHistory.point1Y,
            trailHistory.point2Y, trailHistory.point3Y, trailHistory.point4Y, trailHistory.point5Y, trailHistory.point6Y)
        readonly property real maximumY: Math.max(trailHistory.currentY, trailHistory.point1Y,
            trailHistory.point2Y, trailHistory.point3Y, trailHistory.point4Y, trailHistory.point5Y, trailHistory.point6Y)
        x: minimumX - margin; y: minimumY - margin
        width: Math.max(1, maximumX - minimumX + margin * 2)
        height: Math.max(1, maximumY - minimumY + margin * 2)
        visible: backend.showCenterDot && backend.dotTrailEnabled
        opacity: backend.dotOpacity
        property size outputSize: Qt.size(width, height)
        property real dotSize: backend.dotSize
        property color dotColor: backend.dotColor
        property point point0: Qt.point(trailHistory.currentX - x, trailHistory.currentY - y)
        property point point1: Qt.point(trailHistory.point1X - x, trailHistory.point1Y - y)
        property point point2: Qt.point(trailHistory.point2X - x, trailHistory.point2Y - y)
        property point point3: Qt.point(trailHistory.point3X - x, trailHistory.point3Y - y)
        property point point4: Qt.point(trailHistory.point4X - x, trailHistory.point4Y - y)
        property point point5: Qt.point(trailHistory.point5X - x, trailHistory.point5Y - y)
        property point point6: Qt.point(trailHistory.point6X - x, trailHistory.point6Y - y)
        fragmentShader: "qrc:/projecteur/shaders/dottrail.frag.qsb"
    }

    Rectangle {
        x: root.activeX - width / 2; y: root.activeY - height / 2
        width: backend.dotSize; height: width; radius: width / 2
        color: backend.dotColor
        visible: backend.showCenterDot && backend.dotMode === "solid"
        opacity: backend.dotOpacity
        z: 2
    }

    ShaderEffect {
        id: diffuseDot
        x: root.activeX - width / 2; y: root.activeY - height / 2
        width: Math.max(24, backend.dotSize * 5); height: width
        visible: backend.showCenterDot && backend.dotMode === "diffuse"
        opacity: backend.dotOpacity
        z: 2
        property size outputSize: Qt.size(width, height)
        property real dotSize: backend.dotSize
        property color dotColor: backend.dotColor
        property real time: 0
        fragmentShader: "qrc:/projecteur/shaders/diffusedot.frag.qsb"
        NumberAnimation on time {
            from: 0; to: 100; duration: 100000; loops: Animation.Infinite
            running: diffuseDot.visible && root.visible
        }
    }

    Rectangle {
        visible: backend.previewTimeoutEnabled
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top; anchors.topMargin: 24
        width: safetyText.implicitWidth + 32; height: safetyText.implicitHeight + 20
        radius: 8; color: "#d0202020"; z: 20
        Text {
            id: safetyText
            anchors.centerIn: parent
            color: "white"
            text: qsTr("Move the pointer to test the overlay — click to close (12-second timeout)")
        }
    }

    Timer { id: liveIdleTimer; interval: 600
        onTriggered: if (!backend.previewTimeoutEnabled) backend.overlayActive = false }
    Timer { interval: 12000; running: root.visible && backend.previewTimeoutEnabled
        onTriggered: backend.overlayActive = false }
}
