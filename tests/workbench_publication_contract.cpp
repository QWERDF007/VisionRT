#include "WorkbenchFixtures.h"
#include "core/DefinitionJson.h"
#include "ui/AppController.h"
#include "ui/OutputModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
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
}

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    testing::WorkbenchInstallation installation;
    const auto                     root  = installation.directory();
    const auto                     write = [&](const QString &path, const QByteArray &bytes)
    {
        require(QDir().mkpath(QFileInfo(path).absolutePath()), "publication fixture parent unavailable");
        QFile file(path);
        require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "publication fixture write failed");
    };
    const auto package = root + "/tools-package/";
    ToolDefinition definition;
    definition.id          = "test.publication";
    definition.displayName = "发布工具";
    definition.kind        = ToolKind::PythonClass;
    definition.entry       = {"publication", "tool.py", "Tool"};
    definition.initParams  = {
        { "init_value", "初始化值", {Type::Int}, std::int64_t{10}}
    };
    definition.inputs = {
        { "value", "值", {Type::Int}, std::int64_t{1}}
    };
    definition.outputs = {
        { "value", "值", {Type::Int}},
        { "image", "图像", {Type::Image}},
        { "rect", "区域", {Type::ROI}},
        { "note", "说明", {Type::String}}
    };
    write(package + "tools/custom/publication/source.json",
          QByteArray::fromStdString(emitNormalizedToolJson(definition)));
    write(package + "python/custom/publication/tool.py", R"py(import numpy as np

class Tool:
    def initialize(self, inputs):
        self.init = int(inputs.get('init_value', 10))
        return {'__status_code': 0, '__status_message': ''}

    def execute(self, inputs):
        value = int(inputs.get('value', 1))
        if value < 0:
            return {'__status_code': 1, '__status_message': '受控发布失败',
                    'value': None, 'image': None, 'rect': [], 'note': ''}
        image = np.full((4, 4), self.init + value, dtype=np.uint8)
        return {'value': value, 'image': [image],
                'rect': [[[1.0, 2.0, 3.0, 4.0, 0.0]]], 'note': f'{self.init}:{value}',
                '__status_code': 0, '__status_message': ''}

    def release(self):
        return {'__status_code': 0, '__status_message': ''}
)py");
    AppController app(1, root);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "publication catalog did not become ready");
    const auto idle = [&]
    {
        require(QTest::qWaitFor([&] { return !app.busy(); }, 10000), "publication operation did not finish");
    };
    const auto stateOf = [&](const QString &id)
    {
        for (const auto &value : app.nodes())
        {
            const auto row = value.toMap();
            if (row.value("id").toString() == id)
                return row;
        }
        return QVariantMap{};
    };
    app.addNode("test.publication", 0, 0);
    idle();
    const auto node = app.selectedNode();
    require(!node.isEmpty(), "publication node is missing");
    require(app.setParameter("init_value", true, "10"), "publication init parameter could not be set");
    idle();
    require(app.setParameter("value", false, "1"), "publication value could not be set");
    idle();
    app.runNode();
    idle();
    auto *outputs = qobject_cast<OutputModel *>(app.outputModel());
    require(outputs != nullptr, "publication output model is unavailable");
    const auto firstValue = outputs->capture(QStringLiteral("value"));
    const auto firstImage = outputs->capture(QStringLiteral("image"));
    const auto firstRect  = outputs->capture(QStringLiteral("rect"));
    const auto firstNote  = outputs->capture(QStringLiteral("note"));
    require(firstValue && firstImage && firstRect && firstNote, "publication success lost an output");
    require(std::get<std::int64_t>(firstValue->value) == 1, "publication success has the wrong value");
    require(std::get<DataRef<Image>>(firstImage->value).get()[0].at<std::uint8_t>(0, 0) == 11,
            "publication image does not carry the initialized value");
    require(std::get<DataRef<ROI>>(firstRect->value).get()[0][0][1] == 2.0, "publication ROI is malformed");
    require(std::get<std::string>(firstNote->value) == "10:1", "publication note is malformed");
    require(!stateOf(node).value("timingText").toString().isEmpty(), "publication success has no timing");
    const auto firstPublication = firstValue->publication;
    require(firstImage->publication == firstPublication && firstRect->publication == firstPublication
                && firstNote->publication == firstPublication,
            "publication outputs are not atomic");
    require(app.setParameter("value", false, "2"), "publication value change was rejected");
    idle();
    require(outputs->capture(QStringLiteral("value"))->publication == firstPublication
                && std::get<std::int64_t>(outputs->capture(QStringLiteral("value"))->value) == 1,
            "changing a normal parameter replaced the published result");
    require(app.setParameter("init_value", true, "20"), "publication init change was rejected");
    idle();
    require(outputs->capture(QStringLiteral("value"))->publication == firstPublication
                && std::get<std::int64_t>(outputs->capture(QStringLiteral("value"))->value) == 1,
            "changing an init parameter replaced the published result");
    app.runNode();
    idle();
    const auto secondValue = outputs->capture(QStringLiteral("value"));
    const auto secondImage = outputs->capture(QStringLiteral("image"));
    const auto secondRect  = outputs->capture(QStringLiteral("rect"));
    const auto secondNote  = outputs->capture(QStringLiteral("note"));
    require(secondValue && secondImage && secondRect && secondNote, "publication rerun lost an output");
    require(secondValue->publication != firstPublication, "publication rerun reused the old publication");
    require(secondImage->publication == secondValue->publication && secondRect->publication == secondValue->publication
                && secondNote->publication == secondValue->publication,
            "publication rerun is not atomic");
    require(std::get<std::int64_t>(secondValue->value) == 2, "publication rerun has the wrong value");
    require(std::get<DataRef<Image>>(secondImage->value).get()[0].at<std::uint8_t>(0, 0) == 22,
            "publication rerun did not apply the changed init parameter");
    require(std::get<std::string>(secondNote->value) == "20:2", "publication rerun has the wrong note");
    require(app.setParameter("value", false, "-1"), "publication failure value was rejected");
    idle();
    app.runNode();
    idle();
    require(stateOf(node).value("state").toString() == "failed", "publication failure did not fail");
    require(stateOf(node).value("statusMessage").toString().contains("受控发布失败"),
            "publication failure lost its message");
    const auto failedValue = outputs->capture(QStringLiteral("value"));
    const auto failedImage = outputs->capture(QStringLiteral("image"));
    require(failedValue && failedValue->publication > secondValue->publication
                && std::holds_alternative<Missing>(failedValue->value),
            "publication failure retained the old success value");
    require(failedImage && failedImage->publication == failedValue->publication
                && std::holds_alternative<Missing>(failedImage->value),
            "publication failure retained the old success image");
    std::cout << "WORKBENCH_PUBLICATION_PASS keep=1 replace=1 atomic=1 fail=1 timing=1\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
