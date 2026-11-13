// DetailPane.qml — the full identity of the selected row.
//
// Every field the design requires is shown, and anything the backend did not
// report says "unknown" explicitly instead of being left blank: a blank field
// reads as "no restriction", which is the opposite of the truth.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Flickable {
    id: root
    required property int packageIndex
    readonly property bool zh: controller.locale.startsWith("zh")
    function t(zhText, enText) { return zh ? zhText : enText }

    contentWidth: width
    contentHeight: body.height
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    // Row data comes from the model, never from a second copy of the state. The
    // model resets on every query, so the bindings below re-evaluate then.
    readonly property int rowCount: packagesModel ? packagesModel.rowCount() : 0
    readonly property bool hasRow: packageIndex >= 0 && packageIndex < rowCount

    // Named lookup through the model, which resolves the role itself: QML cannot
    // recompute `Qt::UserRole + n` reliably across a context-property model.
    function valueFor(key) {
        return hasRow ? packagesModel.fieldAt(packageIndex, key) : ""
    }

    Column {
        id: body
        width: root.width
        spacing: 2

        Text {
            width: parent.width
            visible: !root.hasRow
            wrapMode: Text.WordWrap
            text: root.t("从左侧选择一项，查看完整身份、来源证据、依赖与能力限制。查询不会安装任何东西。",
                         "Pick a row to see its full identity, source evidence, dependencies and capability limits. " +
                         "Querying never installs anything.")
            color: theme.faint
            font.family: theme.fontFamily
            font.pixelSize: theme.metaFont
        }

        Text {
            width: parent.width
            visible: root.hasRow
            text: root.valueFor("name")
            color: theme.text
            font.family: theme.fontFamily
            font.pixelSize: theme.baseFont
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            visible: root.hasRow
            text: root.t("详情 · 归属 ", "Details · provenance ") + root.valueFor("provenance")
            color: theme.faint
            font.family: theme.fontFamily
            font.pixelSize: theme.tinyFont
        }

        // ---- key/value block ------------------------------------------------
        Column {
            width: parent.width
            topPadding: 8
            spacing: 4
            visible: root.hasRow

            Repeater {
                model: [
                    { label: root.t("管理器", "Manager"), key: "manager" },
                    { label: root.t("来源", "Source"), key: "source" },
                    // "exactId" is the name fieldAt() resolves; "id" was silently
                    // unknown, so this row rendered "—" and the exact identity the
                    // whole design depends on was invisible.
                    { label: root.t("精确 ID", "Exact ID"), key: "exactId" },
                    { label: root.t("作用域", "Scope"), key: "scope" },
                    { label: root.t("架构", "Architecture"), key: "arch" },
                    { label: root.t("已装", "Installed"), key: "installed" },
                    { label: root.t("可用", "Available"), key: "available" }
                ]
                delegate: Row {
                    required property var modelData
                    width: body.width
                    height: theme.smallFont + 5
                    spacing: 8
                    Text {
                        width: 74
                        height: theme.smallFont + 5
                        verticalAlignment: Text.AlignVCenter
                        text: modelData.label
                        color: theme.faint
                        font.family: theme.fontFamily
                        font.pixelSize: theme.tinyFont
                    }
                    Text {
                        width: Math.max(60, body.width - 90)
                        height: theme.smallFont + 5
                        verticalAlignment: Text.AlignVCenter
                        text: root.valueFor(modelData.key)
                        color: theme.text
                        font.family: theme.monoFamily
                        font.pixelSize: theme.tinyFont
                        elide: Text.ElideRight
                    }
                }
            }
        }

        // ---- winget show ------------------------------------------------
        // The publisher, description and real homepage come from the backend's
        // own metadata, loaded as soon as the row is selected. The pane used to
        // show only a button, so a package with rich metadata looked empty.
        Column {
            id: infoBlock
            width: parent.width
            topPadding: 10
            spacing: 3
            visible: root.hasRow

            readonly property var info: controller.packageInfo
            readonly property bool busy: controller.packageInfoBusy
            readonly property var infoKeys: ["publisher", "license", "moniker", "installerType", "homepage"]

            Text {
                width: parent.width
                text: infoBlock.busy ? root.t("正在读取包信息…", "Reading package metadata…")
                                     : root.t("包信息", "Package metadata")
                color: theme.faint
                font.family: theme.fontFamily
                font.pixelSize: theme.tinyFont
            }
            Text {
                width: parent.width
                visible: !infoBlock.busy && String(infoBlock.info["note"] || "").length > 0
                wrapMode: Text.WordWrap
                text: visible ? String(infoBlock.info["note"]) : ""
                color: theme.warn
                font.family: theme.fontFamily
                font.pixelSize: theme.tinyFont
            }
            Text {
                width: parent.width
                visible: String(infoBlock.info["description"] || "").length > 0
                wrapMode: Text.WordWrap
                text: String(infoBlock.info["description"] || "")
                color: theme.subtext
                font.family: theme.fontFamily
                font.pixelSize: theme.tinyFont
            }
            Repeater {
                model: infoBlock.infoKeys
                delegate: Row {
                    required property string modelData
                    width: infoBlock.width
                    visible: String(infoBlock.info[modelData] || "").length > 0
                    height: visible ? theme.smallFont + 4 : 0
                    spacing: 8
                    Text {
                        width: 74
                        height: theme.smallFont + 4
                        verticalAlignment: Text.AlignVCenter
                        text: modelData === "publisher" ? root.t("发布者", "Publisher")
                            : modelData === "license" ? root.t("协议", "License")
                            : modelData === "moniker" ? root.t("别名", "Moniker")
                            : modelData === "installerType" ? root.t("安装器", "Installer")
                            : root.t("官网", "Homepage")
                        color: theme.faint
                        font.family: theme.fontFamily
                        font.pixelSize: theme.tinyFont
                    }
                    Text {
                        width: Math.max(60, infoBlock.width - 90)
                        height: theme.smallFont + 4
                        verticalAlignment: Text.AlignVCenter
                        text: String(infoBlock.info[modelData] || "")
                        color: theme.text
                        font.family: modelData === "homepage" ? theme.fontFamily : theme.monoFamily
                        font.pixelSize: theme.tinyFont
                        elide: Text.ElideMiddle
                    }
                }
            }
        }

        // ---- actions ---------------------------------------------------------
        // Only the actions this row can actually take. A row that is not installed
        // has no upgrade and nothing to uninstall, so it offers install and a way
        // to look the package up; offering the other two as disabled buttons would
        // suggest operations that cannot exist for it.
        Row {
            width: parent.width
            topPadding: 12
            spacing: 6
            visible: root.hasRow

            readonly property bool installed: root.valueFor("installed") !== ""
                                              && root.valueFor("installed") !== "—"

            ThemedButton {
                visible: !parent.installed
                text: root.t("安装…", "Install…")
                kind: "primary"
                onClicked: controller.openReviewFor(root.packageIndex, "install")
            }
            ThemedButton {
                visible: parent.installed && root.valueFor("available") !== "—"
                         && root.valueFor("available") !== root.valueFor("installed")
                text: root.t("升级…", "Upgrade…")
                kind: "primary"
                onClicked: controller.openReviewFor(root.packageIndex, "upgrade")
            }
            ThemedButton {
                visible: parent.installed
                text: root.t("卸载…", "Uninstall…")
                kind: "danger"
                onClicked: controller.openReviewFor(root.packageIndex, "uninstall")
            }
            ThemedButton {
                // Fetches `winget show` first when this row's homepage is not known
                // yet; openHomepage() opens the real URL once it is.
                text: controller.packageInfoBusy ? root.t("查询中…", "Looking up…")
                                                 : root.t("官网 / 项目页", "Project page")
                enabled: !controller.packageInfoBusy
                onClicked: controller.openHomepage(root.packageIndex)
            }
        }
    }
}








