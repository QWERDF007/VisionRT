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
// The fixture declares 值 1 / 初始化值 1 on input scopes; its business outputs start at 2.
constexpr PortId kValueInput     = 1;
constexpr PortId kInitValueInput = 1;
constexpr PortId kValueOutput    = 2;
constexpr PortId kImageOutput    = 3;
constexpr PortId kRectOutput     = 4;
constexpr PortId kNoteOutput     = 5;

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
    definition.nameEn          = "test.publication";
    definition.nameZh = "发布工具";
    definition.kind        = ToolKind::PythonClass;
    definition.entry       = {"publication", "tool.py", "Tool"};
    definition.initParams = {
        {1, "初始化值", {Type::Int}, std::int64_t{10}}
    };
    definition.nextInitPortId = 2;
    definition.inputs = {
        {1, "值", {Type::Int}, std::int64_t{1}}
    };
    definition.nextInputPortId = 2;
    definition.outputs = {
        {2, "值", {Type::Int}},
        {3, "图像", {Type::Image}},
        {4, "区域", {Type::ROI}},
        {5, "说明", {Type::String}}
    };
    definition.nextOutputPortId = 6;
    write(package + "tools/custom/publication/source.json",
          QByteArray::fromStdString(emitNormalizedToolJson(definition)));
    write(package + "python/custom/publication/tool.py", R"py(import numpy as np

class Tool:
    def initialize(self, inputs):
        self.init = int(inputs.get(1, 10))
        return {'code': 0, 'message': '', 'detail': ''}

    def execute(self, inputs):
        value = int(inputs.get(1, 1))
        if value < 0:
            return {'outputs': {2: None, 3: None, 4: [], 5: '',
                                0: 1, 1: '受控发布失败'},
                    'detail': ''}
        image = np.full((4, 4), self.init + value, dtype=np.uint8)
        return {'outputs': {2: value, 3: [image],
                            4: [[[1.0, 2.0, 3.0, 4.0, 0.0]]], 5: f'{self.init}:{value}',
                            0: 0, 1: ''},
                'detail': ''}

    def release(self):
        return {'code': 0, 'message': '', 'detail': ''}
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
    require(app.setParameter(kInitValueInput, true, "10"), "publication init parameter could not be set");
    idle();
    require(app.setParameter(kValueInput, false, "1"), "publication value could not be set");
    idle();
    app.runNode();
    idle();
    auto *outputs = qobject_cast<OutputModel *>(app.outputModel());
    require(outputs != nullptr, "publication output model is unavailable");
    const auto firstValue = outputs->capture(kValueOutput);
    const auto firstImage = outputs->capture(kImageOutput);
    const auto firstRect  = outputs->capture(kRectOutput);
    const auto firstNote  = outputs->capture(kNoteOutput);
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
    require(app.setParameter(kValueInput, false, "2"), "publication value change was rejected");
    idle();
    require(outputs->capture(kValueOutput)->publication == firstPublication
                && std::get<std::int64_t>(outputs->capture(kValueOutput)->value) == 1,
            "changing a normal parameter replaced the published result");
    require(app.setParameter(kInitValueInput, true, "20"), "publication init change was rejected");
    idle();
    require(outputs->capture(kValueOutput)->publication == firstPublication
                && std::get<std::int64_t>(outputs->capture(kValueOutput)->value) == 1,
            "changing an init parameter replaced the published result");
    app.runNode();
    idle();
    const auto secondValue = outputs->capture(kValueOutput);
    const auto secondImage = outputs->capture(kImageOutput);
    const auto secondRect  = outputs->capture(kRectOutput);
    const auto secondNote  = outputs->capture(kNoteOutput);
    require(secondValue && secondImage && secondRect && secondNote, "publication rerun lost an output");
    require(secondValue->publication != firstPublication, "publication rerun reused the old publication");
    require(secondImage->publication == secondValue->publication && secondRect->publication == secondValue->publication
                && secondNote->publication == secondValue->publication,
            "publication rerun is not atomic");
    require(std::get<std::int64_t>(secondValue->value) == 2, "publication rerun has the wrong value");
    require(std::get<DataRef<Image>>(secondImage->value).get()[0].at<std::uint8_t>(0, 0) == 22,
            "publication rerun did not apply the changed init parameter");
    require(std::get<std::string>(secondNote->value) == "20:2", "publication rerun has the wrong note");
    require(app.setParameter(kValueInput, false, "-1"), "publication failure value was rejected");
    idle();
    app.runNode();
    idle();
    require(stateOf(node).value("state").toString() == "failed", "publication failure did not fail");
    require(stateOf(node).value("statusMessage").toString().contains("受控发布失败"),
            "publication failure lost its message");
    const auto failedValue = outputs->capture(kValueOutput);
    const auto failedImage = outputs->capture(kImageOutput);
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
