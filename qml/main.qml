// This file is part of Projecteur - https://github.com/jahnf/projecteur - See LICENSE.md and README.md
import QtQuick
import QtQuick.Effects
import QtQuick.Window
import org.kde.pipewire as KPipeWire

import Projecteur.Utils 1.0 as Utils

Window {
    id: mainWindow
    property var screenId: -1
    readonly property bool spotOnCurrentWindow: ProjecteurApp.currentSpotScreen === screenId
    property alias desktopPixmap: desktopImage.pixmap
    property var desktopStream: null

    width: 300; height: 200

    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus

    color: "transparent"

    readonly property double diagonal: Math.sqrt(Math.pow(Math.max(width, height),2)*2)
    readonly property real deviceScale: screen ? screen.devicePixelRatio : 1.0

    function snapToDevicePixel(value) {
        return Math.round(value * deviceScale) / deviceScale
    }

    Item {
        id: rotationItem
        anchors.centerIn: parent
        width: rotation === 0 ? mainWindow.width : mainWindow.diagonal;
        height: rotation === 0 ? mainWindow.height : width
        rotation: Settings.spotRotationAllowed ? Settings.spotRotation : 0

        opacity: ProjecteurApp.overlayVisible ? 1.0 : 0.0
        Behavior on opacity { PropertyAnimation { easing.type: Easing.OutQuad } }

        Item {
            id: desktopItem
            anchors.centerIn: centerRect
            visible: false; enabled: false; clip: true
            scale: Settings.zoomFactor
            width: centerRect.width / scale; height: centerRect.height / scale

            Item {
                id: desktopSource
                rotation: -rotationItem.rotation
                readonly property real xOffset: Math.floor(parent.width/2.0 + ((rotationItem.width-mainWindow.width)/2))
                readonly property real yOffset: Math.floor(parent.height/2.0 + ((rotationItem.height-mainWindow.height)/2))
                readonly property real rawX: -ma.mouseX + xOffset
                readonly property real rawY: -ma.mouseY + yOffset
                readonly property real sampleScaleX: desktopTexture.textureSize.width / parent.width
                readonly property real sampleScaleY: desktopTexture.textureSize.height / parent.height
                x: rotation == 0 ? Math.round(rawX * sampleScaleX) / sampleScaleX : rawX
                y: rotation == 0 ? Math.round(rawY * sampleScaleY) / sampleScaleY : rawY
                width: mainWindow.width; height: mainWindow.height

                Utils.Image {
                    id: desktopImage
                    anchors.fill: parent
                    smooth: desktopSource.rotation != 0 || mainWindow.deviceScale != 1.0
                    visible: !desktopStreamItem.ready
                }

                ShaderEffectSource {
                    anchors.fill: parent
                    sourceItem: desktopStreamItem
                    sourceRect: Qt.rect(0, 0,
                                        desktopStreamItem.width,
                                        desktopStreamItem.height)
                    live: true
                    smooth: true
                    visible: desktopStreamItem.ready
                }
            }
        }

        KPipeWire.PipeWireSourceItem {
            id: desktopStreamItem
            visible: true
            enabled: false
            width: mainWindow.width
            height: mainWindow.height
            nodeId: mainWindow.desktopStream
                    ? mainWindow.desktopStream.nodeId : 0
            allowDmaBuf: true
        }

        ShaderEffectSource {
            id: desktopTexture
            readonly property bool useDirectStream: desktopStreamItem.ready
            anchors.fill: centerRect
            visible: false
            sourceItem: useDirectStream ? desktopStreamItem : desktopItem
            hideSource: useDirectStream
            sourceRect: useDirectStream
                ? Qt.rect(
                    mainWindow.snapToDevicePixel(
                        centerRect.x + centerRect.width / 2 - desktopItem.width / 2),
                    mainWindow.snapToDevicePixel(
                        centerRect.y + centerRect.height / 2 - desktopItem.height / 2),
                    desktopItem.width,
                    desktopItem.height)
                : Qt.rect(0, 0, desktopItem.width, desktopItem.height)
            smooth: true
            textureSize: Qt.size(
                Math.max(1, Math.round(desktopItem.width)),
                Math.max(1, Math.round(desktopItem.height)))
        }

        MultiEffect {
            visible: Settings.zoomEnabled && mainWindow.spotOnCurrentWindow
            anchors.fill: centerRect
            source: desktopTexture
            maskEnabled: true
            maskSource: spotShapeLoader
            enabled: false
        }

        Item {
            anchors.fill: parent
            MouseArea {
                id: ma

                readonly property bool calculateMapping: Settings.multiScreenOverlayEnabled && !mainWindow.spotOnCurrentWindow
                readonly property point globalPos: calculateMapping ? ProjecteurApp.currentCursorPos : Qt.point(0,0)
                readonly property point mappedPos: calculateMapping ? mainWindow.contentItem.mapFromGlobal(globalPos.x, globalPos.y) : globalPos
                readonly property int posX: spotOnCurrentWindow ? mouseX : mappedPos.x
                readonly property int posY: spotOnCurrentWindow ? mouseY : mappedPos.y

                cursorShape: Settings.cursor
                anchors.fill: parent
                hoverEnabled: true
                onClicked: { ProjecteurApp.spotlightWindowClicked() }
                onExited: { ProjecteurApp.cursorExitedWindow() }
                onEntered: { ProjecteurApp.cursorEntered(screenId) }
                onPositionChanged: (mouse) => {

                    if (Settings.multiScreenOverlayEnabled) {
                        ProjecteurApp.cursorPositionChanged(
                            mainWindow.contentItem.mapToGlobal(mouse.x, mouse.y))
                    }
                }
            }
        }

        Rectangle {
            property int spotSize: (mainWindow.height / 100.0) * Settings.spotSize
            id: centerRect
            opacity: Settings.shadeOpacity
            height: spotSize > 50 ? Math.min(spotSize, mainWindow.height) : 50
            width: height
            x: mainWindow.snapToDevicePixel(ma.posX - width/2)
            y: mainWindow.snapToDevicePixel(ma.posY - height/2)
            color: Settings.shadeColor
            visible: false
            enabled: false
        }

        Loader {
            id: spotShapeLoader
            visible: false; enabled: false
            anchors.centerIn: centerRect
            width: centerRect.width;  height: width
            layer.enabled: true
            sourceComponent: Qt.createComponent(Settings.spotShape)
            onLoaded: item.visible = true
        }

        MultiEffect {
            id: spot
            visible: Settings.showSpotShade
            opacity: centerRect.opacity
            anchors.fill: centerRect
            source: centerRect
            maskEnabled: true
            maskInverted: true
            maskSource: spotShapeLoader
            enabled: false
        }

        Loader {
            id: borderShapeLoader
            anchors.centerIn: centerRect
            width: centerRect.width;  height: width
            visible: false; enabled: false
            layer.enabled: true
            sourceComponent: spotShapeLoader.sourceComponent
            onLoaded: {
                item.visible = true
                item.color = Qt.binding(function(){ return Settings.borderColor; })
            }
        }

        Item {
            id: borderShapeMask
            anchors.centerIn: centerRect
            width: centerRect.width;  height: width
            enabled: false; visible: false
            layer.enabled: true
            Item {
                id: borderShapeScaled
                anchors.centerIn: parent
                width: parent.width; height: width
                scale: (100 - Settings.borderSize) * 1.0 / 100.0
                property Component component: borderShapeLoader.sourceComponent
                property QtObject innerObject
                onComponentChanged: {
                    if (innerObject) innerObject.destroy()
                    innerObject = component.createObject(borderShapeScaled, {visible: true})
                }
            }
        }

        MultiEffect {
            id: spotBorder
            visible: Settings.showBorder && Settings.borderSize > 0
            opacity: Settings.borderOpacity
            anchors.fill: centerRect
            source: borderShapeLoader
            maskEnabled: true
            maskInverted: true
            maskSource: borderShapeMask
            enabled: false
        }

        Rectangle {
            id: dotCursor
            antialiasing: true
            anchors.centerIn: centerRect
            width: Settings.dotSize; height: width
            radius: width*0.5
            color: Settings.dotColor
            visible: Settings.showCenterDot
            opacity: Settings.dotOpacity
            enabled: false
        }

        Rectangle {
            id: topRect
            visible: spot.visible
            color: centerRect.color
            opacity: centerRect.opacity
            anchors{ top: parent.top; bottom: centerRect.top; left: parent.left; right: parent.right }
            enabled: false
        }

        Rectangle {
            id: bottomRect
            visible: spot.visible
            color: centerRect.color
            opacity: centerRect.opacity
            anchors{ top: centerRect.bottom; bottom: parent.bottom; left: parent.left; right: parent.right }
            enabled: false
        }

        Rectangle {
            id: leftRect
            visible: spot.visible
            color: centerRect.color
            opacity: centerRect.opacity
            anchors{ top: topRect.bottom; bottom: bottomRect.top; left: parent.left; right: centerRect.left }
            enabled: false
        }

        Rectangle {
            id: rightRect
            visible: spot.visible
            color: centerRect.color
            opacity: centerRect.opacity
            anchors{ top: topRect.bottom; bottom: bottomRect.top; left: centerRect.right; right: parent.right }
            enabled: false
        }
    }
} // Window
