import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

Button {
    id: root
    property color selectedColor: "white"
    signal colorAccepted(color value)

    implicitWidth: 90
    contentItem: Rectangle {
        implicitHeight: 22
        radius: 3
        color: root.selectedColor
        border.color: Qt.darker(root.selectedColor, 1.5)
    }
    onClicked: dialog.open()

    ColorDialog {
        id: dialog
        selectedColor: root.selectedColor
        onAccepted: root.colorAccepted(selectedColor)
    }
}
