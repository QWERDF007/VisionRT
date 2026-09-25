import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import quickui

ApplicationWindow {
    id: window
    objectName: "visionrtWindow"
    visible: true
    title: app.title
    width: Math.min(1600, Screen.desktopAvailableWidth)
    height: Math.min(960, Screen.desktopAvailableHeight)
    minimumWidth: Math.min(1280, Screen.desktopAvailableWidth)
    minimumHeight: Math.min(720, Screen.desktopAvailableHeight)
    color: QuiColor.Background
    property bool leftExpanded: true
    property real savedLeftWidth: 0
    property bool libraryExpanded: true
    property real savedLibraryHeight: 0
    property bool closingApproved: false
    property bool unsavedSaving: false
    property var notices: []
    property int toastCount: 0
    property real leftRatio: 0.36
    property real libraryRatio: 0.38
    property real resultsRatio: 0.60
    readonly property real leftMinimum: Math.min(320, workbench.width * 0.34)
    readonly property real rightMinimum: Math.min(640, workbench.width * 0.60)

    function localPath(url) {
        let text = decodeURIComponent(url.toString())
        return text.replace(/^file:\/\/\//, "").replace(/^file:\/\//, "//")
    }
    function resetLayout() {
        leftExpanded = true
        libraryExpanded = true
        leftRatio = 0.36
        libraryRatio = 0.38
        resultsRatio = 0.60
        let w = workbench.width > 200 ? workbench.width : window.width
        savedLeftWidth = Math.round(w * leftRatio)
        leftHost.SplitView.preferredWidth = savedLeftWidth
        let leftH = leftSplit.height > 200 ? leftSplit.height : (window.height - 74)
        savedLibraryHeight = Math.round(leftH * libraryRatio)
        library.SplitView.preferredHeight = savedLibraryHeight
        let rightH = rightSplit.height > 200 ? rightSplit.height : (window.height - 74)
        results.SplitView.preferredHeight = Math.round(rightH * resultsRatio)
        parameters.SplitView.preferredHeight = Math.round(rightH * (1.0 - resultsRatio))
        parameters.resetSplit()
    }
    function toggleLeft() {
        if (leftExpanded) {
            savedLeftWidth = leftHost.width
            if (leftContent.activeFocus || leftContent.containsFocusItem(window.activeFocusItem))
                drawerButton.forceActiveFocus()
            leftExpanded = false
            leftHost.SplitView.preferredWidth = 28
        } else {
            leftExpanded = true
            leftHost.SplitView.preferredWidth = Math.max(320,
                Math.min(savedLeftWidth || Math.round(workbench.width * leftRatio), workbench.width - 500))
        }
    }
    function focusSearch() {
        if (!leftExpanded) toggleLeft()
        flowCanvas.focusSearch()
    }
    function saveProject(asNew) {
        unsavedSaving = false
        if (asNew) saveFile.open()
        else app.save()
    }
    Component.onCompleted: {
        QuiColor.themeMode = QuiColor.Light
        QuiColor.Background = "#f5f7fa"
        QuiColor.Primary = "#ffffff"
        QuiColor.Border = "#dce1e8"
        QuiColor.Highlight = "#1887d8"
        QuiColor.FontPrimary = "#344253"
        QuiColor.Button = "#f4f6f9"
        QuiFont.Body = Qt.font({ family: "Microsoft YaHei UI", pixelSize: 12 })
        QuiFont.Caption = Qt.font({ family: "Microsoft YaHei UI", pixelSize: 11 })
        Qt.callLater(resetLayout)
    }
    onClosing: function(event) {
        if (!closingApproved) { event.accepted = false; app.requestClose() }
    }
    Action { id: newAction; text: "新建项目"; shortcut: "Ctrl+N"; onTriggered: app.requestNew() }
    Action { id: openAction; text: "打开项目…"; shortcut: "Ctrl+O"; onTriggered: openFile.open() }
    Action { id: saveAction; text: "保存"; shortcut: "Ctrl+S"; onTriggered: window.saveProject(false) }
    Action { id: saveAsAction; text: "另存为…"; shortcut: "Ctrl+Shift+S"; onTriggered: window.saveProject(true) }
    Action { id: runAction; text: "单次"; shortcut: "F5"; enabled: !app.busy; onTriggered: app.runOnce() }
    Action {
        id: loopAction
        text: app.runState === "Stopping" ? "等待本轮结束" : app.runState === "RunningLoop" ? "停止循环" : "循环"
        shortcut: "F6"
        enabled: !app.busy || app.runState === "RunningLoop"
        onTriggered: app.toggleLoop()
    }
    Shortcut { sequence: "Ctrl+F"; onActivated: window.focusSearch() }

    header: ToolBar {
        height: 46
        background: Rectangle { color: QuiColor.Primary; border.color: QuiColor.Border }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 7
            QuiButton { text: "文件"; focusPolicy: Qt.TabFocus; onClicked: fileMenu.popup() }
            QuiButton { text: "工具"; focusPolicy: Qt.TabFocus; onClicked: toolsMenu.popup() }
            QuiButton { text: "帮助"; focusPolicy: Qt.TabFocus; onClicked: helpMenu.popup() }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: QuiColor.Border }
            QuiTextIconButton { action: saveAction; iconSource: QuiFontIcon.Save; display: AbstractButton.TextBesideIcon }
            QuiTextIconButton { action: runAction; iconSource: QuiFontIcon.Play; display: AbstractButton.TextBesideIcon }
            QuiTextIconButton { action: loopAction; iconSource: app.runState === "RunningLoop" ? QuiFontIcon.Stop : QuiFontIcon.RepeatAll; display: AbstractButton.TextBesideIcon }
            Item { Layout.fillWidth: true }
            QuiText { text: app.busy ? app.runState : "就绪"; color: app.busy ? QuiColor.Highlight : QuiColor.FontCaption }
            QuiButton { text: "运行时复位"; enabled: !app.busy; focusPolicy: Qt.TabFocus; onClicked: resetDialog.open() }
        }
    }
    QuiMenu {
        id: fileMenu
        QuiMenuItem { action: newAction }
        QuiMenuItem { action: openAction }
        QuiMenuItem { action: saveAction }
        QuiMenuItem { action: saveAsAction }
        MenuSeparator {}
        QuiMenuItem { text: "退出"; onTriggered: app.requestClose() }
    }
    QuiMenu {
        id: toolsMenu
        QuiMenuItem { text: "新建工具…"; enabled: !app.busy; onTriggered: author.createDefinition() }
        QuiMenuItem { text: "编辑工具…"; enabled: !app.busy; onTriggered: author.chooseDefinition() }
        QuiMenuItem { text: "刷新 Python 工具"; enabled: !app.busy; onTriggered: app.refreshPython() }
    }
    QuiMenu {
        id: helpMenu
        QuiMenuItem { text: "工具作者说明"; onTriggered: authorHelp.open() }
        QuiMenuItem { text: "关于 VisionRT"; onTriggered: about.open() }
    }

    QuiSplitView {
        id: workbench
        anchors.fill: parent
        orientation: Qt.Horizontal
        backgroundColor: QuiColor.Border
        handleColor: "#adb8c6"
        onWidthChanged: {
            if (window.leftExpanded && !leftHost.SplitHandle.pressed && workbench.width > 200) {
                leftHost.SplitView.preferredWidth = Math.round(workbench.width * window.leftRatio)
            }
        }
        handle: Rectangle {
            implicitWidth: window.leftExpanded ? 4 : 0
            implicitHeight: workbench.height
            visible: window.leftExpanded
            enabled: window.leftExpanded
            color: SplitHandle.hovered || SplitHandle.pressed ? QuiColor.Highlight : QuiColor.Border
            containmentMask: Item {
                x: window.leftExpanded ? -3 : 0
                width: window.leftExpanded ? 10 : 0
                height: window.leftExpanded ? workbench.height : 0
            }
        }
        Item {
            id: leftHost
            SplitView.fillWidth: false
            SplitView.minimumWidth: window.leftExpanded ? 320 : 28
            SplitView.maximumWidth: window.leftExpanded ? Math.round(workbench.width * 0.55) : 28
            SplitView.preferredWidth: Math.round(workbench.width * window.leftRatio)
            onWidthChanged: {
                if (window.leftExpanded && leftHost.SplitHandle.pressed && workbench.width > 200) {
                    window.leftRatio = Math.max(0.20, Math.min(0.55, leftHost.width / workbench.width))
                }
            }
            FocusScope {
                id: leftContent
                anchors.fill: parent; anchors.rightMargin: 28
                visible: window.leftExpanded
                enabled: window.leftExpanded
                clip: true
                function containsFocusItem(item) {
                    while (item) { if (item === leftContent) return true; item = item.parent }
                    return false
                }
                QuiSplitView {
                    id: leftSplit
                    anchors.fill: parent
                    orientation: Qt.Vertical
                    backgroundColor: QuiColor.Border
                    handleColor: "#adb8c6"
                    onHeightChanged: {
                        if (window.libraryExpanded && leftSplit.height > 200) {
                            library.SplitView.preferredHeight = Math.round(leftSplit.height * window.libraryRatio)
                        }
                    }
                    ToolLibrary {
                        id: library
                        expanded: window.libraryExpanded
                        SplitView.fillHeight: false
                        SplitView.preferredHeight: Math.round(leftSplit.height * window.libraryRatio)
                        SplitView.minimumHeight: expanded ? 160 : 30
                        SplitView.maximumHeight: expanded ? Math.round(leftSplit.height * 0.60) : 30
                        onHeightChanged: {
                            if (window.libraryExpanded && leftSplit.height > 200 && library.SplitHandle.pressed) {
                                window.libraryRatio = Math.max(0.20, Math.min(0.60, library.height / leftSplit.height))
                            }
                        }
                        onToggleRequested: {
                            if (window.libraryExpanded) {
                                window.savedLibraryHeight = height
                                window.libraryExpanded = false
                                SplitView.preferredHeight = 30
                            } else {
                                window.libraryExpanded = true
                                SplitView.preferredHeight = Math.min(window.savedLibraryHeight || Math.round(leftSplit.height * window.libraryRatio),
                                    Math.round(leftSplit.height * 0.60))
                            }
                        }
                    }
                    FlowCanvas {
                        id: flowCanvas
                        SplitView.fillHeight: true
                        SplitView.minimumHeight: 260
                    }
                }
            }
            Rectangle {
                width: 28; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: parent.right
                color: QuiColor.Primary
                QuiTextIconButton {
                    id: drawerButton
                    objectName: "drawerToggle"
                    anchors.centerIn: parent; width: 28; height: 64
                    iconSource: window.leftExpanded ? QuiFontIcon.ChevronLeft : QuiFontIcon.ChevronRight
                    text: window.leftExpanded ? "收起左侧工作区" : "展开左侧工作区"
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
            SplitView.minimumWidth: 500
            backgroundColor: QuiColor.Border
            handleColor: "#adb8c6"
            onHeightChanged: {
                if (rightSplit.height > 200) {
                    results.SplitView.preferredHeight = Math.round(rightSplit.height * window.resultsRatio)
                    parameters.SplitView.preferredHeight = Math.round(rightSplit.height * (1.0 - window.resultsRatio))
                }
            }
            ResultsPanel {
                id: results
                SplitView.fillHeight: true
                SplitView.preferredHeight: Math.round(rightSplit.height * window.resultsRatio)
                SplitView.minimumHeight: 280
                onHeightChanged: {
                    if (rightSplit.height > 200 && results.SplitHandle.pressed) {
                        window.resultsRatio = Math.max(0.30, Math.min(0.75, results.height / rightSplit.height))
                    }
                }
                onRoiRequested: parameters.focusRoi()
            }
            ParameterPanel {
                id: parameters
                SplitView.fillHeight: true
                SplitView.preferredHeight: Math.round(rightSplit.height * (1.0 - window.resultsRatio))
                SplitView.minimumHeight: 220
            }
        }
    }
    footer: Rectangle {
        height: 28; color: QuiColor.Primary
        RowLayout {
            anchors.fill: parent; anchors.margins: 5
            QuiText { text: app.timingText; font: QuiFont.Caption }
            Item { Layout.fillWidth: true }
            QuiText { Layout.maximumWidth: window.width * 0.48; elide: Text.ElideRight; text: app.diagnostic; color: "#ba641a"; font: QuiFont.Caption }
            QuiButton { text: "操作提示"; implicitHeight: 23; focusPolicy: Qt.TabFocus; onClicked: noticeDialog.open() }
        }
    }
    FileDialog {
        id: openFile; title: "打开工程"
        nameFilters: ["VisionRT 工程 (*.vrt)", "YAML 文件 (*.yaml *.yml)", "所有文件 (*)"]
        onAccepted: app.requestOpen(window.localPath(selectedFile))
    }
    FileDialog {
        id: saveFile; title: "保存工程"; fileMode: FileDialog.SaveFile
        nameFilters: ["VisionRT 工程文件 (*.vrt)"]; defaultSuffix: "vrt"
        onAccepted: {
            if (window.unsavedSaving) app.resolveUnsaved("save", window.localPath(selectedFile))
            else app.save(window.localPath(selectedFile))
        }
        onRejected: {
            if (window.unsavedSaving) { app.resolveUnsaved("cancel"); unsaved.close() }
            window.unsavedSaving = false
        }
    }
    WorkbenchDialog {
        id: unsaved; title: "保存工程修改？"
        message: "当前工程包含未保存的修改。保存失败将保留当前工程。"
        positiveText: "保存"; negativeText: "取消"; neutralText: "不保存"; useNeutralButton: true
        onPositiveClickListener: function() { window.unsavedSaving = true; app.resolveUnsaved("save") }
        onNegativeClickListener: function() { window.unsavedSaving = false; app.resolveUnsaved("cancel"); unsaved.close() }
        onNeutralClickListener: function() { window.unsavedSaving = false; app.resolveUnsaved("discard"); unsaved.close() }
    }
    WorkbenchDialog {
        id: resetDialog; title: "运行时复位"; message: "清除运行结果并释放实例？所有未保存的工程配置和当前布局将保留。"
        positiveText: "复位"; onPositiveClicked: app.resetRuntime()
    }
    WorkbenchDialog { id: about; title: "关于 VisionRT"; message: app.aboutText; useNegativeButton: false; positiveText: "关闭" }
    WorkbenchDialog {
        id: authorHelp; title: "工具作者说明"; width: Math.min(700, window.width - 40)
        useNegativeButton: false; positiveText: "关闭"
        message: "在便携应用的 tools/custom 受管目录准备 Python 脚本或包，再从“工具 → 新建工具”登记入口。包内使用相对 import。输入为只读借用，不得原地修改图像或数组；输出必须包含定义的全部业务字段。Missing 不等于空数组。NumPy 数组必须匹配定义的 dtype、rank 和 dense / batch_ragged 布局。类工具可实现 initialize / execute / release。修改源码后在空闲时手动刷新 Python 工具；刷新会释放实例并清除旧实现结果。内置 C++ 定义只读，可复制为自定义元数据，不能改变注册签名。"
    }
    WorkbenchDialog {
        id: noticeDialog; title: "应用操作提示"; width: Math.min(800, window.width - 40)
        useNegativeButton: false; positiveText: "关闭"
        message: window.notices.join("\n\n")
    }
    ToolDefinitionDialog { id: author; hostWidth: window.width; hostHeight: window.height }
    QuiInfoBar { id: info; root: window.contentItem; maxWidth: Math.min(520, window.width - 40) }
    Timer { id: toastExpiry; interval: 6000; onTriggered: { info.clearAllInfo(); window.toastCount = 0 } }
    Connections {
        target: app
        function onUnsavedDecisionRequired() { unsaved.open() }
        function onSavePathRequired() { saveFile.open() }
        function onCloseReady() { window.closingApproved = true; window.close() }
        function onProjectReplaced() { unsaved.close(); window.unsavedSaving = false; window.resetLayout() }
        function onProjectChanged() { if (!app.dirty && window.unsavedSaving) { unsaved.close(); window.unsavedSaving = false } }
        function onDiagnosticChanged() {
            if (!app.diagnostic) return
            let list = window.notices.slice()
            if (!list.length || list[0] !== app.diagnostic) list.unshift(app.diagnostic)
            window.notices = list.slice(0, 100)
            if (window.toastCount >= 3) { info.clearAllInfo(); window.toastCount = 0 }
            info.showWarning("操作提示", 6000, app.diagnostic)
            window.toastCount += 1
            toastExpiry.restart()
        }
    }
}
