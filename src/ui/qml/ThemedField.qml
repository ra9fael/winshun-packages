// ThemedField.qml — a text field that follows the host theme.
//
// Same reason as ThemedButton: the Basic style paints its TextField as a white
// plate, which is a bright rectangle in a dark window. This component owns the
// plate so the field is a surface of the current theme instead.
import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: control

    implicitHeight: theme.bodyFont + 16
    leftPadding: 10
    rightPadding: 10

    font.family: theme.fontFamily
    font.pixelSize: theme.metaFont
    color: theme.text
    placeholderTextColor: theme.faint
    selectionColor: theme.textSelection
    selectedTextColor: theme.text
    selectByMouse: true

    background: Rectangle {
        implicitHeight: control.implicitHeight
        radius: theme.controlRadius
        color: control.activeFocus ? theme.inputFocused : theme.control
        border.width: 1
        border.color: control.activeFocus ? theme.accentLine : theme.controlBorder
    }
}
