// ThemedCheckBox.qml — a check box that follows the host theme.
//
// Same reason as ThemedButton and ThemedField: the Basic style draws its indicator
// as a light plate, which is a bright square with an invisible tick in a dark
// window. The indicator is redrawn from theme tokens here.
import QtQuick
import QtQuick.Controls.Basic

CheckBox {
    id: control

    font.family: theme.fontFamily
    font.pixelSize: theme.metaFont
    spacing: 6

    indicator: Rectangle {
        implicitWidth: 14
        implicitHeight: 14
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: 3
        color: control.checked ? theme.accentFill : theme.control
        border.width: 1
        border.color: control.checked ? theme.accentFill : theme.controlBorder
        opacity: control.enabled ? 1 : 0.35

        Text {
            anchors.centerIn: parent
            visible: control.checked
            text: "✓"
            color: theme.accentText
            font.pixelSize: theme.tinyFont
        }
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? theme.text : theme.faint
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
        leftPadding: control.indicator.width + control.spacing
        visible: control.text !== ""
    }
}
