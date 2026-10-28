#include "ui/AppController.h"
#include "ui/Theme.h"
#include "WorkbenchFixtures.h"
#include <quickui/QuiNativeFileDialog.h>

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

void readImageToolUiContract();

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
          "def run(inputs):\n    return {'outputs': {'mode_value': 'directory', '__status_code': 0, '__status_message': ''}, 'detail': ''}\n");
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
    require(app.setParameterPath("file", false, unsavedPath) && value("file") == unsavedPath,
            "AppController::setParameterPath QString overload rejected valid local path");
    const auto testLocal = root.filePath(QStringLiteral("测试 目录 # 1/文件 %20.png"));
    const auto testUrl = QUrl::fromLocalFile(testLocal);
    require(quickui::QuiNativeFileDialog::toLocalPath(testUrl) == testLocal,
            "QuiNativeFileDialog::toLocalPath damaged spaces or percent");
    require(quickui::QuiNativeFileDialog::toFileUrl(testLocal) == testUrl,
            "QuiNativeFileDialog::toFileUrl damaged local file path");

    const auto unsavedProject = root.filePath("unsaved_paths.vrt.json");
    require(app.save(unsavedProject), "Unsaved path project could not save");
    app.requestOpen(unsavedProject);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 3000), "Relative path project did not reopen");
    app.selectNode(app.nodes().front().toMap().value("id").toString());
    require(value("file") == unsavedPath && !app.dirty(),
            "Selected unicode path changed during first save and reopen");
    std::cout << "[Path project state] Unsaved projects accept local URLs without saving; relative Unicode paths retain their text after save/reopen.\n";
    std::cout << "[Evidence T-05] QuickUI QuiFileFolderDialog and QuiNativeFileDialog single-button path contracts verified.\n";

    readImageToolUiContract();
}

