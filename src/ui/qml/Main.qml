import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import quickui
import VisionRT.Ui 1.0

ApplicationWindow {
    id: window
    objectName: "visionrtWindow"
    visible: true
    visibility: Window.Maximized
    title: app.title
    width: Math.min(1600, Screen.desktopAvailableWidth)
    height: Math.min(960, Screen.desktopAvailableHeight)
    minimumWidth: Math.min(960, Screen.desktopAvailableWidth)
    minimumHeight: Math.min(540, Screen.desktopAvailableHeight)
    color: QuiColor.Background
    property bool leftExpanded: true
    property real savedLeftWidth: 0
    property bool libraryExpanded: true
    property real savedLibraryHeight: 0
    property bool closingApproved: false
    property bool unsavedSaving: false
    property int toastCount: 0
    property real leftRatio: 0.362
    property real libraryRatio: 0.38
    property real resultsRatio: 0.59

    function localPath(url) {
        let text = decodeURIComponent(url.toString());
        return text.replace(/^file:\/\/\//, "").replace(/^file:\/\//, "//");
    }
    function resetLayout() {
        leftExpanded = true;
        libraryExpanded = true;
        leftRatio = 0.362;
        libraryRatio = 0.38;
        resultsRatio = 0.59;
        let w = workbench.width > 200 ? workbench.width : window.width;
        savedLeftWidth = Math.round(w * leftRatio);
        leftHost.SplitView.preferredWidth = savedLeftWidth;
        let leftH = leftSplit.height > 200 ? leftSplit.height : window.contentItem.height;
        savedLibraryHeight = Math.round(leftH * libraryRatio);
        library.SplitView.preferredHeight = savedLibraryHeight;
        let rightH = rightSplit.height > 200 ? rightSplit.height : window.contentItem.height;
        results.SplitView.preferredHeight = Math.round(rightH * resultsRatio);
        parameters.SplitView.preferredHeight = Math.round(rightH * (1.0 - resultsRatio));
        parameters.resetSplit();
    }
    function toggleLeft() {
        if (leftExpanded) {
            savedLeftWidth = leftHost.width;
            leftExpanded = false;
            leftHost.width = 28;
            leftHost.SplitView.preferredWidth = 28;
            if (drawerButton) {
                drawerButton.forceActiveFocus();
            }
        } else {
            leftExpanded = true;
            let targetW = Math.max(260, Math.min(savedLeftWidth || Math.round(workbench.width * leftRatio), workbench.width - 400));
            leftHost.width = targetW;
            leftHost.SplitView.preferredWidth = targetW;
            if (drawerCollapseButton) {
                drawerCollapseButton.forceActiveFocus();
            }
        }
    }
    function focusSearch() {
        if (!leftExpanded)
            toggleLeft();
        flowCanvas.focusSearch();
    }
    function saveProject(asNew) {
        if (app.closing)
            return;
        unsavedSaving = false;
        if (asNew)
            saveFile.open();
        else
            app.save();
    }
    Component.onCompleted: {
        Qt.callLater(resetLayout);
    }
    Timer {
        interval: 600
        running: typeof demoMenu !== "undefined" && demoMenu === true
        onTriggered: mainToolbar.openToolsMenu()
    }
    Timer {
        interval: 600
        running: typeof demoFileMenu !== "undefined" && demoFileMenu === true
        onTriggered: mainToolbar.openFileMenu()
    }
    Timer {
        interval: 600
        running: typeof demoSettings !== "undefined" && demoSettings === true
        onTriggered: settingsWindow.openDialog()
    }
    onClosing: function (event) {
        if (!closingApproved) {
            event.accepted = false;
            app.requestClose();
        }
    }
    Action {
        id: newAction
        text: "新建项目"
        shortcut: "Ctrl+N"
        enabled: app.workspaceEnabled
        onTriggered: app.requestNew()
    }
    Action {
        id: openAction
        text: "打开项目…"
        shortcut: "Ctrl+O"
        enabled: app.workspaceEnabled
        onTriggered: openFile.open()
    }
    Action {
        id: openExampleAction
        text: "打开示例…"
        enabled: app.workspaceEnabled
        // The dialog starts in the installed examples. An installation without them says which path
        // is missing instead of opening an unrelated folder.
        onTriggered: {
            const folder = app.examplesDirectoryUrl();
            if (folder.toString().length === 0) {
                examplesMissing.open();
                return;
            }
            exampleFile.currentFolder = folder;
            exampleFile.open();
        }
    }
    Action {
        id: saveAction
        text: "保存"
        shortcut: "Ctrl+S"
        enabled: app.workspaceEnabled
        onTriggered: window.saveProject(false)
    }
    Action {
        id: saveAsAction
        text: "另存为…"
        shortcut: "Ctrl+Shift+S"
        enabled: app.workspaceEnabled
        onTriggered: window.saveProject(true)
    }
    Action {
        id: runAction
        text: "单次"
        shortcut: "F5"
        enabled: app.workspaceEnabled
        onTriggered: app.runOnce()
    }
    Action {
        id: loopAction
        text: app.runState === RuntimeState.Stopping ? "等待本轮结束" : app.runState === RuntimeState.RunningLoop ? "停止循环" : app.stopEnabled ? "停止运行" : "循环"
        shortcut: "F6"
        // Stopping keeps the entry disabled: the request is already submitted. Idle offers a new
        // loop; every running mode offers the same stop.
        enabled: app.workspaceEnabled || app.stopEnabled
        onTriggered: app.stopEnabled ? app.stop() : app.toggleLoop()
    }
    Shortcut {
        sequence: "Ctrl+F"
        onActivated: window.focusSearch()
    }

    header: MainToolbar {
        id: mainToolbar
        fileMenuObject: fileMenu
        settingsMenuObject: settingsMenu
        toolsMenuObject: toolsMenu
        helpMenuObject: helpMenu
        saveCommand: saveAction
        runCommand: runAction
        loopCommand: loopAction
        workspaceEnabled: app.workspaceEnabled
        stopEnabled: app.stopEnabled
        stopping: app.runState === RuntimeState.Stopping
        busy: app.busy
        statusText: app.runStateText
        onResetRequested: resetDialog.open()
    }
    QuiMenu {
        id: fileMenu
        objectName: "fileMenu"
        QuiMenuItem {
            action: newAction
        }
        QuiMenuItem {
            action: openAction
        }
        QuiMenuItem {
            action: openExampleAction
        }
        // The recent list is a submenu of the file menu: newest first, at most ten entries, disabled
        // while there is nothing to offer or while the application is closing.
        QuiMenu {
            id: recentMenu
            objectName: "recentMenu"
            title: "最近打开工程"
            enabled: app.recentProjects.length > 0 && !app.closing
            Instantiator {
                model: app.recentProjects
                delegate: QuiMenuItem {
                    text: modelData.tooltip
                    onTriggered: app.openRecentProject(modelData.path)
                }
                onObjectAdded: function (index, object) {
                    recentMenu.insertItem(index, object);
                }
                onObjectRemoved: function (index, object) {
                    recentMenu.removeItem(object);
                }
            }
        }
        QuiMenuItem {
            action: saveAction
        }
        QuiMenuItem {
            action: saveAsAction
        }
        MenuSeparator {
        }
        QuiMenuItem {
            text: "退出"
            onTriggered: app.requestClose()
        }
    }
    QuiMenu {
        id: settingsMenu
        QuiMenuItem {
            text: "平台设置"
            enabled: app.workspaceEnabled
            onTriggered: settingsWindow.openDialog()
        }
    }
    QuiMenu {
        id: toolsMenu
        QuiMenuItem {
            text: "自定义工具..."
            enabled: app.workspaceEnabled
            onTriggered: customToolWindow.openDialog()
        }
        MenuSeparator {
        }
        QuiMenuItem {
            text: "刷新 Python 工具"
            enabled: app.workspaceEnabled
            onTriggered: app.refreshPython()
        }
    }
    QuiMenu {
        id: helpMenu
        QuiMenuItem {
            text: "关于 VisionRT"
            onTriggered: about.open()
        }
    }

    QuiSplitView {
        id: workbench
        anchors.fill: parent
        orientation: Qt.Horizontal
        backgroundColor: QuiColor.Border
        handleColor: QuiColor.Border
        onWidthChanged: {
            if (window.leftExpanded && !leftHost.SplitHandle.pressed && workbench.width > 200) {
                leftHost.SplitView.preferredWidth = Math.round(workbench.width * window.leftRatio);
            }
        }
        handle: Rectangle {
            id: leftHandleItem
            objectName: "leftHandle"
            implicitWidth: window.leftExpanded ? 1 : 0
            width: window.leftExpanded ? 1 : 0
            implicitHeight: workbench.height
            visible: window.leftExpanded
            enabled: window.leftExpanded
            color: SplitHandle.hovered || SplitHandle.pressed ? QuiColor.Highlight : QuiColor.Border
            containmentMask: Item {
                objectName: "leftHandleMask"
                x: window.leftExpanded ? -3 : 0
                width: window.leftExpanded ? 7 : 0
                height: workbench.height
                enabled: window.leftExpanded
            }
        }
        Item {
            id: leftHost
            objectName: "leftHost"
            SplitView.fillWidth: false
            SplitView.minimumWidth: window.leftExpanded ? 260 : 28
            SplitView.maximumWidth: window.leftExpanded ? Math.round(workbench.width * 0.55) : 28
            SplitView.preferredWidth: window.leftExpanded ? Math.round(workbench.width * window.leftRatio) : 28
            onWidthChanged: {
                if (window.leftExpanded && leftHost.SplitHandle.pressed && workbench.width > 200) {
                    window.leftRatio = Math.max(0.20, Math.min(0.55, leftHost.width / workbench.width));
                }
            }
            FocusScope {
                id: leftContent
                objectName: "leftContent"
                anchors.fill: parent
                visible: window.leftExpanded
                enabled: window.leftExpanded
                clip: true
                QuiSplitView {
                    id: leftSplit
                    anchors.fill: parent
                    orientation: Qt.Vertical
                    backgroundColor: QuiColor.Border
                    handleColor: QuiColor.Border
                    handle: Rectangle {
                        implicitHeight: 1
                        implicitWidth: leftSplit.width
                        color: SplitHandle.hovered || SplitHandle.pressed ? QuiColor.Highlight : QuiColor.Border
                        containmentMask: Item {
                            y: -3
                            height: 7
                            width: leftSplit.width
                        }
                    }
                    onHeightChanged: {
                        if (window.libraryExpanded && leftSplit.height > 200) {
                            library.SplitView.preferredHeight = Math.round(leftSplit.height * window.libraryRatio);
                        }
                    }
                    ToolLibrary {
                        id: library
                        objectName: "library"
                        expanded: window.libraryExpanded
                        SplitView.fillHeight: false
                        SplitView.preferredHeight: Math.round(leftSplit.height * window.libraryRatio)
                        SplitView.minimumHeight: expanded ? 160 : 36
                        SplitView.maximumHeight: expanded ? Math.round(leftSplit.height * 0.60) : 36
                        onHeightChanged: {
                            if (window.libraryExpanded && leftSplit.height > 200 && library.SplitHandle.pressed) {
                                window.libraryRatio = Math.max(0.20, Math.min(0.60, library.height / leftSplit.height));
                            }
                        }
                        onToggleRequested: {
                            if (window.libraryExpanded) {
                                window.savedLibraryHeight = height;
                                window.libraryExpanded = false;
                                SplitView.preferredHeight = 36;
                            } else {
                                window.libraryExpanded = true;
                                SplitView.preferredHeight = Math.min(window.savedLibraryHeight || Math.round(leftSplit.height * window.libraryRatio), Math.round(leftSplit.height * 0.60));
                            }
                        }
                    }
                    FlowCanvas {
                        id: flowCanvas
                        objectName: "flowCanvas"
                        SplitView.fillHeight: true
                        SplitView.minimumHeight: 260
                    }
                }
            }
            Rectangle {
                id: drawerCollapseTab
                visible: window.leftExpanded
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: VisionRTSize.IconButtonSize
                height: VisionRTSize.IconButtonSize
                color: QuiColor.Button
                border.color: QuiColor.Border
                border.width: 1
                radius: 3
                z: 20
                WorkbenchActionButton {
                    id: drawerCollapseButton
                    objectName: "drawerCollapseButton"
                    anchors.fill: parent
                    iconSource: QuiFontIcon.ChevronLeft
                    text: "收起左侧工作区"
                    Accessible.name: text
                    focusPolicy: Qt.TabFocus
                    onClicked: window.toggleLeft()
                }
            }
            Rectangle {
                id: collapsedBar
                anchors.fill: parent
                visible: !window.leftExpanded
                color: QuiColor.Primary
                border.color: QuiColor.Border
                border.width: 1
                WorkbenchActionButton {
                    id: drawerButton
                    objectName: "drawerToggle"
                    anchors.centerIn: parent
                    width: 22
                    height: 64
                    iconSource: QuiFontIcon.ChevronRight
                    text: "展开左侧工作区"
                    Accessible.name: text
                    focusPolicy: Qt.TabFocus
                    onClicked: window.toggleLeft()
                }
            }
        }
        QuiSplitView {
            id: rightSplit
            orientation: Qt.Vertical
            SplitView.fillWidth: true
            SplitView.minimumWidth: 380
            backgroundColor: QuiColor.Border
            handleColor: QuiColor.Border
            handle: Rectangle {
                implicitHeight: 1
                implicitWidth: rightSplit.width
                color: SplitHandle.hovered || SplitHandle.pressed ? QuiColor.Highlight : QuiColor.Border
                containmentMask: Item {
                    y: -3
                    height: 7
                    width: rightSplit.width
                }
            }
            onHeightChanged: {
                if (rightSplit.height > 200) {
                    results.SplitView.preferredHeight = Math.round(rightSplit.height * window.resultsRatio);
                    parameters.SplitView.preferredHeight = Math.round(rightSplit.height * (1.0 - window.resultsRatio));
                }
            }
            ResultsPanel {
                id: results
                SplitView.fillHeight: true
                SplitView.preferredHeight: Math.round(rightSplit.height * window.resultsRatio)
                SplitView.minimumHeight: 180
                onHeightChanged: {
                    if (rightSplit.height > 200 && results.SplitHandle.pressed) {
                        window.resultsRatio = Math.max(0.30, Math.min(0.75, results.height / rightSplit.height));
                    }
                }
            }
            ParameterPanel {
                id: parameters
                SplitView.fillHeight: true
                SplitView.preferredHeight: Math.round(rightSplit.height * (1.0 - window.resultsRatio))
                SplitView.minimumHeight: 140
            }
        }
    }
    footer: Rectangle {
        height: VisionRTSize.BarSize
        color: QuiColor.Button
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: QuiColor.Border
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 5
            anchors.rightMargin: 5
            spacing: 5

            // 底部时间条：三个数值是同一时钟下的嵌套区间，不是三次独立测量。
            //   流程：当前流程内所有已执行节点 [开始, 结束] 的并集；
            //   全流程：本轮所有流程窗口的并集（单流程时与“流程”同一个区间）；
            //   已运行：本次会话从启动到结束的总时长，包含准备、执行与收尾读回。
            RowLayout {
                id: timingRow
                spacing: 5
                QuiTextIcon {
                    iconSource: QuiFontIcon.History
                    iconSize: VisionRTSize.IconSize
                    iconColor: QuiColor.FontCaption
                }
                QuiText {
                    text: app.timingText
                    font: QuiFont.Body
                }
                HoverHandler {
                    id: timingHover
                }
                ToolTip.visible: timingHover.hovered
                ToolTip.delay: 400
                ToolTip.text: "流程：当前流程内各节点执行区间的并集\n" + "全流程：本轮所有流程执行区间的并集\n" + "已运行：本次会话从启动到结束的总时长（含准备与读回）"
            }

            Item {
                Layout.fillWidth: true
            }
            Rectangle {
                Layout.preferredHeight: 24
                Layout.preferredWidth: logRow.width + 12
                radius: 3
                color: logMouse.containsMouse ? QuiColor.ItemHover : "transparent"
                RowLayout {
                    id: logRow
                    anchors.centerIn: parent
                    spacing: 5
                    width: implicitWidth
                    QuiTextIcon {
                        iconSource: QuiFontIcon.Document
                        iconSize: VisionRTSize.IconSize
                        iconColor: QuiColor.FontCaption
                    }
                    QuiText {
                        text: "日志"
                        font: QuiFont.Body
                    }
                }
                MouseArea {
                    id: logMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: logDialog.showAndRaise()
                }
            }
        }
    }
    FileDialog {
        id: openFile
        title: "打开工程"
        nameFilters: ["VisionRT JSON 工程 (*.vrt.json *.json)"]
        onAccepted: app.requestOpen(window.localPath(selectedFile))
    }
    FileDialog {
        id: exampleFile
        title: "打开示例"
        nameFilters: ["VisionRT 工程 (*.vrt.json)"]
        onAccepted: app.requestOpen(window.localPath(selectedFile))
    }
    WorkbenchDialog {
        id: examplesMissing
        title: "未安装示例工程"
        useNegativeButton: false
        positiveText: "关闭"
        message: "示例目录不存在：" + app.examplesDirectory() + "\n随包示例会随安装目录一起提供。"
    }
    WorkbenchDialog {
        id: toolUpdated
        title: "工具已更新"
        objectName: "toolUpdateDialog"
        useNegativeButton: false
        positiveText: "知道了"
        message: app.toolUpdateSummary
        // Dismissing the window is the whole interaction: the next detection is a new event and
        // must be able to raise it again.
        onClosed: app.dismissToolUpdate()
    }
    FileDialog {
        id: saveFile
        title: "保存工程"
        fileMode: FileDialog.SaveFile
        nameFilters: ["VisionRT JSON 工程 (*.vrt.json)"]
        defaultSuffix: "vrt.json"
        onAccepted: {
            if (window.unsavedSaving)
                app.resolveUnsaved(UnsavedDecision.Save, window.localPath(selectedFile));
            else
                app.save(window.localPath(selectedFile));
        }
        onRejected: {
            if (window.unsavedSaving) {
                app.resolveUnsaved(UnsavedDecision.Cancel);
                unsaved.close();
            }
            window.unsavedSaving = false;
        }
    }
    WorkbenchDialog {
        id: unsaved
        title: "保存工程修改？"
        message: "当前工程包含未保存的修改。保存失败将保留当前工程。"
        positiveText: "保存"
        negativeText: "取消"
        neutralText: "不保存"
        useNeutralButton: true
        onPositiveClickListener: function () {
            window.unsavedSaving = true;
            app.resolveUnsaved(UnsavedDecision.Save);
        }
        onNegativeClickListener: function () {
            window.unsavedSaving = false;
            app.resolveUnsaved(UnsavedDecision.Cancel);
            unsaved.close();
        }
        onNeutralClickListener: function () {
            window.unsavedSaving = false;
            app.resolveUnsaved(UnsavedDecision.Discard);
            unsaved.close();
        }
    }
    WorkbenchDialog {
        id: resetDialog
        title: "运行时复位"
        message: "清除运行结果并释放实例？所有未保存的工程配置和当前布局将保留。"
        positiveText: "复位"
        onPositiveClicked: app.resetRuntime()
    }
    SettingsWindow {
        id: settingsWindow
        transientParent: window
        applicationClosing: app.isClosing
    }
    AboutVisionRTDialog {
        id: about
    }
    CustomToolWindow {
        id: customToolWindow
        applicationClosing: app.isClosing
        controller: app.toolEditor
        transientParent: window
    }
    LogDialog {
        id: logDialog
        objectName: "logWindow"
        transientParent: window
    }
    QuiInfoBar {
        id: info
        root: window.contentItem
        maxWidth: Math.min(520, window.width - 40)
    }
    Timer {
        id: toastExpiry
        interval: 6000
        onTriggered: {
            info.clearAllInfo();
            window.toastCount = 0;
        }
    }
    Connections {
        target: app
        function onUnsavedDecisionRequired() {
            unsaved.open();
        }
        function onSavePathRequired() {
            saveFile.open();
        }
        function onCloseReady() {
            window.closingApproved = true;
            window.close();
        }
        function onProjectReplaced() {
            unsaved.close();
            window.unsavedSaving = false;
            window.resetLayout();
        }
        function onToolUpdateSummaryChanged() {
            if (app.toolUpdateSummary.length > 0)
                toolUpdated.open();
        }
        function onProjectChanged() {
            if (!app.dirty && window.unsavedSaving) {
                unsaved.close();
                window.unsavedSaving = false;
            }
        }
    }
}
