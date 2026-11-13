// PackageInfoWindow.qml — the full `winget show` record for one package.
//
// The details pane has room for a summary; this dialog is for the whole thing:
// release notes, publisher contacts, tag list and installer internals. It opens as a
// a full-width modal panel in the owner window rather than a second native window.
// This keeps the dialog inside the host's QtQuick runtime and avoids a dependency
// on QtQuick.Window, which is not deployed by some WinShun host installations.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: root
    property int packageIndex: -1
    readonly property bool zh: controller.locale.startsWith("zh")
    function t(zhText, enText) { return zh ? zhText : enText }

    width: 720
    height: 640
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    anchors.centerIn: Overlay.overlay
    padding: 0

    readonly property var info: controller.packageInfo
    readonly property bool busy: controller.packageInfoBusy
    readonly property int rowCount: packagesModel ? packagesModel.rowCount() : 0
    readonly property bool hasRow: packageIndex >= 0 && packageIndex < rowCount
    function valueFor(key) { return hasRow ? packagesModel.fieldAt(packageIndex, key) : "" }

    // Field order is deliberate: what the package is, then who publishes it, then
    // how it installs. Release notes come last because they are the only unbounded
    // field and would otherwise push everything else off screen.
    readonly property var sections: [
        {
            title: t("身份", "Identity"),
            fields: [
                { label: t("名称", "Name"), key: "name", from: "row" },
                { label: t("精确 ID", "Exact ID"), key: "exactId", from: "row", mono: true },
                { label: t("管理器", "Manager"), key: "manager", from: "row" },
                { label: t("来源", "Source"), key: "source", from: "row" },
                { label: t("已装版本", "Installed"), key: "installed", from: "row", mono: true },
                { label: t("可用版本", "Available"), key: "available", from: "row", mono: true }
            ]
        },
        {
            title: t("发布者", "Publisher"),
            fields: [
                { label: t("发布者", "Publisher"), key: "publisher" },
                { label: t("作者", "Author"), key: "author" },
                { label: t("发布者网站", "Publisher URL"), key: "publisherUrl", url: true },
                { label: t("支持", "Support"), key: "publisherSupportUrl", url: true },
                { label: t("官网", "Homepage"), key: "homepage", url: true },
                { label: t("别名", "Moniker"), key: "moniker", mono: true }
            ]
        },
        {
            title: t("许可", "License"),
            fields: [
                { label: t("协议", "License"), key: "license" },
                { label: t("协议地址", "License URL"), key: "licenseUrl", url: true },
                { label: t("版权", "Copyright"), key: "copyright" },
                { label: t("隐私", "Privacy"), key: "privacyUrl", url: true }
            ]
        },
        {
            title: t("安装器", "Installer"),
            fields: [
                { label: t("类型", "Type"), key: "installerType", mono: true },
                { label: t("语言", "Locale"), key: "installerLocale", mono: true },
                { label: t("架构", "Architecture"), key: "installerArchitecture", mono: true },
                { label: t("发布日期", "Release date"), key: "releaseDate", mono: true },
                { label: t("下载地址", "Installer URL"), key: "installerUrl", url: true },
                { label: t("SHA256", "SHA256"), key: "installerSha256", mono: true, wrap: true }
            ]
        }
    ]

    // A field is shown only when it has a value: an empty row here would claim the
    // backend reported nothing, which is not the same as not reporting the field.
    function fieldValue(field) {
        if (field.from === "row")
            return valueFor(field.key)
        return String(root.info[field.key] || "")
    }

    function sectionHasValues(section) {
        for (let i = 0; i < section.fields.length; ++i) {
            if (fieldValue(section.fields[i]) !== "")
                return true
        }
        return false
    }

    background: Rectangle {
        color: theme.page
        border.color: theme.windowEdge
        border.width: 1
        radius: theme.cardRadius
    }

    Rectangle {
        anchors.fill: parent
        color: theme.page
        radius: theme.cardRadius

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // ---- header ---------------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: theme.tabHeight + 18
                color: theme.layer

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Text {
                            text: root.hasRow ? root.valueFor("name") : root.t("包信息", "Package metadata")
                            color: theme.text
                            font.family: theme.fontFamily
                            font.pixelSize: theme.baseFont
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: root.hasRow ? root.valueFor("exactId") : ""
                            visible: root.hasRow
                            color: theme.subtext
                            font.family: theme.monoFamily
                            font.pixelSize: theme.tinyFont
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    ThemedButton {
                        text: root.t("复制全部", "Copy all")
                        onClicked: controller.copyPackageInfo()
                    }
                    ThemedButton {
                        text: root.t("关闭", "Close")
                        onClicked: root.close()
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.divider }
            }

            // ---- body -----------------------------------------------------
            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: width
                contentHeight: bodyColumn.implicitHeight + 24
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                ColumnLayout {
                    id: bodyColumn
                    width: parent.width - 32
                    x: 16
                    y: 14
                    spacing: 12

                    Text {
                        Layout.fillWidth: true
                        visible: root.busy
                        text: root.t("正在读取包信息…", "Reading package metadata…")
                        color: theme.faint
                        font.family: theme.fontFamily
                        font.pixelSize: theme.metaFont
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: !root.busy && String(root.info["note"] || "").length > 0
                        wrapMode: Text.WordWrap
                        text: String(root.info["note"] || "")
                        color: theme.warn
                        font.family: theme.fontFamily
                        font.pixelSize: theme.metaFont
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: String(root.info["description"] || "").length > 0
                        wrapMode: Text.WordWrap
                        text: String(root.info["description"] || "")
                        color: theme.subtext
                        font.family: theme.fontFamily
                        font.pixelSize: theme.metaFont
                    }

                    Repeater {
                        model: root.sections
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 4
                            visible: root.sectionHasValues(modelData)

                            Text {
                                text: modelData.title
                                color: theme.text
                                font.family: theme.fontFamily
                                font.pixelSize: theme.smallFont
                                font.weight: Font.DemiBold
                                Layout.topMargin: 4
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: theme.divider
                            }
                            Repeater {
                                model: modelData.fields
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 10
                                    visible: root.fieldValue(modelData) !== ""

                                    Text {
                                        Layout.preferredWidth: 118
                                        text: modelData.label
                                        color: theme.faint
                                        font.family: theme.fontFamily
                                        font.pixelSize: theme.tinyFont
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: root.fieldValue(modelData)
                                        color: modelData.url ? theme.accentLine : theme.text
                                        font.family: modelData.mono ? theme.monoFamily : theme.fontFamily
                                        font.pixelSize: theme.tinyFont
                                        wrapMode: (modelData.wrap || modelData.url) ? Text.WrapAnywhere
                                                                                   : Text.NoWrap
                                        elide: (modelData.wrap || modelData.url) ? Text.ElideNone
                                                                                 : Text.ElideRight
                                        // A URL is third-party data: only an absolute
                                        // http(s) address is ever opened.
                                        MouseArea {
                                            anchors.fill: parent
                                            enabled: modelData.url === true
                                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                            onClicked: controller.openUrl(parent.text)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Tags are a list, not a paragraph: one chip per tag reads as
                    // a set, where "container, containerization, develop…" does not.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: String(root.info["tags"] || "").length > 0
                        Text {
                            text: root.t("标签", "Tags")
                            color: theme.text
                            font.family: theme.fontFamily
                            font.pixelSize: theme.smallFont
                            font.weight: Font.DemiBold
                            Layout.topMargin: 4
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: theme.divider
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: String(root.info["tags"] || "").split(/[\s,]+/)
                                            .filter(function(t) { return t.length > 0 })
                                delegate: Rectangle {
                                    required property string modelData
                                    width: tagText.implicitWidth + 12
                                    height: theme.tinyFont + 9
                                    radius: 8
                                    color: theme.tagFill
                                    Text {
                                        id: tagText
                                        anchors.centerIn: parent
                                        text: modelData
                                        color: theme.subtext
                                        font.family: theme.monoFamily
                                        font.pixelSize: theme.tinyFont
                                    }
                                }
                            }
                        }
                    }

                    // Release notes are the one field that can be long, and the one
                    // a user opens this window for when an upgrade misbehaves.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: String(root.info["releaseNotes"] || "").length > 0
                        Text {
                            text: root.t("发行说明", "Release notes")
                            color: theme.text
                            font.family: theme.fontFamily
                            font.pixelSize: theme.smallFont
                            font.weight: Font.DemiBold
                            Layout.topMargin: 4
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: theme.divider
                        }
                        Text {
                            Layout.fillWidth: true
                            text: String(root.info["releaseNotes"] || "")
                            color: theme.subtext
                            font.family: theme.fontFamily
                            font.pixelSize: theme.tinyFont
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.divider }
    }
}
