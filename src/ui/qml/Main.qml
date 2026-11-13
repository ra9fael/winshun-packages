// Main.qml — the Packages window.
//
// Layout mirrors the confirmed mockup (mockup/index.html): title bar, four tabs,
// filter row, evidence strip, list + details split, footer, and the review dialog
// as an overlay. Nothing here starts an action on its own; the only way to change
// the machine is Review → explicit agreement → Confirm and queue.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: false
    width: 980
    height: 720
    minimumWidth: 720
    minimumHeight: 520
    color: theme.background
    title: qsTr("软件包")
    flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowCloseButtonHint

    readonly property bool zh: controller.locale.startsWith("zh")
    function t(zhText, enText) { return zh ? zhText : enText }

    // Which tabs are searchable. Installed needs it for a machine with hundreds of
    // packages; Discover is a search by definition.
    readonly property bool searchableTab: controller.tab === "installed"
                                          || controller.tab === "discover"
    // Only Discover runs a query; on Installed the box filters what is already
    // listed, which needs no backend call.
    readonly property bool searchQueriesBackend: controller.tab === "discover"

    // Closing the window only hides it: the resident plugin keeps its cache, and
    // the host decides when the process ends.
    onClosing: function(close) { close.accepted = false; window.visible = false }

    // ---------------------------------------------------------------- title bar
    header: Rectangle {
        height: theme.headerHeight - theme.bodyFont + theme.smallFont
        color: theme.layer

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10

            Rectangle {
                width: 18; height: 18; radius: theme.controlRadius
                color: theme.accentFill
                Text {
                    anchors.centerIn: parent
                    text: "P"
                    color: theme.accentText
                    font.pixelSize: theme.metaFont
                    font.weight: Font.Bold
                }
            }
            Text {
                text: window.t("软件包", "Packages")
                color: theme.text
                font.family: theme.fontFamily
                font.pixelSize: theme.baseFont
                font.weight: Font.DemiBold
            }
            Rectangle {
                implicitWidth: chipText.width + 14
                implicitHeight: theme.smallFont + 6
                radius: height / 2
                color: theme.tagFill
                Text {
                    id: chipText
                    anchors.centerIn: parent
                    // States the actual rights, not an intention: with the
                    // manifest's `elevated` permission this reads 管理员权限
                    // exactly when the host runs the plugin elevated.
                    text: controller.elevated
                          ? window.t("管理员权限", "Administrator")
                          : window.t("普通权限 · 未提权", "Standard rights · not elevated")
                    color: theme.subtext
                    font.family: theme.fontFamily
                    font.pixelSize: theme.metaFont
                }
            }
            Item { Layout.fillWidth: true }

            ThemedButton {
                text: window.t("刷新", "Refresh")
                enabled: !controller.busy
                onClicked: controller.refreshAll()
            }
            ThemedButton {
                text: window.t("设置", "Settings")
                onClicked: settingsPopup.open()
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.divider }
    }

    // -------------------------------------------------------------------- tabs
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            // ColumnLayout owns child geometry: a plain `height:` is ignored and
            // the bar collapses to 0, which is what stacked everything on top of
            // everything in the real host. Every fixed-height child of this
            // ColumnLayout uses Layout.preferredHeight.
            Layout.preferredHeight: theme.tabHeight + 2
            color: theme.layer

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                Repeater {
                    model: [
                        { key: "installed", zh: "已安装", en: "Installed" },
                        { key: "discover", zh: "发现", en: "Discover" },
                        { key: "updates", zh: "更新", en: "Updates" },
                        { key: "tasks", zh: "任务", en: "Tasks" }
                    ]
                    delegate: Item {
                        required property var modelData
                        width: tabLabel.width + 24
                        height: theme.tabHeight + 2
                        Rectangle {
                            anchors.fill: parent
                            color: tabMouse.containsMouse ? theme.hover : "transparent"
                        }
                        Text {
                            id: tabLabel
                            anchors.centerIn: parent
                            text: window.t(modelData.zh, modelData.en) +
                                  (modelData.key === "tasks" && controller.activeTasks > 0
                                       ? " " + controller.activeTasks : "")
                            color: controller.tab === modelData.key ? theme.text : theme.subtext
                            font.family: theme.fontFamily
                            font.pixelSize: theme.smallFont
                            font.weight: controller.tab === modelData.key ? Font.DemiBold : Font.Normal
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 2
                            color: controller.tab === modelData.key ? theme.accentLine : "transparent"
                        }
                        MouseArea {
                            id: tabMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: controller.setTab(modelData.key)
                        }
                    }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.divider }
        }

        // ----------------------------------------------------------- filter row
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: theme.tabHeight + 6
            color: theme.layer
            visible: controller.tab !== "tasks"

            Row {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: window.t("管理器", "Backend")
                    color: theme.faint
                    font.family: theme.fontFamily
                    font.pixelSize: theme.metaFont
                }
                // Multi-select segmented control instead of a ComboBox: Scoop and
                // WinGet toggle independently (one click, no popup), and "全部"
                // is a reset shortcut that turns both on. A button lights up when
                // its manager is currently in the list, so the filter state reads
                // directly off the buttons.
                //
                // In a plain Row, not a RowLayout: a RowLayout sizes its children
                // from implicit widths and a Rectangle whose inner Row is centred
                // on it then measured ~130 px too wide, which left a 137 px phantom
                // gap between the label and the buttons. A Row places children by
                // their real widths, so the buttons sit where they are drawn.
                Rectangle {
                    id: managerSeg
                    anchors.verticalCenter: parent.verticalCenter
                    width: segRow.implicitWidth + 4
                    height: theme.smallFont + 12
                    radius: theme.controlRadius
                    color: theme.tagFill
                    border.width: 1
                    border.color: theme.controlBorder

                    Row {
                        id: segRow
                        anchors.centerIn: parent
                        spacing: 2

                        Repeater {
                            model: [
                                { key: "all", zh: "全部", en: "All" },
                                { key: "scoop", zh: "Scoop", en: "Scoop" },
                                { key: "winget", zh: "WinGet", en: "WinGet" }
                            ]
                            delegate: Rectangle {
                                required property var modelData
                                // Exactly one segment is lit: this is the filter
                                // currently applied. "全部" is its own option, not
                                // "both of the others", so it is not lit together
                                // with Scoop and WinGet.
                                readonly property bool on: controller.managerFilter === modelData.key
                                width: segLabel.width + 20
                                height: theme.smallFont + 8
                                radius: Math.max(0, theme.controlRadius - 1)
                                color: on ? theme.accentFill
                                          : segMouse.containsMouse ? theme.hover : "transparent"
                                Text {
                                    id: segLabel
                                    anchors.centerIn: parent
                                    text: window.t(modelData.zh, modelData.en)
                                    color: on ? theme.accentText : theme.subtext
                                    font.family: theme.fontFamily
                                    font.pixelSize: theme.metaFont
                                    font.weight: on ? Font.DemiBold : Font.Normal
                                }
                                MouseArea {
                                    id: segMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: controller.setManagerFilter(modelData.key)
                                }
                            }
                        }
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: window.t("Enter 查看详情", "Enter opens details")
                    color: theme.faint
                    font.family: theme.fontFamily
                    font.pixelSize: theme.metaFont
                    visible: controller.tab === "discover"
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.divider }
        }

        // --------------------------------------------------- source / result row
        // Two filters that only make sense on one tab each, so they get their own
        // row instead of crowding the manager filter:
        //   Installed → 来源: a manager-owned package is not the same thing as one
        //               the manager merely observed (ARP/MSIX, installed by hand).
        //   Discover  → 结果: whether the search results include what is already
        //               installed. Default is "not installed", because repeating
        //               the installed inventory is what made Discover look broken.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: (controller.tab === "installed" || controller.tab === "discover")
                                        ? theme.tabHeight + 6 : 0
            visible: controller.tab === "installed" || controller.tab === "discover"
            color: theme.layer

            Row {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: controller.tab === "installed" ? window.t("来源", "Source")
                                                         : window.t("结果", "Result")
                    color: theme.faint
                    font.family: theme.fontFamily
                    font.pixelSize: theme.metaFont
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: sourceSegRow.implicitWidth + 4
                    height: theme.smallFont + 12
                    radius: theme.controlRadius
                    color: theme.tagFill
                    border.width: 1
                    border.color: theme.controlBorder

                    Row {
                        id: sourceSegRow
                        anchors.centerIn: parent
                        spacing: 2

                        Repeater {
                            // "全部" first: it is the broad default, and putting it
                            // in the middle made the default state look like some
                            // special filter had been applied.
                            model: controller.tab === "installed"
                                   ? [
                                       { key: "all", zh: "全部", en: "All" },
                                       { key: "managed", zh: "管理器管理", en: "Managed" },
                                       { key: "user", zh: "用户自装", en: "Installed by you" }
                                     ]
                                   : [
                                       { key: "all", zh: "全部", en: "All" },
                                       { key: "new", zh: "仅未安装", en: "Not installed" },
                                       { key: "installed", zh: "仅已安装", en: "Installed" }
                                     ]
                            delegate: Rectangle {
                                required property var modelData
                                readonly property bool on: controller.tab === "installed"
                                                           ? controller.sourceFilter === modelData.key
                                                           : controller.discoverFilter === modelData.key
                                width: srcLabel.width + 20
                                height: theme.smallFont + 8
                                radius: Math.max(0, theme.controlRadius - 1)
                                color: on ? theme.accentFill
                                          : srcMouse.containsMouse ? theme.hover : "transparent"
                                Text {
                                    id: srcLabel
                                    anchors.centerIn: parent
                                    text: window.t(modelData.zh, modelData.en)
                                    color: on ? theme.accentText : theme.subtext
                                    font.family: theme.fontFamily
                                    font.pixelSize: theme.metaFont
                                    font.weight: on ? Font.DemiBold : Font.Normal
                                }
                                MouseArea {
                                    id: srcMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: controller.tab === "installed"
                                               ? controller.setSourceFilter(modelData.key)
                                               : controller.setDiscoverFilter(modelData.key)
                                }
                            }
                        }
                    }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.divider }
        }

        // ------------------------------------------- search (Installed, Discover)
        // Outside the filter row on purpose: see the note in that row. It takes its
        // own line only when it is shown, so the other tabs lose no height.
        //
        // The Installed tab gets a search box too: a machine with hundreds of
        // packages needs one there more than anywhere else, and having it only on
        // Discover made the installed list unsearchable.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: window.searchableTab ? searchField.implicitHeight + 12 : 0
            visible: window.searchableTab
            color: theme.layer

            ThemedField {
                id: searchField
                anchors.fill: parent
                anchors.margins: 6
                // Room for the spinner on the right, so a long query never runs
                // underneath it.
                rightPadding: 30
                placeholderText: window.searchQueriesBackend
                    ? window.t("搜索包名或精确 ID（只查询，不安装）",
                               "Search name or exact ID (query only, never installs)")
                    : window.t("在本机已安装列表中筛选", "Filter the installed list")
                // Each tab owns its own text: the Discover box is a query and the
                // Installed box is a local filter, so they must not share a value.
                text: window.searchQueriesBackend ? controller.searchText
                                                  : controller.installedFilter
                onTextEdited: searchDebounce.restart()
                // Discover runs a query (one process per call, seconds each), so it
                // is debounced. Installed only re-filters rows already in memory, so
                // it updates immediately — waiting half a second there would feel
                // broken.
                Timer {
                    id: searchDebounce
                    interval: 500
                    onTriggered: {
                        if (window.searchQueriesBackend)
                            controller.setSearchText(searchField.text)
                        else
                            controller.setInstalledFilter(searchField.text)
                    }
                }
                // The Installed filter is local, so it does not wait for the timer.
                onTextChanged: {
                    if (!window.searchQueriesBackend)
                        controller.setInstalledFilter(text)
                }
                // Switching tabs must show that tab's own text, not the other's.
                Connections {
                    target: controller
                    function onTabChanged() {
                        searchField.text = window.searchQueriesBackend ? controller.searchText
                                                                        : controller.installedFilter
                    }
                }
            }
            // A search takes seconds, so it needs a visible sign of progress where
            // the user is looking. The status bar alone was too easy to miss: the
            // list looked empty rather than busy.
            Item {
                id: searchSpinner
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                height: 14
                // Only a backend search can be "busy": the Installed filter is a
                // local pass over rows already in memory.
                visible: window.searchQueriesBackend && controller.searching

                // Draw only the animated arc; the old half-ring mask used a
                // rectangular layer-colored plate which showed as a colored block
                // when the field's focused fill differed from theme.layer.
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "transparent"
                    border.width: 2
                    border.color: theme.controlBorder
                }
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "transparent"
                    border.width: 2
                    border.color: "transparent"
                    Canvas {
                        anchors.fill: parent
                        antialiasing: true
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.lineWidth = 2
                            ctx.strokeStyle = theme.accentLine
                            ctx.beginPath()
                            ctx.arc(width / 2, height / 2, width / 2 - 1, -Math.PI / 2, Math.PI * 0.65)
                            ctx.stroke()
                        }
                    }
                }
                RotationAnimator on rotation {
                    from: 0
                    to: 360
                    duration: 700
                    loops: Animation.Infinite
                    running: controller.searching
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.divider }
        }

        // ---------------------------------------------------------- evidence strip
        // Always visible: the scan time and per-backend state explain what the
        // list is standing on, which matters on the tasks page just as much as
        // on the list pages.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: theme.tinyFont + 16
            color: theme.page

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                // Scan time and per-backend state, so an empty list is never
                // mistaken for a successful "nothing installed".
                text: controller.scanLine + "  ·  " + controller.backendLine
                color: theme.faint
                font.family: theme.fontFamily
                font.pixelSize: theme.tinyFont
                elide: Text.ElideRight
                width: parent.width - 24
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.divider }
        }

        // ------------------------------------------------------- selection bar
        // Multi-select actions belong next to the rows they act on, not in the
        // status bar: a status bar is read as output, and a button there is both
        // easy to miss and far from the selection it applies to. This bar appears
        // only while something is selected, so it costs no height otherwise.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: controller.checkedCount > 0 && controller.tab !== "tasks"
                                        ? theme.tabHeight + 10 : 0
            visible: controller.checkedCount > 0 && controller.tab !== "tasks"
            color: theme.layer

            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.divider }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                Text {
                    text: window.t("已选 ", "Selected ") + controller.checkedCount + window.t(" 项", "")
                    color: theme.text
                    font.family: theme.fontFamily
                    font.pixelSize: theme.metaFont
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: controller.selectionSummary
                    color: theme.subtext
                    font.family: theme.fontFamily
                    font.pixelSize: theme.tinyFont
                    elide: Text.ElideRight
                }
                ThemedButton {
                    visible: controller.tab !== "discover"
                    text: window.t("审阅所选操作…", "Review selected…")
                    kind: "primary"
                    onClicked: controller.openReview()
                }
                ThemedButton {
                    // Discover rows are search results, so the bulk action there is
                    // Install: showing Upgrade/Uninstall would grey both out and
                    // make the whole selection look inert.
                    visible: controller.tab === "discover"
                    text: window.t("安装所选…", "Install selected…")
                    kind: "primary"
                    onClicked: {
                        controller.setReviewAction("install")
                        controller.openReview()
                    }
                }
                ThemedButton {
                    text: window.t("清除选择", "Clear")
                    onClicked: controller.clearSelection()
                }
            }
        }

        // ------------------------------------------------------------ main body
        // Wrapped in a painted Rectangle rather than relying on anything inside:
        // SplitView is not a Control (it has no `background` slot) and it paints
        // nothing itself, so the area between the panes — and any pane that does
        // not paint — would show the window's default background.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: controller.tab !== "tasks"
            color: theme.page

            SplitView {
                id: mainBody
                anchors.fill: parent
                orientation: Qt.Horizontal
                // The split handle is the only part of the view that has no pane
                // behind it, so it is painted here.
                handle: Rectangle {
                    implicitWidth: 4
                    color: theme.divider
                }

            // list
            Item {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 320

                // The container must paint: only parts of it have their own
                // Rectangles, and an unpainted Item lets the window's default
                // background show through wherever nothing else covers it (visibly
                // white in a dark theme).
                Rectangle {
                    anchors.fill: parent
                    color: theme.page
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: theme.tinyFont + 12
                        color: theme.page
                        Row {
                            anchors.left: parent.left
                            anchors.leftMargin: 34
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            Text {
                                text: window.t("名称 / 精确 ID", "Name / exact ID")
                                color: theme.faint
                                font.family: theme.fontFamily
                                font.pixelSize: theme.tinyFont
                            }
                        }
                    }

                    // The state pane replaces the list when the query itself could
                    // not produce rows, and it always says why. It must not take
                    // layout space while hidden, or the list is clipped in half.
                    // Every text fills the width and centres itself: an
                    // AlignHCenter on an item wider than the pane does nothing,
                    // which is why the title sat at the left edge in a narrow
                    // window.
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: controller.statePane !== ""
                        spacing: 6

                        Item { Layout.fillHeight: true }
                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: controller.stateTitle
                            color: theme.text
                            font.family: theme.fontFamily
                            font.pixelSize: theme.titleFont
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.maximumWidth: 560
                            Layout.alignment: Qt.AlignHCenter
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: controller.stateBody
                            color: theme.subtext
                            font.family: theme.fontFamily
                            font.pixelSize: theme.metaFont
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.maximumWidth: 640
                            Layout.alignment: Qt.AlignHCenter
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: controller.stateWhy
                            color: theme.faint
                            font.family: theme.monoFamily
                            font.pixelSize: theme.tinyFont
                        }
                        Item { Layout.fillHeight: true }
                    }

                    // Skeleton rows while a search runs. An empty list during a
                    // multi-second query reads as "nothing found"; a few pulsing
                    // placeholders read as "working", which is the truth.
                    Column {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: window.searchQueriesBackend && controller.searching
                                 && controller.statePane === ""
                        spacing: 0

                        Repeater {
                            model: 6
                            delegate: Rectangle {
                                width: parent.width
                                height: theme.rowHeight
                                color: "transparent"

                                Rectangle {
                                    x: 12
                                    y: (parent.height - 14) / 2
                                    width: 14
                                    height: 14
                                    radius: theme.smallControlRadius
                                    color: theme.tagFill
                                    SequentialAnimation on opacity {
                                        loops: Animation.Infinite
                                        NumberAnimation { to: 0.9; duration: 600 }
                                        NumberAnimation { to: 0.45; duration: 600 }
                                    }
                                }
                                Column {
                                    x: 34
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 6
                                    Rectangle {
                                        width: parent.parent.width * 0.34
                                        height: theme.smallFont + 2
                                        radius: theme.smallControlRadius
                                        color: theme.tagFill
                                        SequentialAnimation on opacity {
                                            loops: Animation.Infinite
                                            NumberAnimation { to: 0.9; duration: 600 }
                                            NumberAnimation { to: 0.45; duration: 600 }
                                        }
                                    }
                                    Rectangle {
                                        width: parent.parent.width * 0.22
                                        height: theme.tinyFont
                                        radius: theme.smallControlRadius
                                        color: theme.tagFill
                                        SequentialAnimation on opacity {
                                            loops: Animation.Infinite
                                            NumberAnimation { to: 0.9; duration: 600 }
                                            NumberAnimation { to: 0.45; duration: 600 }
                                        }
                                    }
                                }
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: 1
                                    color: theme.divider
                                }
                            }
                        }
                    }

                    ListView {
                        id: list
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        visible: controller.statePane === "" && !controller.searching
                        model: packagesModel
                        // Keyboard navigation needs a focusable list, and the
                        // selection must follow focus: otherwise arrow keys move a
                        // highlight the details pane knows nothing about.
                        focus: true
                        activeFocusOnTab: true
                        // Rows have a fixed height and the delegate never resizes
                        // itself, so the list virtualises without a layout pass
                        // changing item geometry.
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        // No contentHeight override: ListView already sizes its
                        // content from the delegates, and forcing a second source
                        // of truth clipped the last rows.

                        delegate: RowDelegate {
                            width: list.width
                            height: theme.rowHeight
                            selected: list.currentIndex === index
                            onRowClicked: function(i) {
                                list.currentIndex = i
                                controller.selectRow(i)
                            }
                            onCheckboxToggled: function(i) { controller.toggleRow(i) }
                            // Right-click is how a single row is acted on without
                            // first checking it: the menu offers the actions that
                            // row can actually take, so a package with no update
                            // does not offer an upgrade.
                            onRowRightClicked: function(i, x, y) {
                                list.currentIndex = i
                                controller.selectRow(i)
                                rowMenu.openAt(i, x, y)
                            }
                        }

                        // Enter opens the selected row's details; it never runs an
                        // action and never installs.
                        Keys.onReturnPressed: controller.openReview()
                        Keys.onEnterPressed: controller.openReview()

                        // Keyboard navigation. Windows users expect arrows/Home/End;
                        // a Vim user expects j/k/g/G. Both are handled here rather
                        // than globally, so typing in the search box is never
                        // hijacked. Enter opens the review for the focused row, and
                        // Space toggles its checkbox: the two actions the list
                        // itself offers.
                        Keys.onDownPressed: function(event) {
                            if (list.count === 0) return
                            list.currentIndex = Math.min(list.count - 1, list.currentIndex + 1)
                            if (list.currentIndex >= 0) list.positionViewAtIndex(list.currentIndex, ListView.Contain)
                            event.accepted = true
                        }
                        Keys.onUpPressed: function(event) {
                            if (list.count === 0) return
                            list.currentIndex = Math.max(0, list.currentIndex < 0 ? 0 : list.currentIndex - 1)
                            list.positionViewAtIndex(list.currentIndex, ListView.Contain)
                            event.accepted = true
                        }
                        Keys.onPressed: function(event) {
                            if (list.count === 0) return
                            if (event.key === Qt.Key_Home) {
                                list.currentIndex = 0
                                event.accepted = true
                            } else if (event.key === Qt.Key_End) {
                                list.currentIndex = list.count - 1
                                list.positionViewAtIndex(list.currentIndex, ListView.Contain)
                                event.accepted = true
                            } else if (event.key === Qt.Key_J) {
                                list.currentIndex = Math.min(list.count - 1, list.currentIndex + 1)
                                list.positionViewAtIndex(list.currentIndex, ListView.Contain)
                                event.accepted = true
                            } else if (event.key === Qt.Key_K) {
                                list.currentIndex = Math.max(0, list.currentIndex < 0 ? 0 : list.currentIndex - 1)
                                list.positionViewAtIndex(list.currentIndex, ListView.Contain)
                                event.accepted = true
                            } else if (event.key === Qt.Key_G && (event.modifiers & Qt.ShiftModifier)) {
                                list.currentIndex = list.count - 1
                                list.positionViewAtIndex(list.currentIndex, ListView.Contain)
                                event.accepted = true
                            } else if (event.key === Qt.Key_G) {
                                list.currentIndex = 0
                                event.accepted = true
                            } else if (event.key === Qt.Key_Space) {
                                controller.toggleRow(list.currentIndex)
                                event.accepted = true
                            }
                        }
                        // Selecting with the keyboard is the same operation as
                        // clicking: the details pane follows, and so does the
                        // metadata lookup for that package. Model resets set the
                        // index to -1, which is not a selection.
                        onCurrentIndexChanged: {
                            if (currentIndex >= 0)
                                controller.selectRow(currentIndex)
                        }

                        // The model is replaced wholesale on every rebuild (a new
                        // search, a filter change, a tab switch), and currentIndex
                        // survives that reset. Without this the details pane kept
                        // describing a row from the previous list.
                        onModelChanged: currentIndex = -1
                        Connections {
                            target: packagesModel
                            function onModelReset() { list.currentIndex = -1 }
                        }
                    }
                }
            }

            // details
            Rectangle {
                SplitView.preferredWidth: 330
                SplitView.minimumWidth: 240
                color: theme.page

                DetailPane {
                    anchors.fill: parent
                    anchors.margins: 12
                    // The controller owns the selection, so the pane follows it even
                    // when the selection came from a keyboard shortcut or the
                    // harness rather than a click on this ListView.
                    packageIndex: list.currentIndex >= 0 ? list.currentIndex : controller.selectedRow
                }
                Rectangle { width: 1; height: parent.height; color: theme.divider }
            }
            }
        }

        // --------------------------------------------------------------- tasks
        TasksPage {
            id: tasksPage
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: controller.tab === "tasks"
        }

        // -------------------------------------------------------------- status bar
        // A status bar reports state and nothing else. The selection actions used
        // to live here, which put a button in a place users read as read-only and
        // separated it from the rows it acts on; they now sit in the selection bar
        // directly above the list.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: theme.tabHeight + 2
            color: theme.layer

            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: theme.divider }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 12
                text: controller.statusLine
                color: theme.subtext
                font.family: theme.fontFamily
                font.pixelSize: theme.metaFont
                elide: Text.ElideRight
            }
        }
    }

    // --------------------------------------------------------------- overlays
    // The per-row action menu. It lives at the window level so it can open over
    // the list without being clipped by the ListView.
    RowMenu {
        id: rowMenu
        onUpgradeRequested: function(i) { controller.openReviewFor(i, "upgrade") }
        onUninstallRequested: function(i) { controller.openReviewFor(i, "uninstall") }
        onInstallRequested: function(i) { controller.openReviewFor(i, "install") }
        onHomepageRequested: function(i) { controller.openHomepage(i) }
        onDetailsRequested: function(i) { controller.selectRow(i) }
        onCopyIdRequested: function(id) { controller.copyToClipboard(id) }
        onAddToSelectionRequested: function(i) { controller.toggleRow(i) }
    }

    ReviewDialog {
        id: reviewDialog
        anchors.fill: parent
    }

    Popup {
        id: settingsPopup
        objectName: "settingsPopup"
        anchors.centerIn: parent
        width: 440
        modal: true
        padding: 14
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        // The layout must be given its width explicitly: a Popup does not
        // constrain its children, and without a width the wrapMode below has
        // nothing to wrap at, so the long lines ran past the popup edge.
        ColumnLayout {
            width: parent.width
            spacing: 8

            Text {
                text: window.t("设置", "Settings")
                color: theme.text
                font.family: theme.fontFamily
                font.pixelSize: theme.baseFont
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                // Settings are read-only information in this build: no dependency
                // installation and no source management is offered here.
                text: window.t(
                    "本窗口只展示 backend 路径、版本与适配状态。来源、bucket 与 pin/hold 的管理不在本版本内；" +
                    "本插件不会下载包管理器，也不会自行提权——提权只跟随 Win顺 的管理员状态，且需你在设置里确认。",
                    "This window only reports backend path, version and adapter state. Source, bucket and pin/hold " +
                    "management are not part of this version; the plugin never downloads a package manager and never " +
                    "elevates on its own — elevation only follows Win顺 running as administrator, after you confirm " +
                    "it in the host settings.")
                color: theme.subtext
                font.family: theme.fontFamily
                font.pixelSize: theme.metaFont
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                text: controller.backendLine
                color: theme.faint
                font.family: theme.monoFamily
                font.pixelSize: theme.tinyFont
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                Item { Layout.fillWidth: true }
                ThemedButton {
                    text: window.t("关闭", "Close")
                    onClicked: settingsPopup.close()
                }
            }
        }
    }

    // The controller raises this when the host keyword action asks for the window.
    Connections {
        target: controller
        function onErrorRaised(message) {
            // Kept visible rather than swallowed: a load or probe error must reach
            // the user, not just the log.
            statusBanner.text = message
            statusBanner.visible = true
        }
    }

    Rectangle {
        id: statusBanner
        property string text: ""
        visible: false
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 44
        width: Math.min(bannerLabel.implicitWidth + 24, window.width - 48)
        height: theme.smallFont + 18
        radius: theme.controlRadius
        color: theme.dangerFill
        Text {
            id: bannerLabel
            anchors.centerIn: parent
            width: parent.width - 24
            text: statusBanner.text
            color: theme.accentText
            font.family: theme.fontFamily
            font.pixelSize: theme.metaFont
            elide: Text.ElideRight
        }
    }
}







