import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root
    required property string label
    default property alias content: contentSlot.data

    Label {
        text: root.label
        Layout.preferredWidth: 105
    }
    RowLayout {
        id: contentSlot
        Layout.fillWidth: true
    }
}
