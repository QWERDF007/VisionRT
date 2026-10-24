#include "core/ProjectEditor.h"
#include "core/ProjectStorage.h"
#include "core/ToolPackage.h"
#include "core/ToolRegistry.h"
#include "ui/AppController.h"
#include "ui/Theme.h"
#include "WorkbenchFixtures.h"

#include <quickui/Color.h>
#include <QCommandLineParser>
#include <QFile>
#include <QGuiApplication>
#include <QJSValue>
#include <QLineF>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace visionrt;
using Clock = std::chrono::steady_clock;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

QQuickItem *findItem(QQuickItem *parent, const QString &name)
{
    for (auto *child : parent->childItems())
    {
        if (child->objectName() == name)
            return child;
        if (auto *found = findItem(child, name))
            return found;
    }
    return nullptr;
}

QVariantList list(const QVariant &value)
{
    return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant().toList()
                                                               : value.toList();
}

double percentile(std::vector<double> samples, double fraction)
{
    std::sort(samples.begin(), samples.end());
    const auto position = fraction * static_cast<double>(samples.size() - 1);
    const auto lower    = static_cast<std::size_t>(position);
    const auto upper    = std::min(lower + 1, samples.size() - 1);
    return samples[lower] + (samples[upper] - samples[lower]) * (position - lower);
}
}

