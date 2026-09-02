pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.projecteur

Item {
    id: root
    SystemPalette { id: systemPalette }
    property real presenterGlobalX: Application.screens[0].virtualX
                                    + Application.screens[0].width / 2
    property real presenterGlobalY: Application.screens[0].virtualY
                                    + Application.screens[0].height / 2

    function showPreferences() {
        backend.showWindow = true
        preferences.show()
        preferences.raise()
        preferences.requestActivate()
    }

    function movePresenterPointer(dx, dy) {
        let left = Application.screens[0].virtualX
        let top = Application.screens[0].virtualY
        let right = left + Application.screens[0].width
        let bottom = top + Application.screens[0].height
        for (let screen of Application.screens) {
            left = Math.min(left, screen.virtualX)
            top = Math.min(top, screen.virtualY)
            right = Math.max(right, screen.virtualX + screen.width)
            bottom = Math.max(bottom, screen.virtualY + screen.height)
        }
        presenterGlobalX = Math.max(left, Math.min(right - 1, presenterGlobalX + dx))
        presenterGlobalY = Math.max(top, Math.min(bottom - 1, presenterGlobalY + dy))
    }

    ProjecteurBackend {
        id: backend
        Component.onCompleted: confirmQmlLoaded()
    }

    SettingsWindow {
        id: preferences
        backend: backend
    }

    Component.onCompleted: {
        if (backend.showWindow)
            showPreferences()
    }

    Instantiator {
        model: Application.screens
        delegate: OverlayPreview {
            required property var modelData
            backend: backend
            screen: modelData
            screenEnabled: backend.multiScreenOverlay
                           || (root.presenterGlobalX >= modelData.virtualX
                               && root.presenterGlobalX < modelData.virtualX + modelData.width
                               && root.presenterGlobalY >= modelData.virtualY
                               && root.presenterGlobalY < modelData.virtualY + modelData.height)
            presenterGlobalX: root.presenterGlobalX
            presenterGlobalY: root.presenterGlobalY
        }
    }

    Connections {
        target: backend
        function onShowPreferencesRequested() {
            root.showPreferences()
        }
        function onMotionSerialChanged() {
            root.movePresenterPointer(backend.pointerDeltaX, backend.pointerDeltaY)
        }
        function onQuitRequestedChanged() {
            if (backend.quitRequested)
                Qt.quit()
        }
    }

    Timer {
        interval: 8
        repeat: true
        running: true
        onTriggered: backend.pollPresenter()
    }
}
