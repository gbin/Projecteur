import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

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

    function changed() { dirty = true; presetCombo.currentIndex = 0 }
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

                CheckBox {
                    text: qsTr("Enable")
                    checked: !backend.overlayDisabled
                    onClicked: { backend.overlayDisabled = !checked; root.changed() }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop

                        GroupBox {
                            title: qsTr("Shape")
                            Layout.fillWidth: true
                            ColumnLayout {
                                anchors.fill: parent
                                SettingRow {
                                    label: qsTr("Size")
                                    SpinBox {
                                        from: 5; to: 100; value: backend.spotSize
                                        onValueModified: { backend.spotSize = value; root.changed() }
                                    }
                                    Label { text: qsTr("% of screen height") }
                                }
                                SettingRow {
                                    label: qsTr("Type")
                                    ComboBox {
                                        Layout.fillWidth: true
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
                                }
                                SettingRow {
                                    visible: backend.spotShape.indexOf("Circle") < 0
                                    label: qsTr("Rotation")
                                    SpinBox {
                                        from: 0; to: 3600; stepSize: 10; value: Math.round(backend.spotRotation * 10)
                                        textFromValue: function(value) { return (value / 10).toFixed(1) + "°" }
                                        valueFromText: function(text) { return Math.round(parseFloat(text) * 10) }
                                        onValueModified: { backend.spotRotation = value / 10; root.changed() }
                                    }
                                }
                                SettingRow {
                                    visible: backend.spotShape.indexOf("Square") >= 0
                                    label: qsTr("Corner Radius")
                                    SpinBox { from: 0; to: 100; value: backend.squareRadius
                                        onValueModified: { backend.squareRadius = value; root.changed() } }
                                }
                                SettingRow {
                                    visible: backend.spotShape.indexOf("Star") >= 0
                                    label: qsTr("Points")
                                    SpinBox { from: 3; to: 100; value: backend.starPoints
                                        onValueModified: { backend.starPoints = value; root.changed() } }
                                }
                                SettingRow {
                                    visible: backend.spotShape.indexOf("Star") >= 0
                                    label: qsTr("Inner Radius")
                                    SpinBox { from: 5; to: 100; value: backend.starInnerRadius
                                        onValueModified: { backend.starInnerRadius = value; root.changed() } }
                                }
                                SettingRow {
                                    visible: backend.spotShape.indexOf("Ngon") >= 0
                                    label: qsTr("Sides")
                                    SpinBox { from: 3; to: 100; value: backend.ngonSides
                                        onValueModified: { backend.ngonSides = value; root.changed() } }
                                }
                            }
                        }

                        GroupBox {
                            Layout.fillWidth: true
                            label: CheckBox {
                                text: qsTr("Zoom")
                                checked: backend.zoomEnabled
                                onClicked: { backend.zoomEnabled = checked; root.changed() }
                            }
                            ColumnLayout {
                                anchors.fill: parent
                                enabled: backend.zoomEnabled
                                SettingRow {
                                    label: qsTr("Level")
                                    SpinBox {
                                        from: 150; to: 2000; stepSize: 10; value: Math.round(backend.zoomFactor * 100)
                                        textFromValue: function(value) { return (value / 100).toFixed(2) + "×" }
                                        valueFromText: function(text) { return Math.round(parseFloat(text) * 100) }
                                        onValueModified: { backend.zoomFactor = value / 100; root.changed() }
                                    }
                                }
                                SettingRow {
                                    label: qsTr("Content Type")
                                    ComboBox {
                                        Layout.fillWidth: true
                                        model: [qsTr("Smooth (images)"), qsTr("Text and UI"), qsTr("Pixel-perfect")]
                                        currentIndex: backend.zoomMode === "text" ? 1 : backend.zoomMode === "pixel" ? 2 : 0
                                        onActivated: { backend.zoomMode = ["smooth", "text", "pixel"][currentIndex]; root.changed() }
                                    }
                                }
                            }
                        }

                        GroupBox {
                            title: qsTr("Cursor")
                            Layout.fillWidth: true
                            SettingRow {
                                anchors.fill: parent
                                label: qsTr("Cursor")
                                ComboBox {
                                    Layout.fillWidth: true
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
                        }

                        CheckBox {
                            text: qsTr("Multi-screen overlay")
                            checked: backend.multiScreenOverlay
                            onClicked: { backend.multiScreenOverlay = checked; root.changed() }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop

                        GroupBox {
                            Layout.fillWidth: true
                            label: CheckBox { text: qsTr("Shade"); checked: backend.showSpotShade
                                onClicked: { backend.showSpotShade = checked; root.changed() } }
                            ColumnLayout {
                                anchors.fill: parent; enabled: backend.showSpotShade
                                SettingRow { label: qsTr("Color")
                                    ColorButton { selectedColor: backend.shadeColor
                                        onColorAccepted: function(value) { backend.shadeColor = value; root.changed() } } }
                                SettingRow { label: qsTr("Opacity")
                                    SpinBox { from: 0; to: 100; stepSize: 10; value: Math.round(backend.shadeOpacity * 100)
                                        textFromValue: function(value) { return (value / 100).toFixed(2) }
                                        valueFromText: function(text) { return Math.round(parseFloat(text) * 100) }
                                        onValueModified: { backend.shadeOpacity = value / 100; root.changed() } } }
                            }
                        }

                        GroupBox {
                            Layout.fillWidth: true
                            label: CheckBox { text: qsTr("Laser dot"); checked: backend.showCenterDot
                                onClicked: { backend.showCenterDot = checked; root.changed() } }
                            ColumnLayout {
                                anchors.fill: parent; enabled: backend.showCenterDot
                                SettingRow { label: qsTr("Appearance")
                                    ComboBox { Layout.fillWidth: true
                                        model: [qsTr("Solid"), qsTr("Diffuse scintillating")]
                                        currentIndex: backend.dotMode === "diffuse" ? 1 : 0
                                        onActivated: { backend.dotMode = currentIndex ? "diffuse" : "solid"; root.changed() } } }
                                CheckBox { text: qsTr("Fading trail"); checked: backend.dotTrailEnabled
                                    onClicked: { backend.dotTrailEnabled = checked; root.changed() } }
                                SettingRow { label: qsTr("Size")
                                    SpinBox { from: 3; to: 100; value: backend.dotSize
                                        onValueModified: { backend.dotSize = value; root.changed() } }
                                    Label { text: qsTr("pixels") } }
                                SettingRow { label: qsTr("Color")
                                    ColorButton { selectedColor: backend.dotColor
                                        onColorAccepted: function(value) { backend.dotColor = value; root.changed() } } }
                                SettingRow { label: qsTr("Opacity")
                                    SpinBox { from: 0; to: 100; stepSize: 10; value: Math.round(backend.dotOpacity * 100)
                                        textFromValue: function(value) { return (value / 100).toFixed(2) }
                                        valueFromText: function(text) { return Math.round(parseFloat(text) * 100) }
                                        onValueModified: { backend.dotOpacity = value / 100; root.changed() } } }
                            }
                        }

                        GroupBox {
                            Layout.fillWidth: true
                            label: CheckBox { text: qsTr("Border"); checked: backend.showBorder
                                onClicked: { backend.showBorder = checked; root.changed() } }
                            ColumnLayout {
                                anchors.fill: parent; enabled: backend.showBorder
                                SettingRow { label: qsTr("Size")
                                    SpinBox { from: 0; to: 100; value: backend.borderSize
                                        onValueModified: { backend.borderSize = value; root.changed() } }
                                    Label { text: qsTr("% of spotlight size") } }
                                SettingRow { label: qsTr("Color")
                                    ColorButton { selectedColor: backend.borderColor
                                        onColorAccepted: function(value) { backend.borderColor = value; root.changed() } } }
                                SettingRow { label: qsTr("Opacity")
                                    SpinBox { from: 0; to: 100; stepSize: 10; value: Math.round(backend.borderOpacity * 100)
                                        textFromValue: function(value) { return (value / 100).toFixed(2) }
                                        valueFromText: function(text) { return Math.round(parseFloat(text) * 100) }
                                        onValueModified: { backend.borderOpacity = value / 100; root.changed() } } }
                            }
                        }
                    }
                }

                Frame {
                    Layout.fillWidth: true
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
                            RowLayout {
                                Layout.fillWidth: true
                                Button {
                                    text: "+"
                                    enabled: false
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Add a new input mapping entry.")
                                }
                                Button {
                                    text: "−"
                                    enabled: false
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
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: qsTr("Input Sequence"); font.bold: true; Layout.fillWidth: true }
                                        Label { text: qsTr("Type"); font.bold: true; Layout.preferredWidth: 130 }
                                        Label { text: qsTr("Mapped Action"); font.bold: true; Layout.fillWidth: true }
                                    }
                                    Rectangle { Layout.fillWidth: true; height: 1; color: root.palette.mid }
                                    Item { Layout.fillHeight: true }
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
            ColumnLayout {
                width: parent.width
                GroupBox {
                    title: qsTr("Global Shortcuts")
                    Layout.fillWidth: true
                    ColumnLayout {
                        anchors.fill: parent
                        Repeater {
                            model: [
                                qsTr("Toggle Spotlight"),
                                qsTr("Show Preferences"),
                                qsTr("Start or Restart Presentation Timer"),
                                qsTr("Reset Presentation Timer"),
                                qsTr("Next Spotlight Preset"),
                                qsTr("Previous Spotlight Preset")
                            ]
                            delegate: Label {
                                required property string modelData
                                text: modelData
                                Layout.fillWidth: true
                            }
                        }
                        Button {
                            text: qsTr("Configure Global Shortcuts…")
                            icon.name: "configure-shortcuts"
                            onClicked: backend.configureGlobalShortcuts()
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
