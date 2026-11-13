// RowDelegate.qml — one package row.
//
// The checkbox is a separate hit target from the row: checking selects a candidate
// for review, while clicking the row only shows details. Neither can start an
// action, so browsing can never change the machine.
//
// Geometry is fixed rather than content-driven: the list virtualises rows and
// needs a stable height, and a content-driven width here made the two text lines
// overlap while the layout settled.
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: root
    required property int index
    required property string name
    required property string exactId
    required property string manager
    required property string source
    required property string scope
    required property string installed
    required property string available
    required property string provenance
    required property string hold
    required property bool unknownVersion
    required property string duplicateIn
    required property bool checked
    required property bool actionable
    required property string limits

    property bool selected: false
    readonly property bool zh: controller.locale.startsWith("zh")
    function t(zhText, enText) { return zh ? zhText : enText }
    signal rowClicked(int index)
    signal checkboxToggled(int index)
    // Right-click opens the per-row action menu. The row reports the click
    // position so the menu can open where the user pointed.
    signal rowRightClicked(int index, real sceneX, real sceneY)

    implicitHeight: theme.rowHeight
    implicitWidth: 640

    Rectangle {
        anchors.fill: parent
        color: root.selected ? theme.selection : (rowMouse.containsMouse ? theme.hover : "transparent")
    }
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: theme.divider
    }
    // A left accent marks a checked row, matching the mockup.
    Rectangle {
        width: root.checked ? 2 : 0
        height: parent.height
        color: theme.accentLine
    }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton)
                root.rowRightClicked(root.index, mouse.x, mouse.y)
            else
                root.rowClicked(root.index)
        }
    }

    Rectangle {
        id: box
        x: 12
        y: (parent.height - 14) / 2
        width: 14
        height: theme.tinyFont + 2
        radius: 3
        border.color: theme.controlBorder
        border.width: 1
        color: root.checked ? theme.accentFill : theme.control
        opacity: root.actionable ? 1 : 0.35

        Text {
            anchors.centerIn: parent
            text: "✓"
            visible: root.checked
            color: theme.accentText
            font.pixelSize: theme.tinyFont
        }

        MouseArea {
            anchors.fill: parent
            enabled: root.actionable
            cursorShape: Qt.PointingHandCursor
            onClicked: root.checkboxToggled(root.index)
        }
    }

    // Line 1: display name and the exact id, on one baseline, each clipped so a
    // long id can never push the name away. When a backend reports no display
    // name, the row shows the id as the name and the id column is redundant, so
    // it is hidden rather than repeated.
    Text {
        id: nameText
        x: 34
        y: (parent.height - (theme.bodyFont + theme.smallFont + 8)) / 4
        width: Math.min(implicitWidth, Math.max(60, (root.width - 34 - 12) * 0.45))
        height: theme.bodyFont + 4
        text: root.name
        color: theme.text
        font.family: theme.fontFamily
        font.pixelSize: theme.baseFont
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }
    Text {
        id: idText
        x: nameText.x + nameText.width + 8
        visible: root.exactId !== root.name
        y: nameText.y
        width: Math.max(40, root.width - x - 12)
        height: theme.smallFont + 4
        text: root.exactId
        color: theme.subtext
        font.family: theme.monoFamily
        font.pixelSize: theme.smallFont
        elide: Text.ElideRight
    }

    // Line 2 is intentionally decision-oriented: manager, install state/current
    // version, and (only when known) an emphasized update target. Raw source,
    // scope and provenance remain in the details pane; values such as "unknown"
    // and "backend-recorded" are implementation facts, not useful row labels.
    Row {
        id: metaRow
        x: 34
        y: parent.height - theme.smallFont - 10
        width: Math.max(80, root.width - 34 - 12)
        height: theme.smallFont + 4
        spacing: 8

        Rectangle {
            width: managerLabel.width + 10
            height: theme.tinyFont + 5
            y: 1
            radius: 7
            color: "transparent"
            border.width: 1
            border.color: root.manager === "scoop" ? "#2b6f8f" : "#2f6b3a"
            Text {
                id: managerLabel
                anchors.centerIn: parent
                text: root.manager === "scoop" ? "Scoop" : "WinGet"
                color: root.manager === "scoop" ? "#2b6f8f" : "#2f6b3a"
                font.family: theme.fontFamily
                font.pixelSize: theme.tinyFont
            }
        }
        Text {
            height: theme.tinyFont + 5
            visible: root.source !== "" && root.source !== "unknown" && root.scope !== "unknown"
            text: root.source + " · " + root.scope
            color: theme.subtext
            font.family: theme.fontFamily
            font.pixelSize: theme.smallFont
        }
        // Version: only what is true. A row with no newer version shows just the
        // installed version — an "→ —" arrow to nothing is noise that made every
        // row look like it had an update. A row that can be upgraded shows the
        // transition with the target emphasised, so "there is an update" is
        // visible without reading the numbers.
        Row {
            id: versionRow
            height: theme.tinyFont + 5
            spacing: 5

            readonly property bool upgradable:
                root.available !== "" && root.available !== "—" &&
                root.available !== root.installed

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.installed === "" ? qsTr("未安装") : root.installed
                color: root.installed === "" ? theme.faint : theme.subtext
                font.family: theme.monoFamily
                font.pixelSize: theme.smallFont
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: versionRow.upgradable
                text: "→"
                color: theme.faint
                font.family: theme.monoFamily
                font.pixelSize: theme.smallFont
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: versionRow.upgradable
                width: availableLabel.implicitWidth + 10
                height: theme.tinyFont + 5
                radius: theme.smallControlRadius
                color: theme.accentFill
                Text {
                    id: availableLabel
                    anchors.centerIn: parent
                    text: root.available
                    color: theme.accentText
                    font.family: theme.monoFamily
                    font.pixelSize: theme.smallFont
                    font.weight: Font.DemiBold
                }
            }
        }
        Text {
            id: provenanceGlyph
            height: theme.tinyFont + 5
            verticalAlignment: Text.AlignVCenter
            visible: root.provenance === "backend-recorded" || root.provenance === "backend 记录"
                     || root.provenance === "observed" || root.provenance === "观察到"
            // A glyph, not a phrase: "管理器已登记" was four characters of jargon on
            // every row, occupying space that belongs to the version. The details
            // pane still spells it out for anyone who wonders.
            text: (root.provenance === "backend-recorded" || root.provenance === "backend 记录")
                  ? "\uE73E" : "\uE890"
            color: (root.provenance === "backend-recorded" || root.provenance === "backend 记录")
                   ? theme.ok : theme.warn
            font.family: "Segoe Fluent Icons"
            font.pixelSize: theme.smallFont + 2
            font.weight: Font.Normal
            HoverHandler { id: provenanceHover }
            ToolTip.visible: provenanceHover.hovered
            ToolTip.delay: 400
            ToolTip.text: (root.provenance === "backend-recorded" || root.provenance === "backend 记录")
                          ? root.t("包管理器登记了这个包", "The package manager records this package")
                          : root.t("系统安装记录，包管理器只观察到它",
                                   "A system install record the manager only observed")
        }
        Text {
            height: theme.tinyFont + 5
            visible: root.hold !== ""
            text: root.hold
            color: theme.warn
            font.family: theme.fontFamily
            font.pixelSize: theme.smallFont
        }
        Text {
            height: theme.tinyFont + 5
            visible: root.unknownVersion
            text: qsTr("目标版本无法比较")
            color: theme.warn
            font.family: theme.fontFamily
            font.pixelSize: theme.smallFont
        }
        Text {
            height: theme.tinyFont + 5
            visible: root.duplicateIn !== ""
            // A hint only. Nothing is merged and nothing is removed.
            text: qsTr("疑似在 %1 中重复").arg(root.duplicateIn)
            color: theme.warn
            font.family: theme.fontFamily
            font.pixelSize: theme.smallFont
        }
    }
}



