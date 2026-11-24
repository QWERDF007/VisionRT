#include "WorkbenchFixtures.h"
#include "ui/AppController.h"
#include "ui/OutputModel.h"
#include "ui/UiTypes.h"

#include <QFile>
#include <QGuiApplication>
#include <QQuickStyle>
#include <QTest>
#include <iostream>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

const NodeDefinition &node(const AppController &app, NodeId id)
{
    for (const auto &flow : app.project().flows)
        for (const auto &candidate : flow.nodes)
            if (candidate.id == id) return candidate;
    throw std::runtime_error("node is absent");
}
}

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    QQuickStyle::setStyle("Basic");
    registerUiTypes();
    testing::WorkbenchInstallation installation;
    const auto source = testing::copyCableExample(VISIONRT_SOURCE_DIR, installation.directory());
    AppController app(2, installation.directory());
    const auto settle = [&]
    {
        require(QTest::qWaitFor([&] { return app.workspaceEnabled(); }, 15000), "operation did not settle");
        QTest::qWait(100);
        require(QTest::qWaitFor([&] { return app.workspaceEnabled(); }, 15000), "synchronize did not settle");
    };
    settle();
    app.requestOpen(source);
    app.resolveUnsaved(UnsavedDecision::Discard);
    settle();
    const auto saved = installation.directory() + "/working/project.json";
    require(app.save(saved), "example and model assets could not be saved as a working project");
    settle();
    app.runOnce();
    settle();
    app.selectNode("2");
    settle();
    auto *outputs = qobject_cast<OutputModel *>(app.outputModel());
    require(outputs && outputs->capture(PortId{2}) && !isMissing(outputs->capture(PortId{2})->value),
            "the real edge model did not execute");
    const auto runRegion = node(app, 2).inputs.at(6).literal;
    const auto runContrast = node(app, 2).inputs.at(2).literal;
    app.setTrainingMode(true);
    require(app.trainingSavable(), "published reference draft did not load");
    require(app.setTrainingParameter(1, ""), "default model destination could not be selected");
    require(app.setTrainingParameter(3, "31.0"), "training contrast could not be edited");
    app.applyTraining();
    settle();
    require(app.outputTitle() == QStringLiteral("训练输出") && !app.trainingDirty(), "training was not published");
    require(isMissing(outputs->capture(PortId{2})->value), "training retained the old alignment matrix");
    const auto fit = outputs->capture(PortId{3})->value;
    const auto &edges = std::get<DataRef<ROI>>(fit).get();
    require(edges.size() == 1 && edges.front().size() == 2 && edges.front().front().size() == 5,
            "training did not publish complete measured segment endpoints");
    require(equalValues(node(app, 2).inputs.at(6).literal, runRegion)
                && equalValues(node(app, 2).inputs.at(2).literal, runContrast), "training changed detection parameters");
    const auto root = std::filesystem::path(saved.toStdWString()).parent_path();
    const auto defaultPath = std::filesystem::path(QString::fromStdString(std::get<std::string>(node(app, 2).trainParams.at(1))).toStdWString());
    require(defaultPath.is_relative() && std::filesystem::is_regular_file(root / defaultPath),
            "default training did not save a project-relative model");
    app.setTrainingMode(false);
    require(app.outputTitle() == QStringLiteral("训练输出") && equalValues(outputs->capture(PortId{3})->value, fit),
            "switching image modes restored a historical run result");
    app.runNode();
    settle();
    require(app.outputTitle() == QStringLiteral("运行输出") && !isMissing(outputs->capture(PortId{2})->value),
            "execution did not replace the training result");
    app.selectNode("3");
    settle();
    app.setTrainingMode(true);
    require(app.setTrainingParameter(3, "models/chosen-color.bin"), "custom model destination could not be set");
    app.applyTraining();
    settle();
    require(!app.trainingDirty() && std::filesystem::is_regular_file(root / "models/chosen-color.bin"),
            "color training did not save the requested relative model path");
    require(isMissing(outputs->capture(PortId{4})->value), "color training retained old detection labels");
    const auto previousAsset = node(app, 3).trainingAsset;
    const auto previousModel = node(app, 3).trainParams.at(3);
    QFile blocked(QString::fromStdWString((root / "blocked").wstring()));
    require(blocked.open(QIODevice::WriteOnly) && blocked.write("file") == 4, "save failure fixture failed");
    blocked.close();
    require(app.setTrainingParameter(3, "blocked/model.bin"), "failure destination could not be selected");
    app.applyTraining();
    settle();
    require(node(app, 3).trainingAsset == previousAsset && equalValues(node(app, 3).trainParams.at(3), previousModel),
            "a real save failure replaced the running model reference");
    require(app.trainingDirty() && std::get<std::int64_t>(outputs->capture(PortId{0})->value) != 0
                && isMissing(outputs->capture(PortId{9})->value), "model save failure was not reported through the shared outputs");
    require(app.setTrainingParameter(3, QString::fromStdString(std::get<std::string>(previousModel))),
            "previous model destination could not be restored");
    app.setTrainingMode(false);
    app.runNode();
    settle();
    require(std::get<std::int64_t>(outputs->capture(PortId{0})->value) == 0
                && !isMissing(outputs->capture(PortId{4})->value), "previous color model did not remain usable");
    require(app.save(), "trained project could not be saved");
    app.requestOpen(saved);
    app.resolveUnsaved(UnsavedDecision::Discard);
    settle();
    app.runOnce();
    settle();
    app.selectNode("3");
    settle();
    require(std::get<std::int64_t>(outputs->capture(PortId{0})->value) == 0,
            "saved project could not reload and execute both trained models");
    std::cout << "Real edge and color training save, reload, replace shared results, and preserve models on save failure\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
