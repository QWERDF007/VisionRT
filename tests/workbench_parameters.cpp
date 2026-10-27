#include <iostream>
#include "ui/AppController.h"
#include "ui/OutputModel.h"
#include "ui/Theme.h"

#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QInputMethodEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>
#include <stdexcept>


void parameterStatesContract(visionrt::AppController &app);

void parameterEditorsContract()
{
    using namespace visionrt;
    const auto require = [](bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };
    QTemporaryDir directory;
    require(directory.isValid(), "parameter fixture directory is invalid");
    QDir(directory.path()).mkpath("tools-package/tools/custom/Test/Parameters");
    const auto write = [&](const QString &name, const QByteArray &content)
    {
        QFile file(directory.path() + "/tools-package/" + name);
        require(QDir().mkpath(QFileInfo(file).absolutePath()), "cannot create fixture parent directory");
        require(file.open(QIODevice::WriteOnly), "cannot create parameter fixture");
        require(file.write(content) == content.size(), "parameter fixture write failed");
    };
    write("module_index.json",
          R"json({
  "schema_version": 1,
  "variant": "test",
  "tools": [
    {"tool_id": "test.parameters", "definition": "tools/custom/Test/Parameters/tool.json"}
  ],
  "modules": [],
  "runtime_files": []
})json");
    write("python/custom/Test/Parameters/run.py",
          "def run(inputs):\n    return {'__status_code': 0, '__status_message': '', "
          "'ratio': inputs['ratio'], 'count': inputs['count']}\n");
    write("tools/custom/Test/Parameters/tool.json",
          R"json({
  "schema_version": 4,
  "tool_id": "test.parameters",
  "display_name": "参数测试",
  "version": "1.0.0",
  "kind": "python_function",
  "entry": {"package": "Test/Parameters", "file": "run.py", "symbol": "run"},
  "init_params": [],
  "inputs": [
    {"id": "ratio", "name": "比例", "type": "Float", "default": 0.15, "min": 0, "max": 1, "step": 0.05, "decimals": 2},
    {"id": "count", "name": "整数", "type": "Int", "default": 9007199254740993},
    {"id": "path", "name": "路径", "type": "String", "default": "原始路径", "editor": "file"},
    {"id": "enabled", "name": "启用", "type": "Bool", "default": true},
    {"id": "mode", "name": "色彩", "type": "String", "default": "bgr", "options": ["bgr", "gray"]},
    {"id": "labels", "name": "标签列表", "type": "Array", "dtype": "string", "rank": 1, "default": ["a", "b"]},
    {"id": "note", "name": "备注", "type": "String", "default": "中文备注"},
    {"id": "directory", "name": "数据目录", "type": "String", "default": "F:/tmp", "editor": "directory"},
    {"id": "weights", "name": "数值数组", "type": "Array", "dtype": "float64", "rank": 1, "default": [0, 1]},
    {"id": "images", "name": "图像", "type": "Image", "default": [], "required": false},
    {"id": "rois", "name": "区域", "type": "ROI", "default": [], "image_input": "images"}
  ],
  "outputs": [
    {"id": "ratio", "name": "比例", "type": "Float"},
    {"id": "count", "name": "整数", "type": "Int"}
  ]
})json");

    AppController app(1, directory.path());
    for (int attempt = 0; app.busy() && attempt < 100; ++attempt)
        QTest::qWait(10);
    require(!app.busy(), "parameter tool initialization did not finish");
    app.addNode("test.parameters", 0, 100);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
    if (app.nodes().size() != 1)
        throw std::runtime_error("parameter fixture tool was not discovered: " + app.diagnostic().toStdString());
    app.selectNode(app.nodes().front().toMap().value("id").toString());
    require(!app.setParameter("count", false, "-9223372036854775809"), "overflowing negative integer was rounded");
    require(!app.setParameter("ratio", false, ".nan"), "YAML numeric syntax must not be accepted");
    require(!app.setParameter("weights", false, "{shape: [2], data: [0, 1]}"), "YAML arrays must not be accepted");
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport \".\"\n"
        "ParameterEditor { property string fieldId: 'ratio'; width: 600; height: 36; "
        "parameter: app.parameters.find(function(row) { return row.id === fieldId }) }",
        QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/ParameterContract.qml")));
    std::unique_ptr<QObject> owner(component.create());
    if (!owner)
        throw std::runtime_error(component.errorString().toStdString());
    auto *editor = qobject_cast<QQuickItem *>(owner.get());
    require(editor != nullptr, "parameter root is not an item");
    QQuickWindow window;
    window.resize(600, 80);
    editor->setParentItem(window.contentItem());
    window.show();
    require(QTest::qWaitForWindowExposed(&window), "parameter window was not exposed");
    QTest::qWait(50);

    require(Theme::instance()->paramEditorHeight() == 32, "parameter theme height is not the shared 32 pixel contract");
    auto *input = editor->findChild<QQuickItem *>("stepperInput");
    require(input != nullptr, "Float parameter did not use the public spin editor");
    require(input->height() >= 28, "Float input did not inherit the parameter height");
    require(input->property("font").value<QFont>().pixelSize() == Theme::instance()->paramEditorFontSize(),
            "Float input did not inherit the shared parameter font");
    input->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_Up);
    const auto value = [&](const QString &id)
    {
        for (const auto &item : app.parameters())
        {
            const auto row = item.toMap();
            if (row.value("id").toString() == id)
                return row.value("valueText").toString();
        }
        return QString{};
    };
    require(!app.setParameter("missing_port", false, "1"), "unknown parameter was accepted");
    require(app.diagnostic() == QStringLiteral("参数不存在：missing_port"),
            "parameter rejection lost its precise Chinese reason and stable port ID");
    for (const auto &raw : {QString{}, QStringLiteral("null"), QStringLiteral("~"), QStringLiteral("[1]"),
                            QStringLiteral("\"原始引号\"\n中文")})
    {
        require(app.setParameter("note", false, raw), "raw string parameter was rejected");
        require(value("note") == raw, "raw strings were parsed as JSON or YAML literals");
    }
    require(app.setParameter("note", false, "\"JSON 文本\"", true) && value("note") == QStringLiteral("JSON 文本"),
            "explicit JSON string entry failed");
    require(!app.setParameter("note", false, "unquoted", true), "raw JSON mode accepted an unquoted string");
    require(value("ratio") == "0.2", "Float keyboard step was not committed to the real parameter");
    input->setProperty("text", "0.4");
    QTest::keyClick(&window, Qt::Key_Escape);
    require(value("ratio") == "0.2", "Escape committed an unfinished Float draft");
    require(input->property("text").toString() == "0.2", "Escape did not restore the committed Float value");
    editor->setProperty("fieldId", "count");
    QTest::qWait(20);
    input = editor->findChild<QQuickItem *>("stepperInput");
    require(input != nullptr, "integer parameter lost the shared field shell");
    input->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_Up);
    require(value("count") == "9007199254740994", "integer keyboard step lost precision or did not commit");
    input->setProperty("text", "9007199254740996");
    QTest::keyClick(&window, Qt::Key_Up);
    require(value("count") == "9007199254740997", "integer step ignored the uncommitted draft");
    input->setProperty("text", "-9223372036854775809");
    QTest::keyClick(&window, Qt::Key_Up);
    require(value("count") == "9007199254740997"
                && input->property("text").toString() == "-9223372036854775809",
            "invalid integer draft was rounded, discarded or stepped from the old value");
    QTest::keyClick(&window, Qt::Key_Escape);
    require(app.setParameter("count", false, "9223372036854775807"), "Int64 maximum was rejected");
    input->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_Up);
    require(value("count") == "9223372036854775807", "integer increment overflowed at the maximum");
    input->setProperty("text", "9223372036854775808");
    QTest::keyClick(&window, Qt::Key_Return);
    require(value("count") == "9223372036854775807", "out-of-range integer replaced the valid parameter");
    QTest::keyClick(&window, Qt::Key_Escape);
    require(input->property("text").toString() == "9223372036854775807", "Escape lost the full Int64 value");
    require(app.setParameter("count", false, "-9223372036854775808"), "Int64 minimum was rejected");
    input->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_Down);
    require(value("count") == "-9223372036854775808", "integer decrement overflowed at the minimum");
    require(app.setParameter("count", false, "9007199254740994"), "integer restore failed");
    editor->setProperty("fieldId", "path");
    QTest::qWait(20);
    const auto textInput = [](QQuickItem *row) -> QQuickItem *
    {
        for (auto *item : row->findChildren<QQuickItem *>())
            if (item->property("actionIcon").isValid())
                return qobject_cast<QQuickItem *>(item->property("inputItem").value<QObject *>());
        return nullptr;
    };
    auto *pathInput = textInput(editor);
    require(pathInput != nullptr, "text parameter does not expose its editable input");
    pathInput->forceActiveFocus();
    const QString path = QStringLiteral("F:/测试 数据/含\"引号\"/图像.png");
    pathInput->setProperty("text", path);
    QTest::keyClick(&window, Qt::Key_Return);
    bool pathSaved = false;
    for (const auto &item : app.parameters())
    {
        const auto row = item.toMap();
        if (row.value("id").toString() == "path")
            pathSaved = row.value("valueText").toString() == path;
    }
    require(pathSaved, "text parameter did not preserve Unicode, whitespace and quotes");
    const auto project = directory.path() + "/parameters.vrt.json";
    app.save(project);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
    require(!app.dirty(), "parameter save failed");
    owner.reset();
    app.requestOpen(project);
    for (int attempt = 0; app.busy() && attempt < 100; ++attempt)
        QTest::qWait(10);
    require(!app.busy(), "parameter project did not finish reopening");
    app.selectNode(app.nodes().front().toMap().value("id").toString());
    require(value("count") == "9007199254740994", "integer parameter lost precision after reopening");
    app.runNode();
    for (int attempt = 0; app.busy() && attempt < 500; ++attempt)
        QTest::qWait(10);
    require(!app.busy(), "parameter execution did not finish");
    auto *outputs = qobject_cast<OutputModel *>(app.outputModel());
    require(outputs != nullptr, "parameter execution output model is unavailable");
    QMap<QString, QString> results;
    for (int row = 0; row < outputs->rowCount(); ++row)
    {
        const auto output = outputs->get(row);
        results.insert(output.value("portId").toString(), output.value("preview").toString());
    }
    if (results.value("__status_code") != "0")
        throw std::runtime_error("parameter execution failed: " + results.value("__status_message").toStdString()
                                 + " diagnostic=" + app.diagnostic().toStdString()
                                 + " selected=" + app.selectedNode().toStdString()
                                 + " rows=" + std::to_string(outputs->rowCount()));
    require(results.value("ratio").toDouble() == 0.2, "Float parameter changed during save/reopen/execution");
    require(results.value("count") == "9007199254740994", "integer parameter lost precision during Python execution");
    QQmlComponent panelComponent(&engine,
        QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/ParameterPanel.qml")));
    std::unique_ptr<QObject> panelOwner(panelComponent.create());
    if (!panelOwner)
        throw std::runtime_error(panelComponent.errorString().toStdString());
    auto *panel = qobject_cast<QQuickItem *>(panelOwner.get());
    require(panel != nullptr, "parameter panel root is not an item");
    window.resize(980, 520);
    panel->setWidth(980);
    panel->setHeight(520);
    panel->setParentItem(window.contentItem());
    QTest::qWait(50);
    const auto findItem = [](auto &&self, QQuickItem *parent, const QString &name) -> QQuickItem *
    {
        for (auto *child : parent->childItems())
        {
            if (child->objectName() == name)
                return child;
            if (auto *match = self(self, child, name))
                return match;
        }
        return nullptr;
    };
    auto *ratioRow = findItem(findItem, panel, "parameter_ratio");
    require(ratioRow != nullptr, "parameter panel did not render the Float row");
    input = findItem(findItem, ratioRow, "stepperInput");
    require(input != nullptr, "parameter panel did not render the Float input");
    input->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_Up);
    QTest::keyClick(&window, Qt::Key_Up);
    require(value("ratio") == "0.3", "parameter panel lost keyboard focus after the first committed step");
    require(input->hasActiveFocus(), "parameter refresh recreated the focused field");
    QSignalSpy parameterChanges(&app, &AppController::parametersChanged);
    input->setProperty("text", "0.35");
    QTest::keyClick(&window, Qt::Key_Return);
    require(value("ratio") == "0.35", "panel Float Enter did not commit");
    panel->forceActiveFocus();
    require(parameterChanges.count() == 1, "Enter followed by blur committed the parameter more than once");
    input->forceActiveFocus();
    input->setProperty("text", "0.35garbage");
    QTest::keyClick(&window, Qt::Key_Return);
    require(value("ratio") == "0.35", "invalid Float draft replaced the committed value");
    QTest::keyClick(&window, Qt::Key_Escape);
    require(input->property("text").toString() == "0.35", "Escape did not restore the panel Float");
    input->setProperty("text", "0.4");
    panel->setEnabled(false);
    window.contentItem()->forceActiveFocus();
    QTest::qWait(20);
    require(value("ratio") == "0.35", "disabling the parameter panel committed a Float draft");
    panel->setEnabled(true);
    input->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_Escape);
    QInputMethodEvent composing(QStringLiteral("拼"), {});
    QGuiApplication::sendEvent(&window, &composing);
    require(input->property("inputMethodComposing").toBool(), "numeric input did not receive IME composition");
    QTest::keyClick(&window, Qt::Key_Up);
    require(value("ratio") == "0.35", "numeric step submitted a value during IME composition");
    QInputMethodEvent cancelComposition;
    QGuiApplication::sendEvent(&window, &cancelComposition);
    const auto firstNode = app.selectedNode();
    app.addNode("test.parameters", 240, 0);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
    const auto secondNode = app.selectedNode();
    require(firstNode != secondNode, "second parameter test node was not selected");
    app.selectNode(firstNode);
    QTest::qWait(20);
    ratioRow = findItem(findItem, panel, "parameter_ratio");
    input = findItem(findItem, ratioRow, "stepperInput");
    input->forceActiveFocus();
    input->setProperty("text", "0.9");
    app.selectNode(secondNode);
    QTest::qWait(20);
    require(value("ratio") == "0.15", "node switching committed the previous node draft to the new node");
    app.selectNode(firstNode);
    QTest::qWait(20);
    require(value("ratio") == "0.35", "node switching silently committed an unfinished draft");
    auto *pathRow = findItem(findItem, panel, "parameter_path");
    require(pathRow != nullptr, "parameter panel did not render the path row");
    pathInput = textInput(pathRow);
    require(pathInput != nullptr, "parameter panel path input is unavailable");
    pathInput->forceActiveFocus();
    require(app.setParameter("path", false, "external path"), "external path update failed");
    require(pathInput->property("text").toString() == "external path",
            "focused clean text field did not follow an external parameter update");
    pathInput->setProperty("text", "unfinished path");
    require(app.setParameter("path", false, "new external path"), "second external path update failed");
    require(pathInput->property("text").toString() == "unfinished path", "external update erased the active draft");
    QTest::keyClick(&window, Qt::Key_Escape);
    require(pathInput->property("text").toString() == "new external path", "Escape did not use the latest model value");
    pathInput->setProperty("text", "disabled draft");
    panel->setEnabled(false);
    window.contentItem()->forceActiveFocus();
    QTest::qWait(20);
    require(value("path") == "new external path", "disabling the parameter panel committed a text draft");
    panel->setEnabled(true);
    pathInput->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_Escape);
    require(pathInput->property("text").toString() == "new external path", "disabled draft could not be cancelled");
    pathInput->setProperty("text", "unfinished IME draft");
    QInputMethodEvent textComposition(QStringLiteral("拼"), {});
    QGuiApplication::sendEvent(&window, &textComposition);
    require(pathInput->property("inputMethodComposing").toBool(), "text input did not receive IME composition");
    QTest::keyClick(&window, Qt::Key_Escape);
    require(pathInput->property("text").toString() == "unfinished IME draft",
            "Escape discarded the text draft while the IME was composing");
    require(value("path") == "new external path", "IME cancellation committed an unfinished text draft");
    QInputMethodEvent endTextComposition;
    QGuiApplication::sendEvent(&window, &endTextComposition);
    QTest::keyClick(&window, Qt::Key_Escape);
    require(pathInput->property("text").toString() == "new external path", "Escape did not cancel the draft after IME completion");
    panel->forceActiveFocus();
    QTest::mouseMove(&window, QPoint(window.width() - 5, window.height() - 5));
    QTest::qWait(50);
    const auto fieldPixel = pathInput->mapToScene(QPointF(pathInput->width() - 4, 2)) * window.devicePixelRatio();
    const auto normalField = window.grabWindow().pixelColor(fieldPixel.toPoint());
    QTest::mouseMove(&window, pathInput->mapToScene(QPointF(pathInput->width() - 8, pathInput->height() / 2)).toPoint());
    QTest::qWait(50);
    const auto hoveredField = window.grabWindow().pixelColor(fieldPixel.toPoint());
    require(hoveredField != normalField, "text field has no visible hover feedback");
    QQuickItem *pathField = nullptr;
    QQuickItem *pathAction = nullptr;
    for (auto *item : pathRow->findChildren<QQuickItem *>())
    {
        if (item->property("inputItem").value<QObject *>() == pathInput
            && item->property("readOnly").isValid()) pathField = item;
        if (item->property("text").toString() == QStringLiteral("选择文件")
            && item->metaObject()->indexOfSignal("clicked()") >= 0) pathAction = item;
    }
    require(pathField && pathAction, "path field public controls are missing");
    const auto savedPath = value("path");
    const auto pathChanges = parameterChanges.count();
    require(pathField->setProperty("readOnly", true), "text field read-only state is not writable");
    pathInput->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(&window, Qt::Key_X);
    QTest::keyClick(&window, Qt::Key_Return);
    panel->forceActiveFocus();
    QTest::qWait(20);
    require(pathInput->property("text").toString() == savedPath && !pathAction->isEnabled(),
            "read-only text field accepted keyboard input or enabled its chooser");
    require(value("path") == savedPath && parameterChanges.count() == pathChanges,
            "read-only text field submitted a parameter");
    require(pathField->setProperty("readOnly", false), "text field read-only state could not be restored");
    pathInput->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(&window, Qt::Key_X);
    panel->forceActiveFocus();
    require(QTest::qWaitFor([&] { return value("path") == "x"; }, 1000),
            "editable text field did not commit real keyboard input on blur");
    require(parameterChanges.count() == pathChanges + 1 && pathAction->isEnabled(),
            "text blur submitted twice or the re-enabled chooser stayed disabled");
    require(app.setParameter("path", false, savedPath), "text state fixture could not restore its path");
    const auto comboBox = [](QQuickItem *row) -> QQuickItem *
    {
        for (auto *item : row->findChildren<QQuickItem *>())
            if (item->property("currentIndex").isValid() && item->property("model").isValid())
                return item;
        return nullptr;
    };
    for (const auto &id : {QStringLiteral("enabled"), QStringLiteral("mode")})
    {
        auto *row = findItem(findItem, panel, "parameter_" + id);
        require(row != nullptr, "parameter panel did not render a dropdown row");
        auto *combo = comboBox(row);
        require(combo != nullptr, "Bool/enum parameter did not use a dropdown");
        require(combo->height() == Theme::instance()->paramEditorHeight(), "dropdown height differs from numeric field");
        require(combo->property("font").value<QFont>().pixelSize() == Theme::instance()->paramEditorFontSize(),
                "dropdown font differs from numeric field");
        auto *label = findItem(findItem, row, "parameterLabel");
        auto *pathLabel = findItem(findItem, pathRow, "parameterLabel");
        auto *actions = findItem(findItem, row, "parameterActions");
        auto *pathActions = findItem(findItem, pathRow, "parameterActions");
        require(label && pathLabel && actions && pathActions, "parameter alignment anchors are missing");
        require(label->mapToItem(panel, QPointF()).x() == pathLabel->mapToItem(panel, QPointF()).x(),
                "parameter labels are not column aligned");
        require(actions->mapToItem(panel, QPointF()).x() == pathActions->mapToItem(panel, QPointF()).x(),
                "parameter actions are not column aligned");
        combo->forceActiveFocus();
        QTest::keyClick(&window, Qt::Key_Down);
        require(value(id) == (id == "enabled" ? "false" : "gray"), "dropdown keyboard selection did not commit its type");
        panel->setEnabled(false);
        QTest::keyClick(&window, Qt::Key_Up);
        require(value(id) == (id == "enabled" ? "false" : "gray"), "disabled dropdown changed its parameter");
        panel->setEnabled(true);
    }
    auto *labelsRow = findItem(findItem, panel, "parameter_labels");
    require(labelsRow != nullptr, "parameter panel did not render the structure row");
    auto *labelsInput = textInput(labelsRow);
    require(labelsInput != nullptr, "array parameter does not expose its editable input");
    labelsInput->forceActiveFocus();
    require(labelsInput->property("text").toString() == "[\"a\",\"b\"]", "array parameter initial text mismatch");
    const auto originalLabels = value("labels");
    require(app.save(project) && !app.dirty(), "cannot save array baseline");
    panel->setEnabled(false);
    labelsInput->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(&window, Qt::Key_X);
    QTest::keyClick(&window, Qt::Key_Return);
    require(value("labels") == originalLabels, "disabled array field still allowed editing");
    panel->setEnabled(true);
    labelsInput->forceActiveFocus();
    labelsInput->setProperty("text", "invalid structure");
    QTest::keyClick(&window, Qt::Key_Return);
    require(value("labels") == originalLabels, "invalid structure replaced a typed parameter");
    QTest::keyClick(&window, Qt::Key_Escape);
    require(labelsInput->property("text").toString() == originalLabels, "Escape did not restore array value");
    labelsInput->setProperty("text", "[\"中文\", \"with spaces\"]");
    QTest::keyClick(&window, Qt::Key_Return);
    const auto committedLabels = value("labels");
    require(committedLabels.contains(QStringLiteral("中文")) && committedLabels.contains("with spaces"),
            "array text input did not preserve typed string values");
    labelsInput->forceActiveFocus();
    labelsInput->setProperty("text", "unfinished structure");
    QTest::keyClick(&window, Qt::Key_Escape);
    require(labelsInput->property("text").toString() == committedLabels, "Escape did not restore array draft");
    require(value("labels") == committedLabels, "Escape committed an unfinished array draft");
    const auto click = [&](QQuickItem *item)
    {
        require(item && item->isVisible() && item->isEnabled(), "parameter action is unavailable");
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        QTest::qWait(120);
    };
    const auto visibleText = [&](const QString &text) -> QQuickItem *
    {
        QList<QQuickItem *> pending{window.contentItem()};
        while (!pending.isEmpty())
        {
            auto *item = pending.takeLast();
            if (item->isVisible() && item->property("text").toString() == text)
                return item;
            pending.append(item->childItems());
        }
        return nullptr;
    };
    require(app.setParameterMode("ratio", true), "parameter could not enter upstream mode");
    QTest::qWait(50);
    ratioRow = findItem(findItem, panel, "parameter_ratio");
    auto *upstream = findItem(findItem, ratioRow, "parameterUpstream");
    require(upstream != nullptr, "upstream parameter did not use the grouped dropdown");
    const auto candidates = app.bindingCandidates(app.portContext("ratio"));
    require(candidates.size() == 1, "upstream fixture should expose one compatible source");
    const auto candidate = candidates.first().toMap();
    click(upstream);
    auto *group = visibleText(candidate.value("title").toString());
    require(group != nullptr, "upstream dropdown did not expose its source node group");
    click(group);
    auto *output = visibleText(QStringLiteral("比例 (浮点数)"));
    require(output != nullptr, "upstream dropdown did not expose its typed source output");
    click(output);
    const auto wires = app.wires();
    require(wires.size() == 1 && wires.first().toMap().value("source").toString() == secondNode
                && wires.first().toMap().value("target").toString() == firstNode,
            "grouped dropdown did not bind the intended nodes");
    require(upstream->property("savedValue").toString().contains(secondNode),
            "grouped dropdown did not follow the committed binding");
    app.renameNode(secondNode, QStringLiteral("重命名的源"));
    QTest::qWait(30);
    require(upstream->property("displayText").toString().contains(QStringLiteral("重命名的源")),
            "bound dropdown did not follow the source display name refresh");
    click(upstream);
    click(visibleText(QStringLiteral("解除绑定")));
    require(app.wires().isEmpty(), "grouped dropdown did not remove the binding");
    require(value("ratio") == "0.35", "unbinding replaced the retained literal value");
    app.deleteNodes(QVariantList{secondNode});
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
    app.selectNode(firstNode);
    require(app.setParameterMode("ratio", true), "parameter could not return to upstream mode");
    QTest::qWait(30);
    ratioRow = findItem(findItem, panel, "parameter_ratio");
    upstream = findItem(findItem, ratioRow, "parameterUpstream");
    require(upstream != nullptr && app.bindingCandidates(app.portContext("ratio")).isEmpty(), "removed binding source remains available");
    require(app.save(project), "cannot save missing-source baseline");
    QSignalSpy missingSourceChanges(&app, &AppController::parametersChanged);
    click(upstream);
    // An empty candidate list stays empty: no placeholder row and no unbind action are invented.
    require(visibleText(QStringLiteral("无可用兼容输出")) == nullptr,
            "an empty candidate list invented a placeholder row");
    require(visibleText(QStringLiteral("解除绑定")) == nullptr,
            "an empty candidate list offered an unbind action");
    auto *missingPopup = upstream->property("popup").value<QObject *>();
    require(missingPopup && QMetaObject::invokeMethod(missingPopup, "close", Qt::DirectConnection),
            "cannot close the empty candidate group");
    require(app.wires().isEmpty() && value("ratio") == "0.35" && !app.dirty() && missingSourceChanges.isEmpty(),
            "opening an unavailable candidate list changed the project");
    require(app.setParameterMode("ratio", false), "integer round-trip fixture could not restore literal inputs");
    const auto parameterGroup = [&]() -> QQuickItem *
    {
        QList<QQuickItem *> pending{panel};
        while (!pending.isEmpty())
        {
            auto *item = pending.takeLast();
            if (item->property("headerText").toString() == QStringLiteral("输入参数")
                && item->property("expand").isValid()) return item;
            pending.append(item->childItems());
        }
        return nullptr;
    };
    panel->forceActiveFocus();
    QTest::qWait(20);
    const auto groupedParameters = app.parameters();
    const auto groupChanges = parameterChanges.count();
    auto *groupSection = parameterGroup();
    require(groupSection != nullptr, "parameter group is missing from the visual tree");
    require(groupSection->property("expand").toBool(), "parameter group is unexpectedly collapsed before header interaction");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      groupSection->mapToScene(QPointF(groupSection->width() / 2, Theme::instance()->paramGroupHeaderHeight() / 2)).toPoint());
    require(QTest::qWaitFor([&] { return groupSection->height() <= Theme::instance()->paramGroupHeaderHeight() + 1; }, 1000),
            "parameter group header did not collapse its content");
    require(!groupSection->property("expand").toBool(), "parameter group remained expanded after its header was clicked");
    panel->setProperty("currentTab", 1);
    QTest::qWait(30);
    panel->setProperty("currentTab", 0);
    QTest::qWait(30);
    groupSection = parameterGroup();
    require(groupSection != nullptr && !groupSection->property("expand").toBool(), "tab navigation lost the collapsed group state");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      groupSection->mapToScene(QPointF(groupSection->width() / 2, Theme::instance()->paramGroupHeaderHeight() / 2)).toPoint());
    require(QTest::qWaitFor([&] { return groupSection->height() > Theme::instance()->paramGroupHeaderHeight() + 1; }, 1000),
            "collapsed parameter group could not expand again");
    require(app.parameters() == groupedParameters && parameterChanges.count() == groupChanges,
            "group collapse or tab navigation changed parameter values");
    for (const auto &integer : {QStringLiteral("9007199254740993"), QStringLiteral("9223372036854775807"),
                                QStringLiteral("-9223372036854775808")})
    {
        QTest::qWait(30);
        auto *countRow = findItem(findItem, panel, "parameter_count");
        require(countRow != nullptr, "integer round-trip input is unavailable");
        auto *countInput = findItem(findItem, countRow, "stepperInput");
        require(countInput != nullptr, "integer round-trip field is missing");
        countInput->forceActiveFocus();
        countInput->setProperty("text", integer);
        QTest::keyClick(&window, Qt::Key_Return);
        require(value("count") == integer, "integer text commit lost precision");
        require(app.save(project), "cannot save integer boundary fixture");
        app.requestOpen(project);
        require(QTest::qWaitFor([&] { return !app.busy(); }, 3000), "integer boundary project did not reopen");
        app.selectNode(firstNode);
        require(value("count") == integer, "integer boundary changed during save/reopen");
        app.runNode();
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "integer boundary execution did not finish");
        QMap<QString, QString> boundaryResults;
        for (int row = 0; row < outputs->rowCount(); ++row)
        {
            const auto result = outputs->get(row);
            boundaryResults.insert(result.value("portId").toString(), result.value("preview").toString());
        }
        require(boundaryResults.value("__status_code") == "0" && boundaryResults.value("count") == integer,
                "integer boundary changed during Python execution");
    }
    window.hide();
    parameterStatesContract(app);
}