void readImageToolUiContract()
{
    const auto require = [](bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };

    const auto previous = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    const auto restore = qScopeGuard([&] { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous); });

    visionrt::testing::WorkbenchInstallation installation;
    visionrt::AppController app(1, installation.directory());
    require(QTest::qWaitFor([&] { return !app.busy(); }, 10000), "image read catalog did not become ready");

    app.addNode("visionrt.image.read", 0, 10);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "image read node did not synchronize");
    require(app.nodes().size() == 1, "image read tool was not added");
    const auto nodeId = app.nodes().front().toMap().value("id").toString();
    app.selectNode(nodeId);

    const auto baselineProject = installation.directory() + "/baseline_read_image.vrt.json";
    require(app.save(baselineProject), "cannot save baseline project");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "baseline save did not settle");
    require(!app.dirty(), "baseline project is dirty");

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport \".\"\n"
        "Item { id: root; property string fieldId: 'path'; width: 700; height: 36; "
        "readonly property var currentParameter: app.parameters.find(function(row) { return row.id === root.fieldId }); "
        "Loader { anchors.fill: parent; active: root.currentParameter !== undefined; "
        "sourceComponent: ParameterEditor { parameter: root.currentParameter } } }",
        QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/ReadImageContract.qml")));
    std::unique_ptr<QObject> owner(component.create());
    if (!owner)
        throw std::runtime_error(component.errorString().toStdString());
    auto *editor = qobject_cast<QQuickItem *>(owner.get());
    require(editor != nullptr, "read image parameter root is not an item");
    QQuickWindow window;
    window.resize(980, 640);
    editor->setParentItem(window.contentItem());
    window.show();
    window.requestActivate();
    require(QTest::qWaitForWindowActive(&window), "read image window did not become active");

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
            throw std::runtime_error("read image dialog action unavailable");
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

    // 1. Verify single button UI on ParameterEditor for editor === "path"
    auto *chooseBtn = findVisible([&](QQuickItem *item) {
        return item->property("text").toString() == QStringLiteral("选择路径")
            && item->metaObject()->indexOfSignal("clicked()") >= 0;
    });
    require(chooseBtn != nullptr, "read image path parameter does not have single '选择路径' button");

    // Verify QuiFileFolderDialog exists and mode is FileOrFolder
    QObject *picker = nullptr;
    for (auto *object : editor->findChildren<QObject *>())
    {
        if (object->property("selectedPath").isValid() && object->property("dialogMode").isValid())
            picker = object;
    }
    require(picker != nullptr, "QuiFileFolderDialog is missing on read image path parameter");
    require(picker->property("dialogMode").toInt() == 0, "read image path dialogMode is not FileOrFolder (0)");

    // 2. Test opening and cancelling the dialog
    click(chooseBtn);
    require(QTest::qWaitFor([&] { return picker->property("visible").toBool(); }, 1500),
            "clicking '选择路径' did not open picker");
    for (auto *w : QGuiApplication::topLevelWindows()) {
        if (w->isVisible()) {
            QTest::keyClick(w, Qt::Key_Escape);
        }
    }
    require(QTest::qWaitFor([&] { return !picker->property("visible").toBool(); }, 1500),
            "Escape did not cancel the path picker");
    require(value("path").isEmpty() && !app.dirty(), "cancelled picker changed parameter or marked project dirty");

    // 3. Test File selection: bus.jpg
    QString imageDir = QStringLiteral("D:/Project/InferRT/assets/pics");
    if (!QDir(imageDir).exists())
        imageDir = QStringLiteral("F:/Projects/InferRT/assets/pics");
    const QString imageFile = imageDir + QStringLiteral("/bus.jpg");
    require(QFile::exists(imageFile), "InferRT test asset bus.jpg is missing");
    click(chooseBtn);
    require(QTest::qWaitFor([&] { return picker->property("visible").toBool(); }, 1500),
            "picker did not open for file selection");
    QMetaObject::invokeMethod(picker, "accepted", Q_ARG(QString, imageFile));
    QMetaObject::invokeMethod(picker, "close");
    require(QTest::qWaitFor([&] { return !picker->property("visible").toBool(); }, 1500),
            "accepting path did not close picker");
    require(value("path") == imageFile, "path parameter was not set to selected image file");
    require(app.dirty(), "selecting image file did not mark project dirty");

    // Run pipeline once with the selected image file
    app.runOnce();
    require(QTest::qWaitFor([&] { return !app.busy(); }, 10000), "pipeline execution with bus.jpg timed out");
    require(app.runState() == QStringLiteral("Idle"), "pipeline execution failed to return to Idle");
    for (const auto &d : app.diagnostics()) {
        require(!d.contains(QStringLiteral("visionrt.image.read")) && !d.contains(QStringLiteral("图像读取")),
                "pipeline execution with bus.jpg produced node errors");
    }
    require(!app.imageOutputs().isEmpty(), "image read produced no image outputs for bus.jpg");

    // 4. Test Directory selection
    require(QDir(imageDir).exists(), "InferRT test asset dir pics is missing");
    click(chooseBtn);
    require(QTest::qWaitFor([&] { return picker->property("visible").toBool(); }, 1500),
            "picker did not open for directory selection");
    QMetaObject::invokeMethod(picker, "accepted", Q_ARG(QString, imageDir));
    QMetaObject::invokeMethod(picker, "close");
    require(QTest::qWaitFor([&] { return !picker->property("visible").toBool(); }, 1500),
            "accepting directory path did not close picker");
    require(value("path") == imageDir, "path parameter was not set to selected directory");

    require(app.setParameter("count", false, "6"), "failed to set count parameter to 6");

    // Run pipeline once with the selected image directory
    app.runOnce();
    require(QTest::qWaitFor([&] { return !app.busy(); }, 10000), "pipeline execution with directory timed out");
    require(app.runState() == QStringLiteral("Idle"), "directory pipeline execution failed to return to Idle");
    for (const auto &d : app.diagnostics()) {
        require(!d.contains(QStringLiteral("visionrt.image.read")) && !d.contains(QStringLiteral("图像读取")),
                "directory pipeline execution produced node errors");
    }
    require(!app.imageOutputs().isEmpty(), "image read produced no image outputs for directory");
    require(app.imageCount() == 6, "directory image read did not load all 6 images");

    // Verify switching image index in directory
    app.setImageIndex(1);
    require(app.imageIndex() == 1, "image index switch failed");

    // 5. Save and Reopen project
    QTemporaryDir tempSaveDir;
    require(tempSaveDir.isValid(), "temporary save dir invalid");
    const auto savedProjectPath = tempSaveDir.filePath("test_image_read.vrt.json");
    require(app.save(savedProjectPath), "failed to save image read project");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "save did not finish");

    app.requestOpen(savedProjectPath);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "reopen did not finish");
    require(app.nodes().size() == 1, "reopened project has no nodes");
    app.selectNode(app.nodes().front().toMap().value("id").toString());
    // A same-drive resource is stored relative to the project, so what has to survive is the
    // resource the stored value points at, not the spelling that happened to be typed before saving.
    const auto reopenedPath = value("path");
    const auto reopenedAbsolute
        = QFileInfo(reopenedPath).isAbsolute()
              ? reopenedPath
              : QDir(QFileInfo(savedProjectPath).absolutePath()).absoluteFilePath(reopenedPath);
    const auto pathMessage = "saved directory path did not survive reopen: stored=" + reopenedPath.toStdString()
                             + " resolved=" + QFileInfo(reopenedAbsolute).canonicalFilePath().toStdString()
                             + " expected=" + QFileInfo(imageDir).canonicalFilePath().toStdString();
    require(QFileInfo(reopenedAbsolute).canonicalFilePath() == QFileInfo(imageDir).canonicalFilePath(),
            pathMessage.c_str());

    std::cout << "[Evidence ReadImage UI] '读取图像' tool single-button path picker successfully tested for both image file ("
              << imageFile.toStdString() << ") and image directory (" << imageDir.toStdString() << ") with pipeline execution and 6 images verified.\n";
}
