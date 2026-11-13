// RowMenu.qml — the per-row action menu.
//
// A row could only be acted on by first checking it and then using the selection
// bar, which meant a single-package operation required a multi-select gesture.
// Right-clicking a row now offers exactly the actions that row can take, so a
// package with no newer version does not offer an upgrade and an uninstalled
// search result does not offer an uninstall.
//
// The menu is a Popup, not a Menu: it needs to open at a scene position reported
// by the delegate, and it must not steal the list's keyboard handling.
import QtQuick
import QtQuick.Controls.Basic

Popup {
    id: menu

    property int rowIndex: -1
    // What the row can do, resolved by the controller so the menu and the review
    // page can never disagree about which actions are legal.
    property bool canUpgrade: false
    property bool canUninstall: false
    property bool canInstall: false
    property string rowName: ""
    property string rowId: ""

    signal upgradeRequested(int index)
    signal uninstallRequested(int index)
    signal installRequested(int index)
    signal homepageRequested(int index)
    signal detailsRequested(int index)
    signal copyIdRequested(string id)
    signal addToSelectionRequested(int index)

    // Opens under the pointer, clamped so the menu never leaves the window.
    function openAt(index, x, y) {
        rowIndex = index
        rowName = packagesModel.fieldAt(index, "name")
        rowId = packagesModel.keyAt(index)
        // Capability comes from the model, which derives it from the same Package
        // fields the plan builder uses. Recomputing it here from version strings
        // would let the menu and the review page disagree.
        const caps = packagesModel.capabilitiesAt(index)
        canUpgrade = caps.canUpgrade === true
        canUninstall = caps.canUninstall === true
        canInstall = caps.canInstall === true
        // The delegate reports coordinates inside itself; map them to the window
        // so the popup lands where the user clicked.
        const scenePoint = mapFromItem(null, x, y)
        open()
        // Popup positions itself from its parent; nudging after open() is what
        // makes it follow the pointer rather than the parent's corner.
        menu.x = Math.max(4, Math.min(scenePoint.x, parent ? parent.width - menu.width - 4 : scenePoint.x))
        menu.y = Math.max(4, Math.min(scenePoint.y, parent ? parent.height - menu.height - 4 : scenePoint.y))
    }

    padding: 4
    modal: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        color: theme.layer
        border.width: 1
        border.color: theme.windowEdge
        radius: theme.cardRadius
    }

    contentItem: Column {
        spacing: 0

        Text {
            width: parent.width
            leftPadding: 10
            rightPadding: 10
            topPadding: 4
            bottomPadding: 6
            text: menu.rowName
            color: theme.faint
            font.family: theme.monoFamily
            font.pixelSize: theme.tinyFont
            elide: Text.ElideRight
        }

        // One delegate for every item so the menu cannot drift in appearance from
        // the rest of the window.
        component MenuItem: Rectangle {
            id: item
            property string label: ""
            property string shortcut: ""
            property bool enabled: true
            property bool danger: false
            signal activated()

            width: menu.width - 8
            height: theme.smallFont + 16
            radius: theme.smallControlRadius
            color: item.enabled && itemMouse.containsMouse ? theme.hover : "transparent"

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: item.label
                color: !item.enabled ? theme.faint : item.danger ? theme.danger : theme.text
                font.family: theme.fontFamily
                font.pixelSize: theme.metaFont
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: item.shortcut
                color: theme.faint
                font.family: theme.monoFamily
                font.pixelSize: theme.tinyFont
            }
            MouseArea {
                id: itemMouse
                anchors.fill: parent
                hoverEnabled: true
                enabled: item.enabled
                cursorShape: item.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: {
                    menu.close()
                    item.activated()
                }
            }
        }

        MenuItem {
            label: qsTr("查看详情")
            shortcut: "Enter"
            onActivated: menu.detailsRequested(menu.rowIndex)
        }
        MenuItem {
            label: qsTr("复制精确 ID")
            shortcut: "Ctrl+C"
            onActivated: menu.copyIdRequested(menu.rowId)
        }
        Rectangle { width: parent.width; height: 1; color: theme.divider }

        // An action is offered only when it can happen. Showing "升级（无可用版本）"
        // greyed out suggests an operation that does not exist for this row; for an
        // uninstalled search result the only real actions are install and looking
        // the package up.
        MenuItem {
            label: qsTr("升级…")
            visible: menu.canUpgrade
            enabled: menu.canUpgrade
            onActivated: menu.upgradeRequested(menu.rowIndex)
        }
        MenuItem {
            label: qsTr("卸载…")
            visible: menu.canUninstall
            enabled: menu.canUninstall
            danger: menu.canUninstall
            onActivated: menu.uninstallRequested(menu.rowIndex)
        }
        MenuItem {
            label: qsTr("安装…")
            visible: menu.canInstall
            enabled: menu.canInstall
            onActivated: menu.installRequested(menu.rowIndex)
        }
        MenuItem {
            label: qsTr("官网 / 项目页")
            onActivated: menu.homepageRequested(menu.rowIndex)
        }
        Rectangle { width: parent.width; height: 1; color: theme.divider }

        MenuItem {
            label: qsTr("加入选择")
            onActivated: menu.addToSelectionRequested(menu.rowIndex)
        }
    }
}
