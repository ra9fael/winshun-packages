// ReviewDialog.qml — the confirmation page.
//
// The gate this file enforces, in order:
//   1. nothing is confirmable until preflight passed;
//   2. each outstanding agreement is a separate checkbox, unchecked by default,
//      and the confirm button stays disabled until every one is checked;
//   3. choosing confirm and queue does not run anything: it records consent and
//      puts a confirmed plan in the queue, which re-checks before running.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    visible: controller.reviewVisible
    color: Qt.rgba(0, 0, 0, 0.55)
    z: 100

    readonly property bool zh: controller.locale.startsWith("zh")
    function t(zhText, enText) { return zh ? zhText : enText }

    // A click on the dim area cancels; it never confirms.
    MouseArea {
        anchors.fill: parent
        onClicked: controller.closeReview()
    }

    Rectangle {
        id: dialog
        width: 560
        height: Math.min(560, root.height - 44)
        anchors.centerIn: parent
        radius: theme.cardRadius
        color: theme.layer
        border.color: theme.windowEdge
        border.width: 1

        // Swallow clicks so the dim-area handler does not close the dialog.
        MouseArea { anchors.fill: parent }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // ---- header -----------------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: theme.tabHeight + 6
                color: theme.layer
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 8
                    Rectangle {
                        visible: controller.reviewAction !== ""
                        implicitWidth: opLabel.width + 10
                        implicitHeight: 16
                        radius: 3
                        color: theme.accentFill
                        Text {
                            id: opLabel
                            anchors.centerIn: parent
                            text: controller.reviewAction
                            color: theme.accentText
                            font.family: theme.fontFamily
                            font.pixelSize: theme.tinyFont
                            font.weight: Font.DemiBold
                        }
                    }
                    Text {
                        text: root.t("审阅操作", "Review action")
                        color: theme.text
                        font.family: theme.fontFamily
                        font.pixelSize: theme.smallFont
                        font.weight: Font.DemiBold
                    }
                    Item { Layout.fillWidth: true }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.divider }
            }

            // ---- body -------------------------------------------------------
            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentHeight: bodyColumn.height
                clip: true
                ScrollBar.vertical: ScrollBar {}

                ColumnLayout {
                    id: bodyColumn
                    width: parent.width - 28
                    x: 14
                    spacing: 6

                    Item { implicitHeight: 6 }

                    // The action selector. Upgrade and uninstall are independent
                    // intents: an installed package with an update available can
                    // do either, so the plugin offers both instead of deciding.
                    // Only legal actions appear, and a change recomputes every
                    // fact below (method, material, verification, risk).
                    RowLayout {
                        Layout.fillWidth: true
                        visible: controller.reviewActions.length > 0
                        spacing: 6

                        Text {
                            text: root.t("动作", "Action")
                            color: theme.faint
                            font.family: theme.fontFamily
                            font.pixelSize: theme.tinyFont
                        }
                        Repeater {
                            model: controller.reviewActions
                            delegate: ThemedButton {
                                required property var modelData
                                readonly property bool on:
                                    controller.reviewAction.toLowerCase() === modelData.id
                                text: modelData.label
                                checkable: true
                                checked: on
                                kind: on ? "primary" : ""
                                onClicked: controller.setReviewAction(modelData.id)
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            // Why this action exists for this row, so the choice is
                            // informed rather than guessed.
                            text: {
                                for (let i = 0; i < controller.reviewActions.length; ++i) {
                                    const a = controller.reviewActions[i]
                                    if (a.id === controller.reviewAction.toLowerCase())
                                        return a.why
                                }
                                return ""
                            }
                            color: theme.faint
                            font.family: theme.fontFamily
                            font.pixelSize: theme.tinyFont
                            elide: Text.ElideRight
                        }
                    }

                    // A blocked plan explains itself and offers nothing to click.
                    Text {
                        Layout.fillWidth: true
                        visible: controller.blockedReason !== ""
                        wrapMode: Text.WordWrap
                        text: controller.blockedReason
                        color: theme.danger
                        font.family: theme.fontFamily
                        font.pixelSize: theme.metaFont
                    }

                    Repeater {
                        model: [
                            { k: "manager", zh: "管理器", en: "Manager" },
                            { k: "source", zh: "来源", en: "Source" },
                            { k: "id", zh: "精确 ID", en: "Exact ID" },
                            { k: "scope", zh: "作用域", en: "Scope" },
                            { k: "installed", zh: "已装", en: "Installed" },
                            { k: "target", zh: "目标", en: "Target" },
                            { k: "method", zh: "方式", en: "Method" },
                            { k: "material", zh: "材料", en: "Material" },
                            { k: "dependencies", zh: "依赖", en: "Dependencies" },
                            { k: "policy", zh: "策略", en: "Policy" },
                            // What the run will be judged by: an exit code is never
                            // the verdict, and the page says so before the user
                            // agrees to anything.
                            { k: "verify", zh: "结果核对", en: "Verification" },
                            { k: "interactive", zh: "交互", en: "Interactive" },
                            { k: "metadataAt", zh: "元数据", en: "Metadata" }
                        ]
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            visible: controller.reviewFacts[modelData.k] !== undefined
                                     && String(controller.reviewFacts[modelData.k]) !== ""
                            Text {
                                Layout.preferredWidth: 76
                                text: root.t(modelData.zh, modelData.en)
                                color: theme.faint
                                font.family: theme.fontFamily
                                font.pixelSize: theme.tinyFont
                            }
                            Text {
                                Layout.fillWidth: true
                                text: controller.reviewFacts[modelData.k] !== undefined
                                      ? controller.reviewFacts[modelData.k] : "—"
                                color: theme.text
                                wrapMode: Text.WordWrap
                                font.family: theme.fontFamily
                                font.pixelSize: theme.metaFont
                            }
                        }
                    }

                    Item { implicitHeight: 4 }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: controller.reviewFacts["risk"] !== undefined ? controller.reviewFacts["risk"] : ""
                        color: theme.danger
                        font.family: theme.fontFamily
                        font.pixelSize: theme.metaFont
                    }

                    // ---- agreements, each explicitly opt-in -----------------
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        visible: controller.needsSourceAgreement
                        implicitHeight: sourceRow.implicitHeight + 14
                        radius: theme.controlRadius
                        color: theme.control
                        border.width: 1
                        border.color: controller.sourceAgreed ? theme.controlBorder : theme.warn

                        RowLayout {
                            id: sourceRow
                            anchors.fill: parent
                            anchors.margins: 7
                            spacing: 8
                            ThemedCheckBox {
                                id: sourceCheck
                                checked: controller.sourceAgreed
                                // Never pre-checked: consent is always an explicit act.
                                onToggled: controller.setSourceAgreed(checked)
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Text {
                                    text: root.t("源协议（source agreement）", "Source agreement")
                                    color: theme.text
                                    font.family: theme.fontFamily
                                    font.pixelSize: theme.metaFont
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: controller.sourceAgreementText
                                    color: theme.faint
                                    font.family: theme.fontFamily
                                    font.pixelSize: theme.tinyFont
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: installerRow.implicitHeight + 14
                        radius: theme.controlRadius
                        color: theme.control
                        border.width: 1
                        border.color: controller.installerAgreed ? theme.controlBorder : theme.warn

                        RowLayout {
                            id: installerRow
                            anchors.fill: parent
                            anchors.margins: 7
                            spacing: 8
                            ThemedCheckBox {
                                id: installerCheck
                                checked: controller.installerAgreed
                                onToggled: controller.setInstallerAgreed(checked)
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Text {
                                    // Uninstall has no installer to agree with; it
                                    // asks for an acknowledgement of the impact.
                                    text: controller.uninstallReview
                                          ? root.t("理解影响（impact acknowledgement）", "Impact acknowledgement")
                                          : root.t("安装器协议（installer agreement）", "Installer agreement")
                                    color: theme.text
                                    font.family: theme.fontFamily
                                    font.pixelSize: theme.metaFont
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: controller.installerAgreementText
                                    color: theme.faint
                                    font.family: theme.fontFamily
                                    font.pixelSize: theme.tinyFont
                                }
                            }
                        }
                    }

                    Item { implicitHeight: 6 }
                }
            }

            // ---- footer -----------------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: theme.bodyFont + 28
                color: theme.page
                Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: theme.divider }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: controller.reviewGateText
                        color: controller.reviewConfirmEnabled ? theme.ok : theme.faint
                        font.family: theme.fontFamily
                        font.pixelSize: theme.tinyFont
                        elide: Text.ElideRight
                    }
                    ThemedButton {
                        text: root.t("取消", "Cancel")
                        onClicked: controller.closeReview()
                    }
                    ThemedButton {
                        text: root.t("确认并加入队列", "Confirm and queue")
                        kind: "primary"
                        enabled: controller.reviewConfirmEnabled
                        onClicked: controller.confirmAndQueue()
                    }
                }
            }
        }
    }
}






