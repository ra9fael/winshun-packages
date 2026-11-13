// TasksPage.qml — the task queue.
//
// Every state the design distinguishes is shown with its own wording and its own
// recovery action. A partial failure from a reinstall says the package may be
// gone; a cancel request says it may only stop waiting; an interrupted task is
// never offered a silent replay.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The page is a Rectangle, not a bare Item: the layout inside paints only its own
// rows, so the gaps between them would show the window's default background (white
// in a dark theme). Painting the page itself is the one place that covers all of it.
Rectangle {
    id: root
    objectName: "tasksPage"
    color: theme.page
    readonly property bool zh: controller.locale.startsWith("zh")
    function t(zhText, enText) { return zh ? zhText : enText }

    // Log panels start collapsed; a user click gives that row an explicit value
    // and breaks the binding. The grab tool sets this true to capture the log
    // panel without a mouse.
    property bool expandAllLogs: false

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Queue controls. Pausing only stops the queue from picking up the next
        // task; it never touches the task that is already running, because that
        // would be a cancel and cancel is a separate, explicit action.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: theme.smallFont + 14
            color: theme.layer
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                Text {
                    text: root.t("队列", "Queue")
                    color: theme.subtext
                    font.family: theme.fontFamily
                    font.pixelSize: theme.tinyFont
                }
                ThemedButton {
                    text: controller.queuePaused ? root.t("继续队列", "Resume")
                                                 : root.t("暂停队列", "Pause")
                    onClicked: controller.toggleQueuePaused()
                }
                Text {
                    Layout.fillWidth: true
                    text: (controller.queuePaused ? root.t("已暂停", "paused") : root.t("空闲", "idle"))
                          + " · " + controller.activeTasks + " " + root.t("个进行中", "active")
                          + (controller.finishedTasks > 0
                             ? " · " + controller.finishedTasks + " " + root.t("个已结束", "finished")
                             : "")
                    color: theme.faint
                    font.family: theme.fontFamily
                    font.pixelSize: theme.tinyFont
                    elide: Text.ElideRight
                }
                ThemedButton {
                    // Clearing only drops terminal records; active work stays.
                    enabled: controller.finishedTasks > 0
                    text: controller.finishedTasks > 0
                          ? root.t("清空已完成（", "Clear finished (") + controller.finishedTasks + "）"
                          : root.t("清空已完成", "Clear finished")
                    onClicked: controller.clearFinishedTasks()
                }
                ThemedButton {
                    text: root.t("重新扫描", "Rescan")
                    onClicked: controller.refreshAll()
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.divider }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: theme.smallFont + 12
            color: theme.page
            Row {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 16
                Text {
                    text: "#"; color: theme.faint
                    font.family: theme.fontFamily; font.pixelSize: theme.tinyFont
                }
                Text {
                    text: root.t("管理器", "Manager"); color: theme.faint
                    font.family: theme.fontFamily; font.pixelSize: theme.tinyFont
                }
                Text {
                    text: root.t("精确目标 / 作用域", "Exact target / scope"); color: theme.faint
                    font.family: theme.fontFamily; font.pixelSize: theme.tinyFont
                }
                Text {
                    text: root.t("状态", "State"); color: theme.faint
                    font.family: theme.fontFamily; font.pixelSize: theme.tinyFont
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.divider }
        }

        ListView {
            id: taskList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: tasksModel
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            // Report the selection so the status line can name it. A background
            // tint alone is easy to lose when several rows look similar.
            onCurrentIndexChanged: {
                controller.setSelectedTask(tasksModel.idAt(currentIndex))
            }

            delegate: Item {
                required property int index
                required property var taskId
                required property string manager
                required property string target
                required property string scope
                required property string action
                required property string state
                required property string stateText
                required property string note
                required property string stage
                required property bool canRemove
                required property bool canCancel
                required property bool canRetry
                required property bool canStart
                required property bool indeterminate
                required property int logCount
                required property string logText

                // Log expansion is user state per row; re-rendering must not
                // collapse a log the user is reading. `expandAllLogs` only
                // exists so the grab tool can capture the panel without a mouse.
                property bool userOpened: false
                readonly property bool logOpen: userOpened || root.expandAllLogs

                width: taskList.width
                height: contentColumn.implicitHeight + 20

                readonly property bool selected: taskList.currentIndex === index

                Rectangle {
                    anchors.fill: parent
                    color: selected ? Qt.rgba(theme.accentFill.r, theme.accentFill.g, theme.accentFill.b, 0.14)
                                    : rowHover.containsMouse ? theme.hover : "transparent"
                }
                // A left accent marks the selected row, matching the list pages.
                Rectangle {
                    width: selected ? 2 : 0
                    height: parent.height
                    color: theme.accentLine
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: theme.divider
                }
                MouseArea {
                    id: rowHover
                    anchors.fill: parent
                    hoverEnabled: true
                    // The row can be selected: it is the task the status line and
                    // its own buttons refer to, and "which task am I about to
                    // cancel" needs an explicit answer. Clicking a button inside
                    // the row must not also change the selection, so the buttons
                    // sit above this area and consume their own clicks.
                    acceptedButtons: Qt.LeftButton
                    onClicked: taskList.currentIndex = index
                }

                ColumnLayout {
                    id: contentColumn
                    x: 12
                    y: 10
                    width: parent.width - 24
                    spacing: 3

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Text {
                            text: taskId
                            color: theme.faint
                            font.family: theme.fontFamily
                            font.pixelSize: theme.metaFont
                        }
                        Text {
                            text: manager === "scoop" ? "Scoop" : "WinGet"
                            color: manager === "scoop" ? "#2b6f8f" : "#2f6b3a"
                            font.family: theme.fontFamily
                            font.pixelSize: theme.metaFont
                        }
                        Text {
                            text: target
                            color: theme.text
                            font.family: theme.monoFamily
                            font.pixelSize: theme.metaFont
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: scope
                            color: theme.subtext
                            font.family: theme.fontFamily
                            font.pixelSize: theme.tinyFont
                        }
                        Text {
                            text: action
                            color: theme.subtext
                            font.family: theme.fontFamily
                            font.pixelSize: theme.tinyFont
                        }
                        Item { Layout.fillWidth: true }

                        // The state badge carries text, so it never depends on
                        // colour alone.
                        Rectangle {
                            implicitWidth: stateLabel.width + 12
                            implicitHeight: theme.smallFont + 6
                            radius: 9
                            color: "transparent"
                            border.width: 1
                            border.color: state === "succeeded" ? theme.ok
                                       : state === "failed" || state === "partial-failure" ||
                                         state === "interrupted" ? theme.danger
                                       : state === "running" ? theme.accentLine
                                                             : theme.warn
                            Text {
                                id: stateLabel
                                anchors.centerIn: parent
                                text: stateText
                                color: state === "succeeded" ? theme.ok
                                     : state === "failed" || state === "partial-failure" ||
                                       state === "interrupted" ? theme.danger
                                     : state === "running" ? theme.accentLine
                                                           : theme.warn
                                font.family: theme.fontFamily
                                font.pixelSize: theme.tinyFont
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: note !== ""
                        wrapMode: Text.WordWrap
                        text: note
                        color: state === "partial-failure" || state === "interrupted" ? theme.danger : theme.subtext
                        font.family: theme.fontFamily
                        font.pixelSize: theme.metaFont
                    }

                    // No reliable total exists, so an indeterminate bar is shown
                    // instead of a fabricated percentage.
                    Rectangle {
                        Layout.preferredWidth: 90
                        implicitHeight: 3
                        radius: 2
                        color: theme.tagFill
                        visible: indeterminate
                        Rectangle {
                            width: parent.width * 0.35
                            height: parent.height
                            radius: 2
                            color: theme.accentLine
                            SequentialAnimation on x {
                                running: indeterminate
                                loops: Animation.Infinite
                                NumberAnimation { from: -parent.width * 0.35; to: parent.width; duration: 1400 }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: stage !== ""
                        text: root.t("阶段：", "Stage: ") + stage
                        color: theme.faint
                        font.family: theme.monoFamily
                        font.pixelSize: theme.tinyFont
                    }

                    // The executor runs a plan in named phases; showing the chain
                    // (not just the current name) makes "where is it now, what is
                    // left" readable at a glance. A task that has not started has
                    // no phase yet and shows none, which is honest: it is not in
                    // acquire, it is waiting for its lane.
                    RowLayout {
                        spacing: 4
                        visible: stage !== ""
                        Repeater {
                            model: ["acquire", "install", "verify"]
                            delegate: RowLayout {
                                required property int index
                                required property string modelData
                                spacing: 4
                                Rectangle {
                                    radius: 999
                                    implicitWidth: phaseLabel.width + 14
                                    implicitHeight: theme.smallFont + 6
                                    color: "transparent"
                                    border.width: 1
                                    border.color: {
                                        const current = stage.indexOf(modelData) === 0
                                        if (current)
                                            return theme.accentLine
                                        return index === 0 && stage !== "" ? theme.ok : theme.controlBorder
                                    }
                                    Text {
                                        id: phaseLabel
                                        anchors.centerIn: parent
                                        text: modelData
                                        color: {
                                            const current = stage.indexOf(modelData) === 0
                                            if (current)
                                                return theme.accentLine
                                            return theme.faint
                                        }
                                        font.family: theme.monoFamily
                                        font.pixelSize: theme.tinyFont
                                    }
                                }
                                Text {
                                    visible: index < 2
                                    text: "›"
                                    color: theme.faint
                                    font.pixelSize: theme.tinyFont
                                }
                            }
                        }
                    }

                    // Actions are per row and address this task's id. A button
                    // that acts on "the last matching task" lets the wrong row be
                    // cancelled, which is exactly what this replaced.
                    RowLayout {
                        spacing: 5
                        ThemedButton {
                            visible: canStart
                            text: root.t("开始", "Start")
                            onClicked: controller.startTask(taskId)
                        }
                        ThemedButton {
                            visible: canCancel
                            text: root.t("取消", "Cancel")
                            kind: "danger"
                            onClicked: controller.cancelTask(taskId)
                        }
                        ThemedButton {
                            visible: canRetry
                            text: root.t("重试", "Retry")
                            onClicked: controller.retryTask(taskId)
                        }
                        ThemedButton {
                            visible: canRemove
                            text: root.t("移除", "Remove")
                            onClicked: controller.removeTask(taskId)
                        }
                        ThemedButton {
                            visible: logCount > 0
                            text: (logOpen ? root.t("收起日志", "Hide log") : root.t("查看日志", "Show log"))
                                  + "（" + logCount + "）"
                            onClicked: userOpened = !userOpened
                        }
                    }

                    // The per-step log: what ran, what came back, why it stopped.
                    // Every line was redacted before it was stored, so there is
                    // nothing here to sanitise again at display time.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: logOpen && logCount > 0
                                                ? Math.min(logColumn.implicitHeight + 8, 190) : 0
                        visible: logOpen && logCount > 0
                        clip: true
                        radius: 4
                        color: theme.control
                        border.width: 1
                        border.color: theme.controlBorder

                        Flickable {
                            anchors.fill: parent
                            anchors.margins: 4
                            contentWidth: width
                            contentHeight: logColumn.implicitHeight
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar {}

                            ColumnLayout {
                                id: logColumn
                                width: parent.width
                                spacing: 1
                                Repeater {
                                    // One line per entry: at \t phase \t kind \t text \t exit
                                    model: logText.length ? logText.split("\n") : []
                                    delegate: RowLayout {
                                        required property string modelData
                                        Layout.fillWidth: true
                                        spacing: 6
                                        readonly property var parts: modelData.split("\t")
                                        Text {
                                            text: parent.parts.length > 0 ? parent.parts[0] : ""
                                            color: theme.faint
                                            font.family: theme.monoFamily
                                            font.pixelSize: theme.tinyFont
                                        }
                                        Text {
                                            text: parent.parts.length > 1 ? parent.parts[1] : ""
                                            color: theme.subtext
                                            font.family: theme.monoFamily
                                            font.pixelSize: theme.tinyFont
                                            Layout.preferredWidth: 56
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: (parent.parts.length > 3 ? parent.parts[3] : "")
                                                  + (parent.parts.length > 4 && parent.parts[4] !== "-1"
                                                     ? "  [exit " + parent.parts[4] + "]" : "")
                                            color: {
                                                const kind = parent.parts.length > 2 ? parent.parts[2] : "info"
                                                if (kind === "error") return theme.danger
                                                if (kind === "warning") return theme.warn
                                                if (kind === "ok") return theme.ok
                                                return theme.text
                                            }
                                            font.family: theme.monoFamily
                                            font.pixelSize: theme.tinyFont
                                            wrapMode: Text.WrapAnywhere
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // The log block: phase, time, exit code and a redacted fragment. Nothing
        // here is replayable shell text.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: theme.tabHeight + 2
            color: theme.layer
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: theme.divider }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10
                Text {
                    text: root.t("日志", "Log")
                    color: theme.subtext
                    font.family: theme.fontFamily
                    font.pixelSize: theme.metaFont
                }
                Text {
                    Layout.fillWidth: true
                    // Only redacted fragments are kept, so export is safe by
                    // construction.
                    text: root.t("只保留脱敏的阶段、退出码与错误片段",
                                 "Redacted phases, exit codes and error fragments only")
                    color: theme.faint
                    font.family: theme.fontFamily
                    font.pixelSize: theme.tinyFont
                    elide: Text.ElideRight
                }
            }
        }
    }
}






