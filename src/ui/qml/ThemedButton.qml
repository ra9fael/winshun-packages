// ThemedButton.qml — a button that follows the host theme.
//
// The plugin sets QtQuick Controls' Basic style, whose default button is a light
// grey plate regardless of the host's theme. In a dark window that reads as a
// light blob with unreadable text. Every button in this plugin uses this component
// instead of a bare `Button`, so a theme change is handled in one place.
import QtQuick
import QtQuick.Controls.Basic

Button {
    id: control

    // Optional emphasis: "primary" for the confirming action, "danger" for a
    // destructive one, empty for an ordinary button.
    property string kind: ""
    // Optional Segoe Fluent Icons glyph shown before the label.
    property string glyph: ""

    implicitHeight: theme.smallFont + 16
    leftPadding: glyph !== "" ? 10 : 12
    rightPadding: 12

    font.family: theme.fontFamily
    font.pixelSize: theme.metaFont

    contentItem: Row {
        spacing: 6
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: control.glyph !== ""
            text: control.glyph
            color: control.enabled && control.kind === "primary"
                       ? theme.accentText
                       : control.enabled && control.kind === "danger"
                             ? theme.danger : theme.subtext
            font.family: "Segoe Fluent Icons"
            font.pixelSize: control.font.pixelSize
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            font: control.font
            elide: Text.ElideRight
            color: {
                if (!control.enabled)
                    return theme.faint
                if (control.kind === "primary")
                    return theme.accentText
                if (control.kind === "danger")
                    return theme.danger
                return theme.text
            }
        }
    }

    background: Rectangle {
        implicitHeight: control.implicitHeight
        radius: theme.controlRadius
        // Filled only for the emphasised kinds; otherwise the control colour, so a
        // button reads as a surface rather than a hole.
        color: {
            if (!control.enabled)
                return theme.tagFill
            if (control.kind === "primary")
                return control.down ? theme.accentLine : theme.accentFill
            if (control.kind === "danger")
                return theme.control
            return control.hovered ? theme.controlHover : theme.control
        }
        border.width: 1
        border.color: {
            if (!control.enabled)
                return theme.divider
            if (control.kind === "danger")
                return theme.danger
            if (control.kind === "primary")
                return theme.accentFill
            return theme.controlBorder
        }
    }
}
