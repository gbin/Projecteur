import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.layershell as LayerShell
import org.projecteur.rust

ApplicationWindow {
    id: root

    width: 520
    height: 310
    visible: backend.showWindow
    title: qsTr("Projecteur Rust port")

    // Keep the LayerShell module in the Cargo-built QML graph. Actual overlay
    // windows will opt into its attached properties; this ordinary diagnostic
    // window must remain a normal, easily dismissible desktop window.
    property Component layerShellProbe: Component {
        Window {
            visible: false
            LayerShell.Window.layer: LayerShell.Window.LayerOverlay
            LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone
        }
    }

    ProjecteurBackend {
        id: backend

        Component.onCompleted: confirmQmlLoaded()
    }

    OverlayPreview {
        backend: backend
    }

    Timer {
        interval: 8
        repeat: true
        running: true
        onTriggered: backend.pollPresenter()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 14

        Label {
            Layout.fillWidth: true
            text: qsTr("Projecteur is running from Rust + QML")
            font.pixelSize: 22
            font.bold: true
            wrapMode: Text.Wrap
        }

        Label {
            Layout.fillWidth: true
            text: backend.status
            wrapMode: Text.Wrap
        }

        Label {
            Layout.fillWidth: true
            text: "Config: " + (backend.configPath.length > 0 ? backend.configPath : "built-in defaults")
            elide: Text.ElideMiddle
        }

        Label {
            Layout.fillWidth: true
            text: "Overlay: size " + backend.spotSize
                  + ", dot " + backend.dotColor
                  + ", zoom " + backend.zoomMode + " ×" + backend.zoomFactor
        }

        Label {
            Layout.fillWidth: true
            text: backend.presenterConnected
                  ? qsTr("Presenter input: connected at %1").arg(backend.presenterDevice)
                  : qsTr("Presenter input: not connected")
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("This is a normal diagnostic window. Press Escape or use Close to exit.")
            opacity: 0.75
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true

            Button {
                text: backend.overlayActive ? qsTr("Hide overlay") : qsTr("Show overlay")
                onClicked: backend.overlayActive = !backend.overlayActive
            }

            Item {
                Layout.fillWidth: true
            }

            Button {
                text: qsTr("Close")
                onClicked: root.close()
            }
        }
    }

    Shortcut {
        sequences: [StandardKey.Cancel]
        onActivated: root.close()
    }
}
