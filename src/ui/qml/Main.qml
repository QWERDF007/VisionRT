import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import quickui
import VisionRT.Style 1.0

ApplicationWindow {
    id: window
    objectName: "visionrtWindow"
    visible: true
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
        let leftH = leftSplit.height > 200 ? leftSplit.height : (window.height - 74);
        savedLibraryHeight = Math.round(leftH * libraryRatio);
        library.SplitView.preferredHeight = savedLibraryHeight;
        let rightH = rightSplit.height > 200 ? rightSplit.height : (window.height - 74);
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
        onTriggered: toolsMenu.popup(toolsBtn, 0, toolsBtn.height)
    }
    Timer {
        interval: 600
        running: typeof demoFileMenu !== "undefined" && demoFileMenu === true
        onTriggered: fileMenu.popup(fileBtn, 0, fileBtn.height)
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
        text: app.runState === "Stopping" ? "等待本轮结束" : app.runState === "RunningLoop" ? "停止循环" : app.stopEnabled ? "停止运行" : "循环"
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

    header: ToolBar {
        objectName: "mainToolbar"
        padding: 0
        height: 36
        background: Rectangle {
            color: QuiColor.Button
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: WorkbenchStyle.menuBarBorder
            }
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 5
            anchors.rightMargin: 5
            spacing: 5

            // Menus
            WorkbenchActionButton {
                id: fileBtn
                text: "文件"
                iconSource: QuiFontIcon.GlobalNavButton
                display: AbstractButton.TextBesideIcon
                normalColor: fileMenu.visible ? QuiColor.Highlight : QuiColor.Transparent
                hoverColor: fileMenu.visible ? QuiColor.Highlight : QuiColor.Hovered
                textColor: fileMenu.visible ? QuiColor.White : QuiColor.FontPrimary
                iconColor: fileMenu.visible ? QuiColor.White : QuiColor.FontPrimary
                enabled: app.workspaceEnabled
                onClicked: {
                    if (fileMenu.visible)
                        fileMenu.dismiss();
                    else
                        fileMenu.popup(fileBtn, 0, fileBtn.height);
                }
            }
            WorkbenchActionButton {
                id: settingsBtn
                text: "设置"
                iconSource: QuiFontIcon.Settings
                display: AbstractButton.TextBesideIcon
                normalColor: settingsWindow.visible ? QuiColor.Highlight : QuiColor.Transparent
                hoverColor: settingsWindow.visible ? QuiColor.Highlight : QuiColor.Hovered
                textColor: settingsWindow.visible ? QuiColor.White : QuiColor.FontPrimary
                iconColor: settingsWindow.visible ? QuiColor.White : QuiColor.FontPrimary
                enabled: app.workspaceEnabled
                onClicked: settingsWindow.openDialog()
            }
            WorkbenchActionButton {
                id: toolsBtn
                text: "工具"
                iconSource: QuiFontIcon.AllApps
                display: AbstractButton.TextBesideIcon
                normalColor: toolsMenu.visible ? QuiColor.Highlight : QuiColor.Transparent
                hoverColor: toolsMenu.visible ? QuiColor.Highlight : QuiColor.Hovered
                textColor: toolsMenu.visible ? QuiColor.White : QuiColor.FontPrimary
                iconColor: toolsMenu.visible ? QuiColor.White : QuiColor.FontPrimary
                enabled: app.workspaceEnabled
                onClicked: {
                    if (toolsMenu.visible)
                        toolsMenu.dismiss();
                    else
                        toolsMenu.popup(toolsBtn, 0, toolsBtn.height);
                }
            }
            WorkbenchActionButton {
                id: helpBtn
                text: "帮助"
                iconSource: QuiFontIcon.Help
                display: AbstractButton.TextBesideIcon
                normalColor: helpMenu.visible ? QuiColor.Highlight : QuiColor.Transparent
                hoverColor: helpMenu.visible ? QuiColor.Highlight : QuiColor.Hovered
                textColor: helpMenu.visible ? QuiColor.White : QuiColor.FontPrimary
                iconColor: helpMenu.visible ? QuiColor.White : QuiColor.FontPrimary
                onClicked: {
                    if (helpMenu.visible)
                        helpMenu.dismiss();
                    else
                        helpMenu.popup(helpBtn, 0, helpBtn.height);
                }
            }

            WorkbenchSeparator { Layout.alignment: Qt.AlignVCenter }

            // Actions & Execution
            WorkbenchActionButton {
                action: saveAction
                text: "保存 (Ctrl+S)"
                iconSource: QuiFontIcon.Save
            }
            WorkbenchActionButton {
                action: runAction
                text: "单次执行 (F5)"
                iconSource: QuiFontIcon.Play
            }
            WorkbenchActionButton {
                action: loopAction
                text: loopAction.text
                iconSource: app.stopEnabled || app.runState === "Stopping" ? QuiFontIcon.Stop : QuiFontIcon.RepeatAll
                iconColor: app.stopEnabled || app.runState === "Stopping" ? QuiColor.Error : QuiColor.FontPrimary
            }

            Item {
                Layout.fillWidth: true
            }

            QuiText {
                text: app.runState
                visible: app.busy
                color: QuiColor.Highlight
                font: QuiFont.Body
            }

            WorkbenchActionButton {
                iconSource: QuiFontIcon.Refresh
                text: "复位"
                display: AbstractButton.TextBesideIcon
                textColor: QuiColor.FontPrimary
                enabled: app.workspaceEnabled
                focusPolicy: Qt.TabFocus
                onClicked: resetDialog.open()
            }
        }
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
            text: "工具作者说明"
            onTriggered: authorHelp.open()
        }
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
                width: 14
                height: 52
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
                color: WorkbenchStyle.tabBarBg
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
                onRoiRequested: parameters.focusRoi()
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
        height: 32
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
                    iconSize: WorkbenchStyle.timingIconSize
                    iconColor: QuiColor.FontDark
                }
                QuiText {
                    text: app.timingText
                    font: QuiFont.Body
                    color: QuiColor.FontDark
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
            QuiText {
                text: "说明文档"
                font: QuiFont.Body
                color: docMouse.containsMouse ? QuiColor.Highlight : QuiColor.FontDark
                MouseArea {
                    id: docMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: authorHelp.open()
                }
            }
            Rectangle {
                Layout.preferredHeight: 24
                Layout.preferredWidth: logRow.width + 12
                radius: 3
                color: logMouse.containsMouse ? WorkbenchStyle.logButtonHoverColor : "transparent"
                RowLayout {
                    id: logRow
                    anchors.centerIn: parent
                    spacing: 5
                    width: implicitWidth
                    QuiTextIcon {
                        iconSource: QuiFontIcon.Document
                        iconSize: WorkbenchStyle.iconSizeSmall
                        iconColor: QuiColor.FontDark
                    }
                    QuiText {
                        text: "日志"
                        font: QuiFont.Body
                        color: QuiColor.FontDark
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
                app.resolveUnsaved("save", window.localPath(selectedFile));
            else
                app.save(window.localPath(selectedFile));
        }
        onRejected: {
            if (window.unsavedSaving) {
                app.resolveUnsaved("cancel");
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
            app.resolveUnsaved("save");
        }
        onNegativeClickListener: function () {
            window.unsavedSaving = false;
            app.resolveUnsaved("cancel");
            unsaved.close();
        }
        onNeutralClickListener: function () {
            window.unsavedSaving = false;
            app.resolveUnsaved("discard");
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
    WorkbenchDialog {
        id: authorHelp
        title: "工具作者说明"
        width: Math.min(700, window.width - 40)
        useNegativeButton: false
        positiveText: "关闭"
        message: "工具声明与实现由 VisionTools 维护。通过工具库打包脚本生成 ZIP，再由 VisionRT 部署入口安装。安装元数据只读；修改后重新打包，在退出宿主后更换工具包。"
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
