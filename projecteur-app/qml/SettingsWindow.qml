import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.kde.kquickcontrols as KQuickControls

ApplicationWindow {
    id: root
    required property QtObject backend

    width: 850
    height: 720
    minimumWidth: 760
    minimumHeight: 620
    title: qsTr("Projecteur - Preferences")
    visible: backend.showWindow
    flags: Qt.Dialog
    property bool dirty: false
    property bool closingAfterCommit: false
    property int selectedInputMappingRow: -1
    property int nativeMappingRecordingRow: -1
    property var inputMappingRows: {
        try { return JSON.parse(backend.inputMappingRows) }
        catch (error) { return [] }
    }
    property var globalShortcutRows: {
        try { return JSON.parse(backend.globalShortcutRows) }
        catch (error) { return [] }
    }

    function changed() {
        dirty = true
        presetCombo.currentIndex = 0
        backend.markSettingsChanged()
    }
    function apply() {
        if (backend.saveSettings()) dirty = false
    }
    function acceptSettings() {
        if (dirty && !backend.saveSettings()) return
        dirty = false
        closingAfterCommit = true
        backend.showWindow = false
    }
    function rejectSettings() {
        backend.restoreAppliedSettings()
        dirty = false
        closingAfterCommit = true
        backend.showWindow = false
    }

    onVisibleChanged: if (visible) {
        backend.beginPreferences()
        dirty = false
        closingAfterCommit = false
        presetCombo.currentIndex = 0
        raise()
        requestActivate()
    }
    onClosing: function(close) {
        close.accepted = false
        if (!closingAfterCommit) backend.restoreAppliedSettings()
        backend.showWindow = false
    }

    header: TabBar {
        id: tabs
        TabButton { text: qsTr("Overlay") }
        TabButton { text: qsTr("Devices") }
        TabButton { text: qsTr("Shortcuts") }
    }

    StackLayout {
        anchors { fill: parent; margins: 12; bottomMargin: buttonRow.height + 24 }
        currentIndex: tabs.currentIndex

        ScrollView {
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 10

                Frame {
                    Layout.fillWidth: true
                    ColumnLayout {
                        anchors.fill: parent
                        Switch {
                            text: qsTr("Overlay enabled")
                            checked: !backend.overlayDisabled
                            font.bold: true
                            onClicked: { backend.overlayDisabled = !checked; root.changed() }
                        }
                        Switch {
                            text: qsTr("Show on all screens")
                            checked: backend.multiScreenOverlay
                            enabled: !backend.overlayDisabled
                            onClicked: { backend.multiScreenOverlay = checked; root.changed() }
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    enabled: !backend.overlayDisabled
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 10

                    Kirigami.FormLayout {
                        id: leftOverlayForm
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        twinFormLayouts: [rightOverlayForm]

                        Kirigami.Separator {
                            Kirigami.FormData.label: qsTr("Spotlight shape")
                            Kirigami.FormData.isSection: true
                        }
                        RowLayout {
                            enabled: backend.zoomEnabled || backend.showSpotShade || backend.showBorder
                            Kirigami.FormData.label: qsTr("Size:")
                            SpinBox {
                                from: 5; to: 100; value: backend.spotSize
                                onValueModified: { backend.spotSize = value; root.changed() }
                            }
                            Label { text: qsTr("% of screen height") }
                        }
                        ComboBox {
                            enabled: backend.zoomEnabled || backend.showSpotShade || backend.showBorder
                            Layout.fillWidth: true
                            Kirigami.FormData.label: qsTr("Type:")
                            model: [qsTr("Circle"), qsTr("Square"), qsTr("Star"), qsTr("N-gon")]
                            currentIndex: backend.spotShape.indexOf("Square") >= 0 ? 1
                                          : backend.spotShape.indexOf("Star") >= 0 ? 2
                                          : backend.spotShape.indexOf("Ngon") >= 0 ? 3 : 0
                            onActivated: {
                                backend.spotShape = ["spotshapes/Circle.qml", "spotshapes/Square.qml",
                                                     "spotshapes/Star.qml", "spotshapes/Ngon.qml"][currentIndex]
                                root.changed()
                            }
                        }
                        SpinBox {
                            visible: backend.spotShape.indexOf("Circle") < 0
                            enabled: backend.zoomEnabled || backend.showSpotShade || backend.showBorder
                            Kirigami.FormData.label: qsTr("Rotation:")
                            from: 0; to: 3600; stepSize: 10; value: Math.round(backend.spotRotation * 10)
                            textFromValue: function(value) { return (value / 10).toFixed(1) + "°" }
                            valueFromText: function(text) { return Math.round(parseFloat(text) * 10) }
                            onValueModified: { backend.spotRotation = value / 10; root.changed() }
                        }
                        SpinBox {
                            visible: backend.spotShape.indexOf("Square") >= 0
                            enabled: backend.zoomEnabled || backend.showSpotShade || backend.showBorder
                            Kirigami.FormData.label: qsTr("Corner radius:")
                            from: 0; to: 100; value: backend.squareRadius
                            onValueModified: { backend.squareRadius = value; root.changed() }
                        }
                        SpinBox {
                            visible: backend.spotShape.indexOf("Star") >= 0
                            enabled: backend.zoomEnabled || backend.showSpotShade || backend.showBorder
                            Kirigami.FormData.label: qsTr("Points:")
                            from: 3; to: 100; value: backend.starPoints
                            onValueModified: { backend.starPoints = value; root.changed() }
                        }
                        SpinBox {
                            visible: backend.spotShape.indexOf("Star") >= 0
                            enabled: backend.zoomEnabled || backend.showSpotShade || backend.showBorder
                            Kirigami.FormData.label: qsTr("Inner radius:")
                            from: 5; to: 100; value: backend.starInnerRadius
                            onValueModified: { backend.starInnerRadius = value; root.changed() }
                        }
                        SpinBox {
                            visible: backend.spotShape.indexOf("Ngon") >= 0
                            enabled: backend.zoomEnabled || backend.showSpotShade || backend.showBorder
                            Kirigami.FormData.label: qsTr("Sides:")
                            from: 3; to: 100; value: backend.ngonSides
                            onValueModified: { backend.ngonSides = value; root.changed() }
                        }

                        Kirigami.Separator {
                            Kirigami.FormData.label: qsTr("Zoom")
                            Kirigami.FormData.isSection: true
                        }
                        CheckBox {
                            text: qsTr("Enable zoom")
                            checked: backend.zoomEnabled
                            onClicked: { backend.zoomEnabled = checked; root.changed() }
                        }
                        SpinBox {
                            enabled: backend.zoomEnabled
                            Kirigami.FormData.label: qsTr("Level:")
                            from: 150; to: 2000; stepSize: 10; value: Math.round(backend.zoomFactor * 100)
                            textFromValue: function(value) { return (value / 100).toFixed(2) + "×" }
                            valueFromText: function(text) { return Math.round(parseFloat(text) * 100) }
                            onValueModified: { backend.zoomFactor = value / 100; root.changed() }
                        }
                        ComboBox {
                            enabled: backend.zoomEnabled
                            Layout.fillWidth: true
                            Kirigami.FormData.label: qsTr("Content type:")
                            model: [qsTr("Smooth (images)"), qsTr("Text and UI"), qsTr("Pixel-perfect")]
                            currentIndex: backend.zoomMode === "text" ? 1 : backend.zoomMode === "pixel" ? 2 : 0
                            onActivated: { backend.zoomMode = ["smooth", "text", "pixel"][currentIndex]; root.changed() }
                        }

                        Kirigami.Separator {
                            Kirigami.FormData.label: qsTr("Cursor")
                            Kirigami.FormData.isSection: true
                        }
                        ComboBox {
                            Layout.fillWidth: true
                            Kirigami.FormData.label: qsTr("Cursor:")
                            textRole: "text"; valueRole: "value"
                            model: [
                                { text: qsTr("No Cursor"), value: 10 },
                                { text: qsTr("Arrow Cursor"), value: 0 },
                                { text: qsTr("Busy Cursor"), value: 16 },
                                { text: qsTr("Cross Cursor"), value: 2 },
                                { text: qsTr("Pointing Hand Cursor"), value: 13 },
                                { text: qsTr("Open Hand Cursor"), value: 17 },
                                { text: qsTr("Up Arrow Cursor"), value: 1 },
                                { text: qsTr("What's This Cursor"), value: 15 }
                            ]
                            Component.onCompleted: {
                                for (let i = 0; i < model.length; ++i)
                                    if (model[i].value === backend.cursor) currentIndex = i
                            }
                            onActivated: { backend.cursor = currentValue; root.changed() }
                        }
                    }

                    Kirigami.FormLayout {
                        id: rightOverlayForm
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        twinFormLayouts: [leftOverlayForm]

                        Kirigami.Separator {
                            Kirigami.FormData.label: qsTr("Shade")
                            Kirigami.FormData.isSection: true
                        }
                        CheckBox {
                            text: qsTr("Enable shade")
                            checked: backend.showSpotShade
                            onClicked: { backend.showSpotShade = checked; root.changed() }
                        }
                        ColorButton {
                            enabled: backend.showSpotShade
                            Kirigami.FormData.label: qsTr("Color:")
                            selectedColor: backend.shadeColor
                            onColorAccepted: function(value) { backend.shadeColor = value; root.changed() }
                        }
                        SpinBox {
                            enabled: backend.showSpotShade
                            Kirigami.FormData.label: qsTr("Opacity:")
                            from: 0; to: 100; stepSize: 10; value: Math.round(backend.shadeOpacity * 100)
                            textFromValue: function(value) { return (value / 100).toFixed(2) }
                            valueFromText: function(text) { return Math.round(parseFloat(text) * 100) }
                            onValueModified: { backend.shadeOpacity = value / 100; root.changed() }
                        }

                        Kirigami.Separator {
                            Kirigami.FormData.label: qsTr("Laser dot")
                            Kirigami.FormData.isSection: true
                        }
                        CheckBox {
                            text: qsTr("Enable laser dot")
                            checked: backend.showCenterDot
                            onClicked: { backend.showCenterDot = checked; root.changed() }
                        }
                        ComboBox {
                            enabled: backend.showCenterDot
                            Layout.fillWidth: true
                            Kirigami.FormData.label: qsTr("Appearance:")
                            model: [qsTr("Solid"), qsTr("Diffuse scintillating")]
                            currentIndex: backend.dotMode === "diffuse" ? 1 : 0
                            onActivated: { backend.dotMode = currentIndex ? "diffuse" : "solid"; root.changed() }
                        }
                        CheckBox {
                            enabled: backend.showCenterDot
                            text: qsTr("Fading trail")
                            checked: backend.dotTrailEnabled
                            onClicked: { backend.dotTrailEnabled = checked; root.changed() }
                        }
                        RowLayout {
                            enabled: backend.showCenterDot
                            Kirigami.FormData.label: qsTr("Size:")
                            SpinBox {
                                from: 3; to: 100; value: backend.dotSize
                                onValueModified: { backend.dotSize = value; root.changed() }
                            }
                            Label { text: qsTr("pixels") }
                        }
                        ColorButton {
                            enabled: backend.showCenterDot
                            Kirigami.FormData.label: qsTr("Color:")
                            selectedColor: backend.dotColor
                            onColorAccepted: function(value) { backend.dotColor = value; root.changed() }
                        }
                        SpinBox {
                            enabled: backend.showCenterDot
                            Kirigami.FormData.label: qsTr("Opacity:")
                            from: 0; to: 100; stepSize: 10; value: Math.round(backend.dotOpacity * 100)
                            textFromValue: function(value) { return (value / 100).toFixed(2) }
                            valueFromText: function(text) { return Math.round(parseFloat(text) * 100) }
                            onValueModified: { backend.dotOpacity = value / 100; root.changed() }
                        }

                        Kirigami.Separator {
                            Kirigami.FormData.label: qsTr("Border")
                            Kirigami.FormData.isSection: true
                        }
                        CheckBox {
                            text: qsTr("Enable border")
                            checked: backend.showBorder
                            onClicked: { backend.showBorder = checked; root.changed() }
                        }
                        RowLayout {
                            enabled: backend.showBorder
                            Kirigami.FormData.label: qsTr("Size:")
                            SpinBox {
                                from: 0; to: 100; value: backend.borderSize
                                onValueModified: { backend.borderSize = value; root.changed() }
                            }
                            Label { text: qsTr("% of spotlight size") }
                        }
                        ColorButton {
                            enabled: backend.showBorder
                            Kirigami.FormData.label: qsTr("Color:")
                            selectedColor: backend.borderColor
                            onColorAccepted: function(value) { backend.borderColor = value; root.changed() }
                        }
                        SpinBox {
                            enabled: backend.showBorder
                            Kirigami.FormData.label: qsTr("Opacity:")
                            from: 0; to: 100; stepSize: 10; value: Math.round(backend.borderOpacity * 100)
                            textFromValue: function(value) { return (value / 100).toFixed(2) }
                            valueFromText: function(text) { return Math.round(parseFloat(text) * 100) }
                            onValueModified: { backend.borderOpacity = value / 100; root.changed() }
                        }
                    }
                }

                Frame {
                    Layout.fillWidth: true
                    enabled: !backend.overlayDisabled
                    RowLayout {
                        anchors.fill: parent
                        Label { text: qsTr("Presets") }
                        ComboBox {
                            id: presetCombo
                            Layout.fillWidth: true
                            model: [qsTr("Custom")].concat(backend.presetNames.length ? backend.presetNames.split("\n") : [])
                            onActivated: if (currentIndex > 0 && backend.loadPreset(currentText)) root.dirty = true
                        }
                        Button { text: "−"; enabled: presetCombo.currentIndex > 0
                            ToolTip.text: qsTr("Delete currently selected preset.")
                            onClicked: { if (backend.removePreset(presetCombo.currentText)) presetCombo.currentIndex = 0 } }
                        Button { text: "+"
                            ToolTip.text: qsTr("Create new preset from current spotlight settings.")
                            onClicked: presetNameDialog.open() }
                    }
                }

                Button {
                    text: qsTr("&Show test...")
                    Layout.alignment: Qt.AlignLeft
                    enabled: !backend.overlayDisabled
                    onClicked: backend.showOverlayTest()
                }
            }
        }

        ScrollView {
            ColumnLayout {
                width: parent.width
                Item {
                    visible: !backend.presenterConnected
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    RowLayout {
                        anchors.centerIn: parent
                        Label { text: "⚠" }
                        Label { text: qsTr("No devices connected.") }
                    }
                }

                ColumnLayout {
                    visible: backend.presenterConnected
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 10

                    SettingRow {
                        label: qsTr("Device")
                        ComboBox {
                            Layout.fillWidth: true
                            model: [backend.presenterDevice]
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("List of connected devices.")
                        }
                    }

                    TabBar {
                        id: deviceTabs
                        Layout.fillWidth: true
                        TabButton { text: qsTr("Input Mapping") }
                        TabButton { text: qsTr("Presentation timer feedback") }
                        TabButton { text: qsTr("Details") }
                    }

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: deviceTabs.currentIndex

                        ColumnLayout {
                            Shortcut {
                                sequence: "Shift+Delete"
                                enabled: root.selectedInputMappingRow >= 0
                                onActivated: {
                                    backend.removeInputMapping(root.selectedInputMappingRow)
                                    root.selectedInputMappingRow = -1
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Button {
                                    text: "+"
                                    enabled: backend.buttonForwarding
                                    onClicked: root.selectedInputMappingRow = backend.addInputMapping()
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Add a new input mapping entry.")
                                }
                                Button {
                                    text: "−"
                                    enabled: root.selectedInputMappingRow >= 0
                                    onClicked: {
                                        backend.removeInputMapping(root.selectedInputMappingRow)
                                        root.selectedInputMappingRow = -1
                                    }
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Delete the selected input mapping entries (Shift+Del).")
                                }
                                Item { Layout.fillWidth: true }
                                Label { text: qsTr("Input Sequence Interval") }
                                SpinBox {
                                    from: 100
                                    to: 950
                                    stepSize: 50
                                    value: backend.deviceInputSequenceInterval
                                    onValueModified: backend.updateDeviceInputSequenceInterval(value)
                                }
                                Label { text: qsTr("ms") }
                            }
                            Frame {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 4
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Item { Layout.preferredWidth: 28 }
                                        Label { text: qsTr("Input Sequence"); font.bold: true; Layout.fillWidth: true }
                                        Label { text: qsTr("Type"); font.bold: true; Layout.preferredWidth: 175 }
                                        Label { text: qsTr("Mapped Action"); font.bold: true; Layout.preferredWidth: 190 }
                                    }
                                    Rectangle { Layout.fillWidth: true; height: 1; color: root.palette.mid }

                                    ListView {
                                        id: inputMappingList
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        clip: true
                                        spacing: 3
                                        model: root.inputMappingRows
                                        delegate: Rectangle {
                                            id: mappingRow
                                            required property var modelData
                                            required property int index
                                            width: inputMappingList.width
                                            height: 38
                                            color: root.selectedInputMappingRow === index
                                                   ? root.palette.highlight : "transparent"
                                            border.color: modelData.duplicate ? "red" : "transparent"
                                            border.width: modelData.duplicate ? 1 : 0

                                            MouseArea {
                                                anchors.fill: parent
                                                onClicked: root.selectedInputMappingRow = mappingRow.index
                                            }

                                            RowLayout {
                                                anchors.fill: parent
                                                spacing: 6
                                                Label {
                                                    Layout.preferredWidth: 28
                                                    horizontalAlignment: Text.AlignHCenter
                                                    text: mappingRow.index + 1
                                                    color: mappingRow.modelData.duplicate ? "red"
                                                          : root.selectedInputMappingRow === mappingRow.index
                                                            ? root.palette.highlightedText : root.palette.text
                                                    ToolTip.visible: mappingRow.modelData.duplicate && hovered
                                                    ToolTip.text: qsTr("Duplicate input sequence")
                                                }
                                                Button {
                                                    Layout.fillWidth: true
                                                    text: backend.inputMappingRecordingRow === mappingRow.index
                                                          ? "● " + backend.inputMappingRecordingPreview
                                                          : mappingRow.modelData.input
                                                    onClicked: {
                                                        root.selectedInputMappingRow = mappingRow.index
                                                        if (backend.inputMappingRecordingRow === mappingRow.index)
                                                            backend.cancelInputMappingRecording()
                                                        else
                                                            backend.startInputMappingRecording(mappingRow.index)
                                                    }
                                                    ToolTip.visible: hovered
                                                    ToolTip.text: qsTr("Click to record presenter button(s); right-click for hold movement.")
                                                    TapHandler {
                                                        acceptedButtons: Qt.RightButton
                                                        onTapped: specialInputMenu.open()
                                                    }
                                                    Menu {
                                                        id: specialInputMenu
                                                        Repeater {
                                                            model: ["Next Hold Move", "Back Hold Move"]
                                                            MenuItem {
                                                                required property string modelData
                                                                text: modelData
                                                                onTriggered: backend.setSpecialInputMapping(
                                                                    mappingRow.index, modelData)
                                                            }
                                                        }
                                                    }
                                                }
                                                ComboBox {
                                                    id: actionType
                                                    Layout.preferredWidth: 175
                                                    property var actionTypes: mappingRow.modelData.moveInput
                                                        ? [{ text: qsTr("Scroll Horizontal"), value: 11 },
                                                           { text: qsTr("Scroll Vertical"), value: 12 },
                                                           { text: qsTr("Volume Control"), value: 13 }]
                                                        : [{ text: qsTr("Key Sequence"), value: 1 },
                                                           { text: qsTr("Cycle Presets"), value: 2 },
                                                           { text: qsTr("Toggle Spotlight"), value: 3 }]
                                                    textRole: "text"
                                                    valueRole: "value"
                                                    model: actionTypes
                                                    currentIndex: {
                                                        for (let i = 0; i < actionTypes.length; ++i)
                                                            if (actionTypes[i].value === mappingRow.modelData.actionType) return i
                                                        return 0
                                                    }
                                                    onActivated: backend.setInputMappingAction(
                                                        mappingRow.index, actionTypes[currentIndex].value)
                                                }
                                                Button {
                                                    Layout.preferredWidth: 190
                                                    enabled: mappingRow.modelData.actionType === 1
                                                    text: mappingRow.modelData.action
                                                    onClicked: keyActionMenu.open()
                                                    Menu {
                                                        id: keyActionMenu
                                                        MenuItem {
                                                            text: qsTr("Record key sequence…")
                                                            onTriggered: {
                                                                if (backend.beginNativeMappingRecording(mappingRow.index)) {
                                                                    root.nativeMappingRecordingRow = mappingRow.index
                                                                    nativeKeyRecorder.forceActiveFocus()
                                                                }
                                                            }
                                                        }
                                                        MenuSeparator {}
                                                        Repeater {
                                                            model: ["Alt+Tab", "Alt+F4", "Meta", "None"]
                                                            MenuItem {
                                                                required property string modelData
                                                                text: modelData
                                                                onTriggered: backend.setInputMappingPredefinedKey(
                                                                    mappingRow.index, modelData)
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: root.nativeMappingRecordingRow >= 0 ? 38 : 0
                                        visible: root.nativeMappingRecordingRow >= 0
                                        color: root.palette.base
                                        border.color: root.palette.highlight
                                        radius: 2
                                        Timer {
                                            id: nativeKeyRecordingTimer
                                            interval: 1000
                                            onTriggered: {
                                                backend.finishNativeMappingRecording()
                                                root.nativeMappingRecordingRow = -1
                                            }
                                        }
                                        FocusScope {
                                            id: nativeKeyRecorder
                                            anchors.fill: parent
                                            focus: root.nativeMappingRecordingRow >= 0
                                            Keys.onPressed: function(event) {
                                                if (event.isAutoRepeat) return
                                                if (event.key === Qt.Key_Escape) {
                                                    nativeKeyRecordingTimer.stop()
                                                    backend.cancelNativeMappingRecording()
                                                    root.nativeMappingRecordingRow = -1
                                                    event.accepted = true
                                                    return
                                                }
                                                if (event.key === Qt.Key_Control || event.key === Qt.Key_Shift
                                                        || event.key === Qt.Key_Alt || event.key === Qt.Key_Meta
                                                        || event.key === Qt.Key_AltGr) {
                                                    event.accepted = true
                                                    return
                                                }
                                                let count = backend.recordNativeMappingKey(
                                                    root.nativeMappingRecordingRow, event.key,
                                                    event.nativeScanCode, event.modifiers)
                                                if (count >= 4) {
                                                    nativeKeyRecordingTimer.stop()
                                                    backend.finishNativeMappingRecording()
                                                    root.nativeMappingRecordingRow = -1
                                                } else if (count > 0) {
                                                    nativeKeyRecordingTimer.restart()
                                                }
                                                event.accepted = true
                                            }
                                            Label {
                                                anchors.centerIn: parent
                                                text: backend.nativeMappingRecordingPreview
                                                      + qsTr(" (Esc to cancel)")
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            GroupBox {
                                title: qsTr("Presentation timer feedback")
                                Layout.fillWidth: true
                                GridLayout {
                                    anchors.fill: parent
                                    columns: 2
                                    Label { text: qsTr("Completion vibration strength") }
                                    SpinBox {
                                        Layout.fillWidth: true
                                        from: 0
                                        to: 100
                                        stepSize: 5
                                        value: backend.deviceTimerHapticStrength
                                        textFromValue: function(value) { return value + "%" }
                                        valueFromText: function(text) { return parseInt(text) }
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Set to 0% to disable completion vibration.")
                                        onValueModified: backend.updateDeviceTimerHapticStrength(value)
                                    }
                                    Label {
                                        Layout.columnSpan: 2
                                        text: qsTr("Vibrates this presenter when the presentation timer finishes.")
                                    }
                                }
                            }
                            Item { Layout.fillHeight: true }
                        }

                        TextArea {
                            readOnly: true
                            text: backend.presenterDetails
                            wrapMode: TextEdit.Wrap
                            selectByMouse: true
                            font.family: Application.font.family
                        }
                    }
                }
            }
        }

        ScrollView {
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                GroupBox {
                    title: qsTr("Global Shortcuts")
                    Layout.fillWidth: true
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Action"); font.bold: true; Layout.fillWidth: true }
                            Label { text: qsTr("Primary"); font.bold: true; Layout.preferredWidth: 220 }
                            Label { text: qsTr("Alternate"); font.bold: true; Layout.preferredWidth: 220 }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: root.palette.mid }
                        Repeater {
                            model: root.globalShortcutRows
                            delegate: RowLayout {
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                Label {
                                    text: modelData.name.replace("&", "")
                                    Layout.fillWidth: true
                                }
                                KQuickControls.KeySequenceItem {
                                    Layout.preferredWidth: 220
                                    showCancelButton: true
                                    multiKeyShortcutsAllowed: false
                                    keySequence: modelData.shortcuts[0] || ""
                                    onKeySequenceModified: {
                                        if (backend.updateGlobalShortcut(index, 0, keySequence))
                                            root.changed()
                                    }
                                }
                                KQuickControls.KeySequenceItem {
                                    Layout.preferredWidth: 220
                                    showCancelButton: true
                                    multiKeyShortcutsAllowed: false
                                    keySequence: modelData.shortcuts[1] || ""
                                    onKeySequenceModified: {
                                        if (backend.updateGlobalShortcut(index, 1, keySequence))
                                            root.changed()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    RowLayout {
        id: buttonRow
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 12 }
        Button { text: qsTr("Defaults"); onClicked: { backend.restoreDefaultSettings(); root.changed() } }
        Item { Layout.fillWidth: true }
        Button { text: qsTr("OK"); onClicked: root.acceptSettings() }
        Button { text: qsTr("Apply"); enabled: root.dirty; onClicked: root.apply() }
        Button { text: qsTr("Cancel"); onClicked: root.rejectSettings() }
    }

    Dialog {
        id: presetNameDialog
        title: qsTr("New Preset")
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        onAccepted: if (backend.savePreset(presetName.text)) presetCombo.currentIndex = presetCombo.count - 1
        TextField { id: presetName; width: 260; text: qsTr("New Preset"); maximumLength: 35; selectByMouse: true }
    }

    Shortcut { sequences: [StandardKey.Cancel]; onActivated: root.rejectSettings() }
}