int main(int argc, char **argv)
try
{
    QGuiApplication    application(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"full", "Run 20/50/100-node sparse/dense graphs with 21 samples each."});
    parser.addOption({"nodes", "Measure one graph size instead of the default 20 nodes.", "count"});
    parser.addOption({"samples", "Override the number of drag samples per graph.", "count"});
    parser.addOption(
        {"qml", "Canvas source to measure; imports resolve from the production source directory.", "path"});
    parser.addOption({"output", "Raw CSV output; project fixtures are saved beside it.", "path"});
    parser.process(application);
    bool      nodeCountValid   = true;
    bool      sampleCountValid = true;
    const int nodeCount        = parser.isSet("nodes") ? parser.value("nodes").toInt(&nodeCountValid) : 20;
    const int samples
        = parser.isSet("samples") ? parser.value("samples").toInt(&sampleCountValid) : (parser.isSet("full") ? 21 : 3);
    require(nodeCountValid && nodeCount > 1 && sampleCountValid && samples > 0,
            "Graph size must exceed one and sample count must be positive");
    require(!parser.isSet("full") || !parser.isSet("nodes"), "--full and --nodes are mutually exclusive");
    const auto destination = parser.value("output");
    require(!destination.isEmpty(), "--output is required");
    const auto output = std::filesystem::absolute(std::filesystem::path(destination.toStdU16String()));
    std::filesystem::create_directories(output.parent_path());
    std::ofstream raw(output);
    auto          summaryPath = output;
    summaryPath += ".summary.csv";
    std::ofstream summary(summaryPath);
    require(raw.good() && summary.good(), "Cannot create routing evidence");
    raw << "nodes,density,edges,sample,publications,burst_ms,release_ms,frame_ms,total_ms\n";
    summary << "nodes,density,edges,samples,publications,p50_ms,p95_ms\n";

    testing::WorkbenchInstallation installation;
    ToolRegistry                   registry;
    registry.registerPackage(
        readToolsPackage(std::filesystem::path(installation.directory().toStdU16String()) / "tools-package"));
    const auto *definition = registry.find("visionrt.detection.filter");
    require(definition != nullptr, "Installed filter declaration is missing");
    AppController app(1, installation.directory());
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "Catalog initialization deadline exceeded");
    QQmlEngine engine;
    quickui::QuiColor::getInstance()->setThemeMode(quickui::QuiColor::Light);
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);
    engine.rootContext()->setContextProperty("Theme", Theme::instance());
    QQmlComponent component(&engine);
    const auto    sourceUrl = QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/FlowCanvas.qml"));
    if (parser.isSet("qml"))
    {
        QFile source(parser.value("qml"));
        require(source.open(QIODevice::ReadOnly), "Cannot read measured QML source");
        component.setData(source.readAll(), sourceUrl);
    }
    else
        component.loadUrl(sourceUrl);
    std::unique_ptr<QObject> owner(component.create());
    if (!owner)
        throw std::runtime_error(component.errorString().toStdString());
    auto *canvas = qobject_cast<QQuickItem *>(owner.get());
    require(canvas != nullptr, "Canvas root is missing");
    QQuickWindow window;
    window.resize(1100, 760);
    canvas->setParentItem(window.contentItem());
    canvas->setSize(window.size());
    window.show();
    require(QTest::qWaitForWindowExposed(&window), "Benchmark window was not exposed");
    auto *surface = findItem(canvas, "flowSurface");
    require(surface != nullptr, "Canvas surface is missing");
    const std::array<const char *, 5> ports{"images", "rois", "scores", "classes", "class_names"};
    const auto sizes = parser.isSet("full") ? std::vector<int>{20, 50, 100} : std::vector<int>{nodeCount};
    for (const auto count : sizes)
        for (const bool dense : {false, true})
        {
            ProjectEditor       editor;
            const auto          flow = editor.project().flows.front().id;
            std::vector<NodeId> ids;
            for (int index = 0; index < count; ++index)
            {
                const auto id = editor.addNode(flow, *definition, {2 * (index % 10), 2 * index});
                ids.push_back(id);
                for (int port = 0; port < (dense ? 5 : 1) && port < index; ++port)
                    require(editor.bindFreeInput(flow, {ids[index - port - 1], ports[port]}, {id, ports[port]}),
                            "Cannot bind routing graph");
            }
            auto       project = editor.project();
            const auto density = dense ? "dense" : "sparse";
            const auto projectPath
                = output.parent_path() / ("routing-" + std::to_string(count) + "-" + density + ".vrt.json");
            ProjectStorage(registry).save(project, projectPath);
            const auto targetId = QString::number(ids.back());
            app.requestOpen(QString::fromStdU16String(projectPath.u16string()));
            app.resolveUnsaved("discard");
            require(QTest::qWaitFor([&] { return !app.busy() && app.nodes().size() == count; }, 10000),
                    "Routing graph did not load");
            app.selectNode(targetId);
            require(QTest::qWaitFor([&] { return list(canvas->property("routedWires")).size() == app.wires().size(); },
                                    10000),
                    "Initial routing did not finish");
            std::vector<double> timings;
            qsizetype           publications = 0;
            for (int sample = 0; sample < samples; ++sample)
            {
                require(QMetaObject::invokeMethod(canvas, "fit"), "Fit action is missing");
                auto *target = findItem(canvas, "flowNode_" + targetId);
                require(target != nullptr, "Dragged node is missing");
                const auto initialX = target->x();
                const auto start    = target->mapToScene(target->boundingRect().center()).toPoint();
                const auto zoom     = canvas->property("zoom").toDouble();
                QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, start, 0);
                require(canvas->property("gesture").toString() == "nodes", "Pointer did not start a node drag");
                QSignalSpy changes(canvas, SIGNAL(routedWiresChanged()));
                require(changes.isValid(), "Routing publication signal is missing");
                std::cout << "ROUTING_BURST_BEGIN nodes=" << count << " density=" << density << " sample=" << sample
                          << std::endl;
                const auto begin = Clock::now();
                for (int offset : {1, 2, 3, 4, 1})
                {
                    const auto point = surface->mapFromScene(start + QPointF(180 * zoom * offset, 0));
                    require(QMetaObject::invokeMethod(canvas, "updateGesture", Q_ARG(QVariant, point)),
                            "Drag action is missing");
                }
                require(QTest::qWaitFor([&] { return !changes.isEmpty(); }, 10000), "Drag routing did not publish");
                const auto routed       = Clock::now();
                const auto releasePoint = start + QPoint(qRound(180 * zoom), 0);
                std::cout << "ROUTING_RELEASE_BEGIN" << std::endl;
                QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, releasePoint, 0);
                require(QTest::qWaitFor(
                            [&]
                            {
                                const auto routes  = list(canvas->property("routedWires"));
                                auto      *current = findItem(canvas, "flowNode_" + targetId);
                                if (!current || routes.size() != app.wires().size())
                                    return false;
                                bool found = false;
                                for (const auto &entry : routes)
                                {
                                    const auto wire = entry.toMap();
                                    if (!wire.value("error").toString().isEmpty())
                                        return false;
                                    if (wire.value("target").toString() != targetId)
                                        continue;
                                    const auto points = wire.value("points").toList();
                                    if (points.isEmpty()
                                        || QLineF(points.back().toPointF(), QPointF(current->x() + 78, current->y()))
                                                   .length()
                                               > 0.01)
                                        return false;
                                    found = true;
                                }
                                return found && std::abs(current->x() - initialX - 180) < 0.01;
                            },
                            10000),
                        "Final routes do not match the released node");
                const auto released = Clock::now();
                QSignalSpy frames(&window, &QQuickWindow::frameSwapped);
                window.update();
                require(QTest::qWaitFor([&] { return !frames.isEmpty(); }, 10000),
                        "Released route frame did not submit");
                const auto finish    = Clock::now();
                const auto burstMs   = std::chrono::duration<double, std::milli>(routed - begin).count();
                const auto releaseMs = std::chrono::duration<double, std::milli>(released - routed).count();
                const auto frameMs   = std::chrono::duration<double, std::milli>(finish - released).count();
                std::cout << "ROUTING_SAMPLE_END" << std::endl;
                timings.push_back(burstMs + releaseMs + frameMs);
                publications += changes.size();
                raw << count << ',' << density << ',' << app.wires().size() << ',' << sample << ',' << changes.size()
                    << ',' << burstMs << ',' << releaseMs << ',' << frameMs << ',' << timings.back() << '\n';
                raw.flush();
            }
            summary << count << ',' << density << ',' << app.wires().size() << ',' << samples << ',' << publications
                    << ',' << percentile(timings, 0.5) << ',' << percentile(timings, 0.95) << '\n';
            raw.flush();
            summary.flush();
            std::cout << count << ' ' << density << " edges=" << app.wires().size() << " publications=" << publications
                      << " p50=" << percentile(timings, 0.5) << " p95=" << percentile(timings, 0.95) << std::endl;
        }
    require(raw.good() && summary.good(), "Routing evidence write failed");
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
