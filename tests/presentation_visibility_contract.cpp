// SET-VIS-01: an ROI session edits its declared input as the canvas background. That internal read
// is not an external selection, so a port the author hid never appears in the image selector and is
// never reported as the image the user picked for results or export. The presentation owns that
// decision, which is why this runs without a window: QML only renders the list it is given.

#include "core/ProjectEditor.h"
#include "ui/OutputModel.h"
#include "ui/ResultPresentation.h"
#include "ui/RoiEditSession.h"

#include <QCoreApplication>

#include <iostream>
#include <map>
#include <stdexcept>

using namespace visionrt;

namespace
{

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

PortDefinition imagePort(PortId id, std::string name, bool visible)
{
    PortDefinition port;
    port.id           = id;
    port.nameZh       = std::move(name);
    port.type         = TypeSpec{Type::Image};
    port.imageVisible = visible;
    return port;
}

// An ROI input states which declared image it edits; the session's background is that image.
PortDefinition roiPort(PortId id, std::string name, PortId image)
{
    PortDefinition port;
    port.id     = id;
    port.nameZh = std::move(name);
    port.type   = TypeSpec{Type::ROI};
    port.image  = PortRef{PortStage::Inputs, image};
    return port;
}

// The presentation reads only these facts from the controller; everything else is an effect this
// contract does not exercise.
struct FakeHost final : PresentationHost
{
    const NodeDefinition *node = nullptr;
    const FlowDefinition *flow = nullptr;
    const ToolDefinition *tool = nullptr;

    const NodeDefinition *selectedNode() const override { return node; }
    const FlowDefinition *currentFlow() const override { return flow; }
    const ToolDefinition *effectiveTool() const override { return tool; }

    const PortDefinition *parameter(PortId id, bool init) const override
    {
        if (!tool || init)
            return nullptr;
        for (const auto &port : tool->inputs)
            if (port.id == id)
                return &port;
        return nullptr;
    }

    const std::map<PortId, bool> *refreshConnectable(const FlowDefinition &, const NodeDefinition &) const override
    {
        return nullptr;
    }
    bool        runtimeIdle() const override { return true; }
    bool        closing() const override { return false; }
    bool        contextCurrent(const QVariantMap &) const override { return true; }
    QVariantMap portContext(PortId) const override { return {}; }
    void        deliverText(const QString &) override {}
    void        resultsChanged() override {}
    void        runtimeChanged() override {}
    void        endRoiEdit() override {}
    void        reportDiagnostic(const ApplicationDiagnostic &) override {}
    void        reportFailure(const std::exception &, const QString &) override {}
};

} // namespace

int main(int argc, char **argv)
try
{
    QCoreApplication application(argc, argv);

    // Two hidden inputs and one visible output, exactly the shape the specification names.
    ToolDefinition tool;
    tool.nameEn  = "test.visibility";
    tool.nameZh  = "可见性测试";
    tool.kind    = ToolKind::PythonFunction;
    tool.inputs  = {imagePort(1, "隐藏图像一", false), imagePort(2, "隐藏图像二", false), roiPort(3, "编辑区域", 1)};
    tool.outputs = {imagePort(2, "结果图像", true)};

    NodeDefinition node;
    node.id     = 1;
    node.toolId = tool.nameEn;
    node.signatures.inputs  = {{1, TypeSpec{Type::Image}}, {2, TypeSpec{Type::Image}}, {3, TypeSpec{Type::ROI}}};
    node.signatures.outputs = {{2, TypeSpec{Type::Image}}};

    FlowDefinition flow;
    flow.id    = "flow_1";
    flow.nodes = {node};

    FakeHost host;
    host.node = &node;
    host.flow = &flow;
    host.tool = &tool;

    ProjectEditor  editor;
    RoiEditSession session(editor);
    OutputModel    outputs;
    ResultPresentation presentation(host, outputs, session, nullptr);

    // Nothing is offered while every image input is hidden: the selector lists the visible output only.
    require(presentation.imageOutputs().size() == 1,
            "hidden images entered the selector without an ROI session");
    require(presentation.selectedImageOutput().isEmpty(), "an image was selected with nothing open");

    // The session edits input 3, whose background is the hidden image 1. Editing it is legitimate;
    // reporting it as the image the user selected is not.
    RoiEditSession::Start start;
    start.node  = node.id;
    start.input = 3;
    start.flow  = QStringLiteral("flow_1");
    start.type  = TypeSpec{Type::ROI};
    session.begin(start);
    require(session.active() && session.input() == 3, "the ROI session did not start on its declared input");
    require(presentation.selectedImageOutput().isEmpty(),
            "a hidden ROI background was reported as the selected image");
    require(presentation.imageOutputs().size() == 1, "a hidden ROI background was listed as a candidate");

    // The author publishes the edited image: selector and reported image agree again.
    tool.inputs[0].imageVisible = true;
    const auto selected         = presentation.selectedImageOutput();
    require(selected == QStringLiteral("inputs:1"), "a published ROI background was not reported: " + selected.toStdString());
    const auto choices = presentation.imageOutputs();
    require(choices.size() == 2, "the published ROI background did not join the selector");
    bool listed = false;
    for (const auto &choice : choices)
        listed = listed || choice.toMap().value(QStringLiteral("id")).toString() == selected;
    require(listed, "the reported image is absent from the selector it came from");

    std::cout << "Hidden ROI backgrounds stay internal; published ones join the selector\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
