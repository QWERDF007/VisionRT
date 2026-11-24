// Contract for the QML expression editor (T-R32-03 / T-R32-04).
// The editor is driven through its own scripted entry points so the checked behaviour is exactly
// what a user triggers: allocate rows, switch a row to Array, commit, delete the highest port and
// allocate again.
#include "TestPythonConfiguration.h"
#include "WorkbenchFixtures.h"
#include "ui/AppController.h"
#include "ui/OutputModel.h"
#include "ui/UiTypes.h"
#include <quickui/Color.h>

#include <QGuiApplication>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <iostream>
#include <memory>
#include <stdexcept>


using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

// ReadImage declares 图像数量 as input 5; an expression node allocates business
// outputs after the reserved status pair, so the first one is 2.
constexpr PortId kImageCountInput     = 5;
constexpr PortId kFirstExpressionPort = 2;
} // namespace

int main(int argc, char **argv)
try
{
    // R50: the host runs Python only on an interpreter someone selected.
    testsupport::useBuildInterpreter();
    QGuiApplication application(argc, argv);
    registerUiTypes();
    testing::WorkbenchInstallation installation;
    const auto                     root = installation.directory();

    AppController app(2, root);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 10000), "expression catalog did not become ready");
    app.addNode("EvaluateExpression", 0, 0);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 10000), "expression node did not synchronize");
    const auto nodeId = app.selectedNode();
    require(!nodeId.isEmpty(), "expression node is missing");
    require(app.nodeModel() == NodeModel::Expression, "expression node did not expose its node model");

    QQmlEngine engine;
    quickui::QuiColor::getInstance()->setThemeMode(quickui::QuiColor::Light);
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);
    QQmlComponent typeComponent(&engine,
        QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/SelectTypeEditor.qml")));
    std::unique_ptr<QObject> typeEditor(typeComponent.create());
    if (!typeEditor) throw std::runtime_error(typeComponent.errorString().toStdString());
    const auto dtypes = typeEditor->property("dtypeList").value<QJSValue>().toVariant().toList();
    QSet<QString> dtypeLabels;
    require(dtypes.size() == 7, "the array element type selector lost supported choices");
    for (const auto &entry : dtypes)
    {
        const auto label = entry.toMap().value("label").toString();
        require(!label.isEmpty() && !label.contains(QStringLiteral("位")), "the type selector still displays bit widths");
        dtypeLabels.insert(label);
    }
    require(dtypeLabels.size() == 7, "different array element types have indistinguishable choices");
    QQmlComponent component(&engine,
                            QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/ExpressionEditor.qml")));
    std::unique_ptr<QObject> editor(component.create());
    if (!editor)
        throw std::runtime_error(component.errorString().toStdString());

    const auto open = [&] { QMetaObject::invokeMethod(editor.get(), "openNode", Q_ARG(QVariant, nodeId)); };
    const auto add = [&]
    {
        const int before = editor->property("program").toList().size();
        QMetaObject::invokeMethod(editor.get(), "addProgram");
        require(editor->property("program").toList().size() == before + 1, "adding a program row failed");
    };
    const auto programRows = [&] { return editor->property("program").toList(); };
    const auto portOf = [&](int index)
    { return programRows().at(index).toMap().value("portId").toUInt(); };
    const auto edit = [&](int index, const char *key, const QVariant &value)
    {
        QMetaObject::invokeMethod(editor.get(), "updateProgram", Q_ARG(QVariant, index), Q_ARG(QVariant, QString::fromLatin1(key)),
                                  Q_ARG(QVariant, value));
    };
    const auto settle = [&] { require(QTest::qWaitFor([&] { return !app.busy(); }, 10000), "expression sync did not finish"); };
    const auto commit = [&]
    {
        QVariant applied;
        const bool invoked
            = QMetaObject::invokeMethod(editor.get(), "commit", Q_RETURN_ARG(QVariant, applied));
        settle();
        return invoked && applied.toBool();
    };

    // Switching a fresh row to Array must settle a valid shape in the same draft, otherwise the
    // displayed fallback and the committed configuration disagree.
    open();
    require(editor->property("nextSeq").toInt() == 2, "a fresh expression node must start after the host status ports");
    add();
    QMetaObject::invokeMethod(editor.get(), "changeType", Q_ARG(QVariant, 0), Q_ARG(QVariant, true),
                              Q_ARG(QVariant, static_cast<int>(Type::Array)));
    auto row = programRows().at(0).toMap();
    require(row.value("type").toInt() == static_cast<int>(Type::Array), "the row did not switch to Array");
    require(row.value("dtype").toInt() == static_cast<int>(DType::Float64) && row.value("rank").toInt() == 1,
            "switching to Array must write a legal dtype and rank into the draft");
    edit(0, "expression", QStringLiteral("[1.5, 2.5]"));
    require(commit(), "a row that only switched to Array must still be applicable");
    auto config = app.expressionConfig(nodeId);
    auto program = config.value("program").toList();
    require(program.size() == 1, "the committed program lost its row");
    auto committedRow = program.at(0).toMap();
    require(committedRow.value("type").toInt() == static_cast<int>(Type::Array) && committedRow.value("dtype").toInt() == static_cast<int>(DType::Float64)
                && committedRow.value("rank").toInt() == 1,
            "the committed configuration does not match the displayed type");
    require(config.value("nextPortId").toULongLong() == 3, "nextPortId must advance after committing port 2");

    // An explicitly empty draft is the user clearing the field: editing must open empty instead of
    // reloading the committed formula. Opening without a draft still shows the committed value.
    require(QMetaObject::invokeMethod(editor.get(), "openNodeWithDraft", Q_ARG(QVariant, nodeId),
                                      Q_ARG(QVariant, QString()), Q_ARG(QVariant, true)),
            "cannot open the editor with an explicit empty draft");
    require(programRows().size() == 1 && programRows().at(0).toMap().value("expression").toString().isEmpty(),
            "an explicit empty draft reloaded the committed formula");
    open();
    require(programRows().at(0).toMap().value("expression").toString() == "[1.5, 2.5]",
            "opening without a draft must load the committed formula");

    // Monotonic identifiers: deleting the highest committed port must not recycle its identifier.
    open();
    add();
    require(portOf(1) == 3u, "the second row did not allocate port 3");
    edit(1, "expression", QStringLiteral("[3.5]"));
    require(commit(), "the second row could not be committed");
    open();
    QMetaObject::invokeMethod(editor.get(), "removeProgram", Q_ARG(QVariant, 1));
    require(commit(), "removing the highest port could not be committed");
    config = app.expressionConfig(nodeId);
    require(config.value("program").toList().size() == 1, "the deleted port is still present");
    require(config.value("nextPortId").toULongLong() == 4,
            "committing after a deletion must keep the issued sequence");
    open();
    add();
    require(portOf(1) == 4u, "a retired port identifier was recycled");
    edit(1, "expression", QStringLiteral("[4.5]"));
    require(commit(), "the replacement row could not be committed");

    // Twenty rows stay operable: the lists live in a scroll container and the actions remain usable.
    open();
    while (programRows().size() < 20)
    {
        add();
        edit(programRows().size() - 1, "expression", QStringLiteral("[1.0]"));
        // Distinct symbols are required, the default rN already satisfies it.
    }
    require(programRows().size() == 20, "twenty rows could not be allocated");
    auto *scroller = editor->findChild<QObject *>(QStringLiteral("expressionScroller"));
    require(scroller != nullptr, "the formula list is not inside a scroll container");
    require(QString::fromLatin1(scroller->metaObject()->className()).contains(QStringLiteral("ScrollView")),
            "the formula list container is not a ScrollView");
    require(commit(), "twenty rows could not be applied");
    config = app.expressionConfig(nodeId);
    require(config.value("program").toList().size() == 20, "applying twenty rows lost entries");
    open();
    QMetaObject::invokeMethod(editor.get(), "removeProgram", Q_ARG(QVariant, 19));
    require(programRows().size() == 19, "deleting from a long list failed");
    QMetaObject::invokeMethod(editor.get(), "close");

    // A condition row selects and commits a dynamic Bool output through the real QML selector.
    app.addNode("EvaluateExpression", 8, 0);
    settle();
    const auto guardSource = app.selectedNode();
    require(!guardSource.isEmpty(), "guard source node is missing");
    const auto guardProgram = [](const QString &name)
    {
        return QVariantList{QVariantMap{{"portId", 2u}, {"name", name}, {"symbol", "flag"},
                                        {"expression", "True"}, {"type", static_cast<int>(Type::Bool)}, {"dtype", ""},
                                        {"rank", -1}, {"exposed", true}}};
    };
    require(app.applyExpressionConfig(guardSource, {}, guardProgram(QStringLiteral("布尔结果"))),
            "guard source program could not be applied");
    settle();

    app.addNode("ConditionalBranch", 8, 2);
    settle();
    const auto conditionNode = app.selectedNode();
    require(app.setConditionMode(conditionNode, true), "condition did not switch to upstream mode");
    settle();
    QVariantMap conditionRow;
    for (const auto &value : app.parameters())
        if (value.toMap().value("editor").toInt() == static_cast<int>(EditorKind::ConditionUpstream))
            conditionRow = value.toMap();
    QQmlComponent conditionComponent(
        &engine, QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/ParameterEditor.qml")));
    std::unique_ptr<QObject> conditionEditor(conditionComponent.createWithInitialProperties(
        {{"parameter", conditionRow}, {"nodeId", conditionNode}}));
    require(conditionEditor != nullptr, "condition parameter editor could not be created");
    QVariant bindingGroups;
    require(QMetaObject::invokeMethod(conditionEditor.get(), "buildBindingGroups", Q_RETURN_ARG(QVariant, bindingGroups)),
            "condition binding choices could not be opened");
    const auto guardValue = guardSource + ":" + QString::number(kFirstExpressionPort);
    bool guardListed = false;
    for (const auto &group : bindingGroups.toList())
        for (const auto &option : group.toMap().value("subOptions").toList())
            guardListed |= option.toMap().value("value").toString() == guardValue;
    require(guardListed, "the condition editor did not offer the upstream Bool output");
    auto *upstream = conditionEditor->findChild<QObject *>(QStringLiteral("parameterUpstream"));
    require(upstream != nullptr, "condition upstream selector is missing");
    QMetaObject::invokeMethod(upstream, "aboutToOpen");
    auto commitBinding = upstream->property("commitValue").value<QJSValue>();
    const auto bound = commitBinding.call({QJSValue(guardValue)});
    require(!bound.isError() && bound.toBool(),
            "the condition selector could not commit the upstream binding");
    settle();
    conditionEditor.reset();

    // A dynamic output connected to an ordinary downstream input must resolve its label from the
    // materialised contract on every refresh; the row may never keep a pointer into a temporary.
    app.addNode("EvaluateExpression", 9, 0);
    settle();
    const auto intSource = app.selectedNode();
    const auto intProgram = [](const QString &name)
    {
        return QVariantList{QVariantMap{{"portId", 2u}, {"name", name}, {"symbol", "n"},
                                        {"expression", "1"}, {"type", static_cast<int>(Type::Int)}, {"dtype", ""},
                                        {"rank", -1}, {"exposed", true}}};
    };
    require(app.applyExpressionConfig(intSource, {}, intProgram(QStringLiteral("计数结果"))),
            "dynamic Int program could not be applied");
    settle();
    app.selectNode(intSource);
    settle();
    auto *outputs = qobject_cast<OutputModel *>(app.outputModel());
    require(outputs != nullptr, "expression output model is null");
    const auto dynamicRow = [&]()
    {
        for (int index = 0; index < outputs->rowCount(); ++index)
        {
            const auto item = outputs->get(index);
            if (item.value("portId").toUInt() == 2u)
                return item;
        }
        return QVariantMap{};
    };
    const auto declared = dynamicRow();
    require(!declared.isEmpty(), "an expression result is missing from the output table");
    require(declared.value("name").toString() == QStringLiteral("计数结果"),
            "the output table did not use the materialised port name");
    require(declared.value("typeDisplay").toString() == QStringLiteral("整数"),
            "the output table did not use the materialised Chinese type");

    app.addNode("ReadImage", 9, 6);
    settle();
    const auto consumer = app.selectedNode();
    require(!consumer.isEmpty(), "downstream reader node is missing");
    app.selectNode(intSource);
    settle();
    require(dynamicRow().value("connectable").toBool(),
            "a free downstream input must mark the dynamic result as connectable");
    app.selectNode(consumer);
    settle();
    require(app.bindInput(app.portContext(kImageCountInput), intSource, kFirstExpressionPort),
            "dynamic Int output could not be bound to an ordinary input");
    settle();

    const auto sourceRow = [&]()
    {
        for (const auto &value : app.parameters())
            if (value.toMap().value("id").toUInt() == kImageCountInput)
                return value.toMap();
        return QVariantMap{};
    };
    for (int refresh = 0; refresh < 100; ++refresh)
    {
        const auto row = sourceRow();
        require(row.value("inputMode").toInt() == static_cast<int>(BindingMode::Upstream),
                "bound ordinary input lost its upstream mode");
        require(row.value("source").toString().contains(QStringLiteral("计数结果")),
                "ordinary binding lost the dynamic port name");
    }
    app.selectNode(intSource);
    require(app.applyExpressionConfig(intSource, {}, intProgram(QStringLiteral("计数结果改"))),
            "dynamic Int port rename failed");
    settle();
    app.selectNode(consumer);
    require(sourceRow().value("source").toString().contains(QStringLiteral("计数结果改")),
            "ordinary binding kept the old dynamic port name");

    std::cout << "WORKBENCH_EXPRESSION_PASS array=atomic uint8=1 chinese=1 nextPortId=monotonic rows=20 scroll=1 guard=dynamic\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << "[Expression editor error] " << error.what() << '\n';
    return 1;
}
