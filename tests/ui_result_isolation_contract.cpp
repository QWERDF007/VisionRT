#include "WorkbenchFixtures.h"
#include "core/DefinitionJson.h"
#include "core/ProjectEditor.h"
#include "core/ProjectStorage.h"
#include "core/ToolPackage.h"
#include "core/ToolRegistry.h"
#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include "ui/OutputModel.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTest>
#include <array>
#include <chrono>
#include <iostream>
#include <optional>
#include <thread>

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
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    testing::WorkbenchInstallation installation;
    const auto                     root    = installation.directory();
    const auto                     package = root + "/tools-package/";
    const auto                     write   = [&](const QString &path, const QByteArray &content)
    {
        require(QDir().mkpath(QFileInfo(path).absolutePath()), "isolation fixture parent unavailable");
        QFile file(path);
        require(file.open(QIODevice::WriteOnly) && file.write(content) == content.size(),
                "isolation fixture write failed");
    };
    ToolDefinition image;
    image.id          = "test.isolation.image";
    image.displayName = "隔离图像";
    image.kind        = ToolKind::PythonFunction;
    image.entry       = {"isolation", "tool.py", "source"};
    image.inputs      = {
        { "level",          "像素值",    {Type::Int}, std::int64_t{1}},
        {"marker", "存储释放标记", {Type::String},   std::string{}}
    };
    image.outputs = {
        {"images", "图像", {Type::Image}}
    };
    image.outputs.front().colorOrder = "BGR";
    const auto declaration           = package + "tools/custom/isolation/image.json";
    write(declaration, QByteArray::fromStdString(emitNormalizedToolJson(image)));
    auto scalar         = image;
    scalar.id           = "test.isolation.scalar";
    scalar.displayName  = "隔离数值";
    scalar.entry.symbol = "scalar";
    scalar.inputs.clear();
    scalar.outputs = {
        {"images", "同名数值", {Type::Int}}
    };
    write(package + "tools/custom/isolation/scalar.json", QByteArray::fromStdString(emitNormalizedToolJson(scalar)));
    write(package + "python/custom/isolation/tool.py", R"py(import numpy as np
import weakref
from pathlib import Path

def retired(marker):
    if marker:
        Path(marker).write_text('released')

def source(inputs):
    level = inputs['level']
    first = np.full((120, 160, 3), (0, 0, level), dtype=np.uint8)
    second = np.full((120, 160, 3), (0, level, 0), dtype=np.uint8)
    weakref.finalize(first, retired, inputs['marker'])
    return {'status': {'code': 0, 'message': '', 'detail': ''}, 'outputs': {'images': [first, second]}}

def scalar(inputs):
    return {'status': {'code': 0, 'message': '', 'detail': ''}, 'outputs': {'images': 84}}
)py");
    ToolRegistry definitions;
    definitions.registerPackage(readToolsPackage(std::filesystem::path(package.toStdU16String())));
    std::optional<NodeId> imageId;
    NodeId                scalarId = 0;
    for (const auto &[name, level] : std::array<std::pair<const char *, std::int64_t>, 2>{
             {{"first", 201}, {"second", 97}}
    })
    {
        ProjectEditor editor;
        const auto    flow   = editor.project().flows.front().id;
        const auto    parent = editor.addNode(flow, *definitions.find(image.id), {0, 0});
        const auto    other  = editor.addNode(flow, *definitions.find(scalar.id), {2, 0});
        require(!imageId || *imageId == parent, "saved projects do not reuse the same image NodeId");
        imageId  = parent;
        scalarId = other;
        editor.setLiteral(flow, parent, "level", level);
        editor.setLiteral(flow, parent, "marker", (root + "/" + name + ".released").toStdString());
        auto project = editor.project();
        project.path = std::filesystem::path((root + "/" + name + ".vrt.json").toStdU16String());
        require(ProjectStorage(definitions).save(project).empty(), "cannot save isolation project");
    }
    bool forbidImages        = false;
    bool staleFramePresented = false;
    QQuickWindow window;
    window.setColor(QColor(17, 23, 31));
    window.resize(480, 240);
    ImageViewport mainView(window.contentItem());
    mainView.setSize({320, 240});
    ImageViewport thumbnail(window.contentItem());
    thumbnail.setX(320);
    thumbnail.setSize({160, 240});
    thumbnail.setProperty("thumbnailIndex", 1);
    window.show();
    require(QTest::qWaitForWindowExposed(&window), "isolation viewport window was not exposed");
    AppController app(2, root);
    const auto    idle = [&]
    {
        require(QTest::qWaitFor([&] { return !app.busy(); }, 10000), "isolation operation did not finish");
    };
    idle();
    app.attachViewport(&mainView);
    app.attachViewport(&thumbnail);
    auto *outputs = qobject_cast<OutputModel *>(app.outputModel());
    require(outputs, "isolation output model unavailable");
    const auto imageNode  = QString::number(*imageId);
    const auto scalarNode = QString::number(scalarId);
    const auto open       = [&](const char *name)
    {
        app.requestOpen(root + "/" + name + ".vrt.json");
        app.resolveUnsaved("discard");
        idle();
        require(app.nodes().size() == 2, "isolation project did not load");
        app.selectNode(imageNode);
    };
    const auto frame = [&](QColor main, QColor preview)
    {
        QImage rendered;
        require(QTest::qWaitFor(
                    [&]
                    {
                        rendered = window.grabWindow();
                        return !rendered.isNull()
                            && rendered.pixelColor(rendered.width() / 3, rendered.height() / 2) == main
                            && rendered.pixelColor(rendered.width() * 5 / 6, rendered.height() / 2) == preview;
                    },
                    5000),
                "rendered main image or thumbnail belongs to another target");
        return rendered;
    };
    open("first");
    app.runNode();
    idle();
    frame(QColor(201, 0, 0), QColor(0, 201, 0));
    const auto oldContext = app.portContext("images", true);
    require(!oldContext.isEmpty(), "first output context unavailable");
    open("second");
    const auto fresh = outputs->capture("images");
    require(fresh && isMissing(fresh->value) && fresh->type.type == Type::Image,
            "new project reused another project's cached Image output");
    require(!mainView.hasImage() && !thumbnail.hasImage() && !app.portContextCurrent(oldContext),
            "project replacement retained an old frame or output context");
    QSignalSpy text(&app, &AppController::outputTextReady);
    app.requestOutputText(oldContext, false, false);
    QTest::qWait(20);
    require(text.isEmpty(), "old output action read a reused NodeId in another project");
    app.runNode();
    idle();
    frame(QColor(97, 0, 0), QColor(0, 97, 0));
    for (int change = 0; change < 40; ++change)
    {
        app.setImageIndex(change % 2);
        app.selectNode(scalarNode);
        app.selectNode(imageNode);
    }
    app.setImageIndex(1);
    frame(QColor(0, 97, 0), QColor(0, 97, 0));
    app.setImageIndex(0);
    frame(QColor(97, 0, 0), QColor(0, 97, 0));
    std::cout << "Project isolation: reused_node=" << *imageId
              << " old=201 new=97 stale_context=rejected rapid_switches=40\n";
    QObject::connect(&mainView, &ImageViewport::imageChanged, &application,
                     [&] { staleFramePresented = staleFramePresented || (forbidImages && mainView.hasImage()); });
    QObject::connect(&thumbnail, &ImageViewport::imageChanged, &application,
                     [&] { staleFramePresented = staleFramePresented || (forbidImages && thumbnail.hasImage()); });
    enum class Transition
    {
        Delete,
        Project,
        Selection
    };
    for (const auto transition : {Transition::Delete, Transition::Project, Transition::Selection})
    {
        open("second");
        const auto marker = root + "/second.released";
        require(!QFile::exists(marker) || QFile::remove(marker), "cannot reset pixel lifetime marker");
        app.runNode();
        idle();
        frame(QColor(97, 0, 0), QColor(0, 97, 0));
        QSignalSpy presented(&mainView, &ImageViewport::imagePresented);
        app.selectNode(scalarNode);
        presented.clear();
        app.selectNode(imageNode);
        require(QTest::qWaitFor([&] { return !presented.isEmpty() && mainView.hasImage(); }, 5000),
                "fresh display request did not reach the main viewport");
        const auto publication = outputs->capture("images");
        require(publication && std::holds_alternative<DataRef<Image>>(publication->value),
                "late-frame source publication unavailable");
        const auto context = app.portContext("images", true);
        app.selectNode(scalarNode);
        frame(QColor(17, 23, 31), QColor(17, 23, 31));
        forbidImages = true;
        app.selectNode(imageNode);
        std::get<DataRef<Image>>(publication->value).get().clear();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!QFile::exists(marker) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(QFile::exists(marker), "converter did not finish reading the tool-owned pixels");
        require(!mainView.hasImage() && !thumbnail.hasImage(), "queued frame was delivered before transition");
        if (transition == Transition::Delete)
        {
            app.deleteNodes({imageNode});
            idle();
            require(app.nodes().size() == 1 && outputs->rowCount() == 0,
                    "late completion recreated a deleted node or its output rows");
        }
        else if (transition == Transition::Project)
        {
            open("first");
            const auto replacement = outputs->capture("images");
            require(replacement && isMissing(replacement->value), "late completion populated a reused NodeId");
        }
        else
        {
            app.selectNode(scalarNode);
            const auto replacement = outputs->capture("images");
            require(replacement && replacement->type.type == Type::Int && isMissing(replacement->value),
                    "late completion populated the newly selected scalar node");
        }
        QTest::qWait(20);
        frame(QColor(17, 23, 31), QColor(17, 23, 31));
        require(
            !staleFramePresented && !mainView.hasImage() && !thumbnail.hasImage() && !app.portContextCurrent(context),
            "late main or thumbnail completion revived a retired publication");
        forbidImages    = false;
        const auto name = transition == Transition::Delete  ? "delete"
                        : transition == Transition::Project ? "project"
                                                            : "selection";
        std::cout << "Late display isolation: transition=" << name
                  << " pixel_owner=released queued_frames=rejected reused_node=" << *imageId << '\n';
    }
    open("first");
    app.runNode();
    idle();
    frame(QColor(201, 0, 0), QColor(0, 201, 0));
    const auto previousImageContext = app.portContext("images", true);
    image.entry.symbol              = "scalar";
    image.outputs.front().type      = {Type::Int};
    image.outputs.front().colorOrder.clear();
    write(declaration, QByteArray::fromStdString(emitNormalizedToolJson(image)));
    app.rescanTools();
    idle();
    app.selectNode(imageNode);
    {
        const auto changed = outputs->capture("images");
        require(changed && changed->type.type == Type::Int && isMissing(changed->value),
                "same-tool output schema change kept an incompatible Image publication");
    }
    require(app.imageOutputs().isEmpty() && app.imageCount() == 0 && !mainView.hasImage() && !thumbnail.hasImage()
                && !app.portContextCurrent(previousImageContext),
            "schema change retained old image ports, frames or context");
    frame(QColor(17, 23, 31), QColor(17, 23, 31));
    app.runNode();
    idle();
    {
        const auto changed = outputs->capture("images");
        require(changed && std::get<std::int64_t>(changed->value) == 84,
                "changed same-tool implementation did not publish its declared Int value");
    }
    open("first");
    {
        const auto reopened = outputs->capture("images");
        require(reopened && reopened->type.type == Type::Int && isMissing(reopened->value),
                "opening an old Image type_snapshot reused a cached Int result or stale schema");
    }
    require(!mainView.hasImage() && !thumbnail.hasImage(), "old project type_snapshot revived image frames");
    app.runNode();
    idle();
    {
        const auto reopened = outputs->capture("images");
        require(reopened && std::get<std::int64_t>(reopened->value) == 84,
                "reconciled project could not execute the changed output schema");
    }
    std::cout << "Output schema isolation: same_tool=test.isolation.image reused_node=" << *imageId
              << " Image->Int old_snapshot=rejected scalar=84\n";
    window.hide();
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
