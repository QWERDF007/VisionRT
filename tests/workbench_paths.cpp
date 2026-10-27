#include "ui/AppController.h"
#include "ui/Theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <iostream>
#include <memory>
#include <stdexcept>


void pathEditorsContract()
{
    const auto require = [](bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };
    const auto previous = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    const auto restore = qScopeGuard([&] { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous); });
    QTemporaryDir directory;
    require(directory.isValid(), "path fixture directory is invalid");
    QDir root(directory.path());
    require(root.mkpath("tools-package/tools/custom/Test"), "cannot create path tool directory");
    const auto write = [&](const QString &path, const QByteArray &content)
    {
        QFile file(root.filePath(path));
        require(QDir().mkpath(QFileInfo(file).absolutePath()), "cannot create fixture parent directory");
        require(file.open(QIODevice::WriteOnly), "cannot create path fixture file");
        require(file.write(content) == content.size(), "path fixture write failed");
    };
    write("tools-package/module_index.json",
          R"json({
  "schema_version": 1,
  "variant": "test",
  "tools": [
    {"tool_id": "test.paths", "definition": "tools/custom/Test/paths.json"}
  ],
  "modules": [],
  "runtime_files": []
})json");
    write("tools-package/python/custom/Test/run.py",
          "def run(inputs):\n    return {'__status_code': 0, '__status_message': '', 'mode_value': 'directory'}\n");
    write("tools-package/tools/custom/Test/paths.json",
          R"json({
  "schema_version": 4,
  "tool_id": "test.paths",
  "display_name": "路径测试",
  "version": "1.0.0",
  "kind": "python_function",
  "entry": {"package": "Test", "file": "run.py", "symbol": "run"},
  "init_params": [],
  "inputs": [
    {"id": "file", "name": "文件", "type": "String", "default": "original file", "editor": "file"},
    {"id": "folder", "name": "目录", "type": "String", "default": "original folder", "editor": "directory"},
    {"id": "mode", "name": "模式", "type": "String", "default": "file", "options": ["file", "directory"]},
    {"id": "path", "name": "动态路径", "type": "String", "default": "original path", "editor": "path", "path_mode_from": "mode"}
  ],
  "outputs": [
    {"id": "mode_value", "name": "模式输出", "type": "String"}
  ]
})json");
    const QString nested = QStringLiteral("测试 空格/") + QString(40, 'a') + "/" + QString(40, 'b');
    const QString folderName = QStringLiteral("选择的目录 # %");
    const QString fileName = QStringLiteral("图像 示例 # %.txt");
    require(root.mkpath(nested + "/" + folderName), "cannot create unicode path fixture");
    write(nested + "/" + fileName, "path selection fixture\n");

    visionrt::AppController app(1, directory.path());
    require(QTest::qWaitFor([&] { return !app.busy(); }, 3000), "path tool initialization did not finish");
    app.addNode("test.paths", 0, 10);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
    require(app.nodes().size() == 1, "path fixture tool was not discovered");
    app.selectNode(app.nodes().front().toMap().value("id").toString());
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport \".\"\n"
        "Item { id: root; property string fieldId: 'file'; width: 700; height: 36; "
        "readonly property var currentParameter: app.parameters.find(function(row) { return row.id === root.fieldId }); "
        "Loader { anchors.fill: parent; active: root.currentParameter !== undefined; "
        "sourceComponent: ParameterEditor { parameter: root.currentParameter } } }",
        QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/PathContract.qml")));
    std::unique_ptr<QObject> owner(component.create());
    if (!owner)
        throw std::runtime_error(component.errorString().toStdString());
    auto *editor = qobject_cast<QQuickItem *>(owner.get());
    require(editor != nullptr, "path parameter root is not an item");
    QQuickWindow window;
    window.resize(980, 640);
    editor->setParentItem(window.contentItem());
    window.show();
    window.requestActivate();
    require(QTest::qWaitForWindowActive(&window), "path window did not become active");
    const auto findVisible = [&](const auto &matches) -> QQuickItem *
    {
        QList<QQuickItem *> pending;
        for (auto *w : QGuiApplication::topLevelWindows())
        {
            if (auto *qw = qobject_cast<QQuickWindow *>(w))
            {
                if (qw->isVisible() && qw->contentItem())
                    pending.append(qw->contentItem());
            }
        }
        while (!pending.isEmpty())
        {
            auto *item = pending.takeLast();
            if (item->isVisible() && matches(item))
                return item;
            pending.append(item->childItems());
        }
        return nullptr;
    };
    const auto click = [&](QQuickItem *item)
    {
        if (!item || !item->isVisible() || !item->isEnabled())
            throw std::runtime_error("path dialog action is unavailable: " + (item
                ? QString("%1 text=%2 enabled=%3").arg(item->metaObject()->className(), item->property("text").toString())
                    .arg(item->isEnabled()).toStdString()
                : std::string("missing item")));
        auto *win = item->window() ? item->window() : &window;
        QTest::mouseClick(win, Qt::LeftButton, Qt::NoModifier,
                         item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        QTest::qWait(50);
    };
    const auto value = [&](const QString &id)
    {
        for (const auto &entry : app.parameters())
            if (entry.toMap().value("id").toString() == id)
                return entry.toMap().value("valueText").toString();
        return QString{};
    };
    const auto project = root.filePath("paths.vrt.json");
    for (const auto &id : {QStringLiteral("file"), QStringLiteral("folder"), QStringLiteral("path")})
    {
        if (id == "path") require(app.setParameter("mode", false, "directory"), "dynamic path mode did not change");
        editor->setProperty("fieldId", id);
        QTest::qWait(30);
        QObject *picker = nullptr;
        for (auto *object : editor->findChildren<QObject *>())
            if (object->property(id == "file" ? "selectedFile" : "selectedFolder").isValid())
                picker = object;
        require(picker != nullptr, "path chooser is missing");
        require(picker->setProperty("acceptLabel", QStringLiteral("确认选择")), "path chooser has no accept label");
        require(picker->setProperty("currentFolder", QUrl::fromLocalFile(root.filePath(nested))),
                "path chooser did not accept its initial folder");
        const auto openPicker = [&]
        {
            picker->setProperty("currentFolder", QUrl::fromLocalFile(root.filePath(nested)));
            click(findVisible([&](QQuickItem *item)
            {
                return item->property("text").toString() == (id == "file" ? QStringLiteral("选择文件") : QStringLiteral("选择文件夹"))
                    && item->metaObject()->indexOfSignal("clicked()") >= 0;
            }));
            require(QTest::qWaitFor([&] { return picker->property("visible").toBool(); }, 1500),
                    "path chooser did not open from its embedded button");
        };
        app.save(project);
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        require(!app.dirty(), "path baseline could not be saved");
        const auto original = value(id);
        QSignalSpy changes(&app, &visionrt::AppController::parametersChanged);
        openPicker();
        std::cerr << "AFTER OPEN PICKER: visible=" << picker->property("visible").toBool() << std::endl;
        for (auto *w : QGuiApplication::topLevelWindows()) {
            std::cerr << "  WIN: " << w->metaObject()->className() << " active=" << w->isActive() << " visible=" << w->isVisible() << std::endl;
        }
        // Try keyClick on all windows
        for (auto *w : QGuiApplication::topLevelWindows()) {
            if (w->isVisible()) {
                QTest::keyClick(w, Qt::Key_Escape);
            }
        }
        require(QTest::qWaitFor([&] { return !picker->property("visible").toBool(); }, 1500), "Escape did not cancel the chooser");
        require(value(id) == original && !app.dirty() && changes.isEmpty(), "cancelled path selection changed the project");
        openPicker();
        const auto label = id == "file" ? fileName : folderName;
        require(QTest::qWaitFor([&] { return findVisible([&](QQuickItem *item) { return item->property("text").toString() == label; }) != nullptr; }, 3000),
                "path chooser did not display the unicode fixture");
        click(findVisible([&](QQuickItem *item) { return item->property("text").toString() == label; }));
        click(findVisible([&](QQuickItem *item)
        {
            return item->property("text").toString() == QStringLiteral("确认选择")
                && item->metaObject()->indexOfSignal("clicked()") >= 0;
        }));
        require(QTest::qWaitFor([&] { return !picker->property("visible").toBool(); }, 1500), "path confirmation did not close the chooser");
        const auto expected = QDir::cleanPath(root.filePath(nested + "/" + label));
        require(value(id) == expected, "path selection damaged unicode, spaces, percent or hash characters");
        require(app.dirty() && changes.count() == 1, "path selection did not submit exactly once");
        app.save(project);
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        app.requestOpen(project);
        require(QTest::qWaitFor([&] { return !app.busy(); }, 3000), "path project did not finish reopening");
        app.selectNode(app.nodes().front().toMap().value("id").toString());
        require(value(id) == expected, "selected path did not survive save/reopen");
    }
    const auto editorKind = [&]
    {
        for (const auto &entry : app.parameters())
            if (entry.toMap().value("id").toString() == "path") return entry.toMap().value("editor").toString();
        return QString{};
    };
    require(app.setParameter("mode", false, "file") && editorKind() == "file", "file mode did not select a file editor");
    QObject *filePicker = nullptr;
    for (auto *object : editor->findChildren<QObject *>())
        if (object->property("selectedFile").isValid()) filePicker = object;
    require(filePicker != nullptr, "dynamic file picker is missing");
    click(findVisible([&](QQuickItem *item)
    {
        return item->property("text").toString() == QStringLiteral("选择文件")
            && item->metaObject()->indexOfSignal("clicked()") >= 0;
    }));
    require(QTest::qWaitFor([&] { return filePicker->property("visible").toBool(); }, 1500),
            "metadata-driven file picker did not open");
    const auto selectedPath = value("path");
    require(app.setParameter("mode", false, "directory") && editorKind() == "directory", "directory mode did not update");
    require(QTest::qWaitFor([&] { return !filePicker->property("visible").toBool(); }, 1500)
                && value("path") == selectedPath,
            "changing path mode left the old picker alive or submitted its selection");
    require(app.setParameter("mode", false, "null", true) && editorKind().isEmpty(),
            "Missing path mode must not silently select the file picker");
    const auto localPath = root.filePath(QStringLiteral("文本 %2f # 路径.txt"));
    require(app.setParameterPath("path", false, QUrl::fromLocalFile(localPath)) && value("path") == localPath,
            "local URL conversion reinterpreted a literal percent escape");
    require(!app.setParameterPath("path", false, QUrl("https://example.invalid/image.png")) && value("path") == localPath,
            "a remote URL was submitted as a local path");
    const auto targetNode = app.selectedNode();
    app.addNode("test.paths", 0, 0);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
    const auto modeSource = app.selectedNode();
    app.selectNode(targetNode);
    require(app.bindInput(app.portContext("mode"), modeSource, "mode_value") && editorKind().isEmpty(),
            "an unresolved upstream mode must not select a picker");
    app.runOnce();
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "upstream path mode did not finish execution");
    require(editorKind() == "directory", "upstream path mode fixture did not produce its declared value");
    require(QTest::qWaitFor([&] { return findVisible([&](QQuickItem *item)
    {
        return item->property("text").toString() == QStringLiteral("选择文件夹")
            && item->metaObject()->indexOfSignal("clicked()") >= 0;
    }) != nullptr; }, 1000), "a new upstream snapshot did not refresh the metadata-driven path picker");
    app.requestNew();
    app.resolveUnsaved("discard");
    require(QTest::qWaitFor([&] { return !app.busy() && app.nodes().isEmpty(); }, 3000),
            "Path verification could not create a fresh unsaved project");
    app.addNode("test.paths", 0, 0);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
    require(app.nodes().size() == 1, "Unsaved project could not create its path parameter tool");
    QSignalSpy saveRequested(&app, &visionrt::AppController::savePathRequired);
    const auto unsavedPath = root.filePath(nested + "/" + fileName);
    require(app.setParameterPath("file", false, QUrl::fromLocalFile(unsavedPath))
                && value("file") == unsavedPath && app.dirty() && saveRequested.isEmpty(),
            "Selecting a local path in an unsaved project required saving or damaged the path");
    const auto relativePath = nested + "/" + fileName;
    require(app.setParameter("file", false, relativePath) && value("file") == relativePath,
            "Text editing implicitly resolved a relative path instead of preserving its value");
    const auto unsavedProject = root.filePath("unsaved_paths.vrt.json");
    require(app.save(unsavedProject), "Unsaved path project could not save");
    app.requestOpen(unsavedProject);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 3000), "Relative path project did not reopen");
    app.selectNode(app.nodes().front().toMap().value("id").toString());
    require(value("file") == relativePath && !app.dirty(),
            "Relative unicode path changed during first save and reopen");
    std::cout << "[Path project state] Unsaved projects accept local URLs without saving; relative Unicode paths retain their text after save/reopen.\n";
    std::cout << "[Evidence T-05] Qt non-native file/folder choosers: actual selection, cancel, unicode/long paths and save/reopen verified.\n";
}
