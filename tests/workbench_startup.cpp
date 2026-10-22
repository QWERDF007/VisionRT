#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include "ui/Theme.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

void startupProjectContract()
{
    const auto require = [](bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };
    qmlRegisterType<visionrt::ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
    qmlRegisterSingletonInstance<visionrt::Theme>("VisionRT", 1, 0, "Theme", visionrt::Theme::instance());
    for (const bool projectReadyBeforeWindow : {false, true})
    {
        visionrt::AppController app(1, QStringLiteral(VISIONRT_BINARY_DIR));
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "startup catalog did not become ready");
        app.requestOpen(QStringLiteral(VISIONRT_SOURCE_DIR "/examples/pipeline_cpu_python.vrt.json"));
        app.resolveUnsaved(QStringLiteral("discard"));
        if (projectReadyBeforeWindow)
            require(QTest::qWaitFor([&] { return !app.busy() && app.nodes().size() == 5; }, 5000),
                    "preloaded startup project did not become ready");

        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
        engine.rootContext()->setContextProperty("app", &app);
        engine.rootContext()->setContextProperty("Theme", visionrt::Theme::instance());
        engine.rootContext()->setContextProperty("demoMenu", false);
        engine.rootContext()->setContextProperty("demoSelect", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/Main.qml")));
        require(!engine.rootObjects().isEmpty(), "startup Main.qml did not load");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        require(window && QTest::qWaitForWindowExposed(window), "startup main window was not exposed");
        require(QTest::qWaitFor([&] { return !app.busy() && app.nodes().size() == 5; }, 5000),
                "startup main window did not receive the real project");

        const auto findItem = [](auto &&self, QQuickItem *parent, const QString &name) -> QQuickItem *
        {
            for (auto *child : parent->childItems())
            {
                if (child->objectName() == name)
                    return child;
                if (auto *found = self(self, child, name))
                    return found;
            }
            return nullptr;
        };
        auto *canvas = findItem(findItem, window->contentItem(), QStringLiteral("flowCanvas"));
        require(canvas != nullptr, "startup flow canvas is missing");
        auto *surface = findItem(findItem, canvas, QStringLiteral("flowSurface"));
        require(surface != nullptr, "startup flow surface is missing");
        const auto allVisible = [&]
        {
            for (const auto &value : app.nodes())
            {
                const auto id = value.toMap().value("id").toString();
                auto *item = findItem(findItem, canvas, QStringLiteral("flowNode_") + id);
                if (!item || !item->isVisible())
                    return false;
                const auto bounds = item->mapRectToItem(surface, item->boundingRect());
                if (bounds.width() < 70 || bounds.height() <= 0
                    || !surface->boundingRect().adjusted(1, 1, -1, -1).contains(bounds))
                    return false;
            }
            return true;
        };
        const bool visible = QTest::qWaitFor(allVisible, 1500);
        const auto mode = projectReadyBeforeWindow ? "preloaded" : "pending";
        require(window->grabWindow().save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/startup_%1.png")
                                             .arg(QString::fromLatin1(mode))),
                "cannot save startup main-window evidence");
        if (!visible)
            throw std::runtime_error(std::string("Startup ") + mode + " project is clipped: viewport="
                                     + std::to_string(surface->width()) + "x" + std::to_string(surface->height())
                                     + "; zoom=" + std::to_string(canvas->property("zoom").toDouble())
                                     + "; pan=" + std::to_string(canvas->property("panX").toDouble()) + ","
                                     + std::to_string(canvas->property("panY").toDouble()));
        std::cout << "[Startup Contract Passed] " << mode << " project has five visible, readable nodes.\n";
        if (projectReadyBeforeWindow)
        {
            QTemporaryDir directory;
            require(directory.isValid(), "project diagnostic directory is unavailable");
            const auto badPath = directory.filePath(QStringLiteral("错误 工程.vrt.json"));
            QFile      badProject(badPath);
            require(badProject.open(QIODevice::WriteOnly), "cannot write malformed project fixture");
            require(badProject.write(R"json({"format_version":2,"project":{},"flows":{}})json") > 0,
                    "cannot populate malformed project fixture");
            badProject.close();
            const auto oldNodes   = app.nodes();
            const auto oldFlows   = app.flows();
            const auto oldTitle   = app.title();
            const auto beforeOpen = app.diagnostics().size();
            app.requestOpen(badPath);
            app.resolveUnsaved(QStringLiteral("discard"));
            require(QTest::qWaitFor([&] { return !app.busy() && app.diagnostics().size() > beforeOpen; }, 3000),
                    "project contract failure did not reach the application");
            const auto failure = QJsonDocument::fromJson(app.diagnosticsJson().toUtf8()).array().at(0).toObject();
            require(failure.value("code").toInt() == -2
                        && failure.value("message").toString().contains(QStringLiteral("flows 必须是数组"))
                        && failure.value("message").toString().contains(badPath)
                        && failure.value("detail").toString().isEmpty(),
                    "project contract reason was hidden behind an unrelated summary or exception detail");
            require(app.nodes() == oldNodes && app.flows() == oldFlows && app.title() == oldTitle,
                    "malformed project replaced the active project");
            auto *failureNotice = window->findChild<QObject *>("applicationDiagnosticDialog");
            require(failureNotice && QMetaObject::invokeMethod(failureNotice, "open"),
                    "project diagnostic dialog is missing");
            require(QTest::qWaitFor([&] { return failureNotice->property("opened").toBool(); }, 1000)
                        && failureNotice->property("text").toString().contains(app.diagnostic()),
                    "real window did not display the precise project rejection");
            require(window->grabWindow().save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/project_load_failure.png")),
                    "cannot capture project failure diagnostic");
            require(QMetaObject::invokeMethod(failureNotice, "close"), "cannot close project failure diagnostic");
            require(QTest::qWaitFor([&] { return !failureNotice->property("visible").toBool(); }, 1000),
                    "project failure diagnostic did not close");
            QFile example(QStringLiteral(VISIONRT_SOURCE_DIR "/examples/pipeline_cpu_python.vrt.json"));
            require(example.open(QIODevice::ReadOnly), "cannot read project warning fixture");
            auto document     = QJsonDocument::fromJson(example.readAll()).object();
            auto projectFlows = document.value("flows").toArray();
            auto projectFlow  = projectFlows.at(0).toObject();
            auto projectNodes = projectFlow.value("nodes").toArray();
            auto configured   = projectNodes.at(3).toObject();
            require(configured.value("node_seq").toString() == "4", "project warning node identity changed");
            auto inputs = configured.value("inputs").toObject();
            auto kernel = inputs.value("kernel").toObject();
            kernel.insert("literal", 0);
            inputs.insert("kernel", kernel);
            configured.insert("inputs", inputs);
            projectNodes[3] = configured;
            projectFlow.insert("nodes", projectNodes);
            projectFlows[0] = projectFlow;
            document.insert("flows", projectFlows);
            const auto warningPath = directory.filePath(QStringLiteral("越界 工程.vrt.json"));
            QFile      warningProject(warningPath);
            require(
                warningProject.open(QIODevice::WriteOnly) && warningProject.write(QJsonDocument(document).toJson()) > 0,
                "cannot write project warning fixture");
            warningProject.close();
            app.requestOpen(warningPath);
            app.resolveUnsaved(QStringLiteral("discard"));
            require(QTest::qWaitFor([&] { return !app.busy() && app.nodes().size() == 5; }, 3000),
                    "project warning load did not complete");
            QJsonObject rangeWarning;
            for (const auto &entry : QJsonDocument::fromJson(app.diagnosticsJson().toUtf8()).array())
                if (entry.toObject().value("storage_code") == "range_warning")
                    rangeWarning = entry.toObject();
            require(rangeWarning.value("flow_id") == "flow_1" && rangeWarning.value("node") == "4"
                        && rangeWarning.value("field") == "kernel"
                        && rangeWarning.value("message").toString().contains(QStringLiteral("已保留原值"))
                        && !rangeWarning.contains("code"),
                    "project warning lost its typed context or invented an execution status");
            app.selectNode("4");
            bool retained = false;
            for (const auto &parameter : app.parameters())
                if (parameter.toMap().value("id") == "kernel")
                    retained = parameter.toMap().value("valueText") == "0";
            require(retained, "loading a range warning clamped the original parameter");
            const auto previous = app.diagnostics().size();
            app.addNode("test.unregistered", 0, 0);
            app.addNode("test.unregistered", 0, 0);
            require(app.diagnostics().size() == previous + 2, "same-text application events were deduplicated");
            require(app.diagnostic() == QStringLiteral("工具定义缺失：test.unregistered"),
                    "application diagnostics lost the known Chinese reason or stable tool ID");
            auto *notice = window->findChild<QObject *>("applicationDiagnosticDialog");
            require(notice && QMetaObject::invokeMethod(notice, "open"), "application diagnostic dialog is missing");
            require(QTest::qWaitFor([&] { return notice->property("opened").toBool(); }, 1000),
                    "application diagnostic dialog did not open");
            const auto summaries = notice->property("text").toString();
            require(summaries.count(app.diagnostic()) >= 2, "QML hides a same-text new diagnostic event");
            require(summaries.contains(QStringLiteral("流程 flow_1 · 节点 4 · 字段 kernel：")),
                    "real diagnostic dialog omitted project warning context");
            const auto findText = [](auto &&self, QQuickItem *parent, const QString &text) -> QQuickItem *
            {
                if (parent->isVisible() && parent->property("text").toString() == text)
                    return parent;
                for (auto *child : parent->childItems())
                    if (auto *found = self(self, child, text))
                        return found;
                return nullptr;
            };
            auto *viewJson = findText(findText, window->contentItem(), "查看 JSON");
            require(viewJson, "application JSON action is missing");
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                              viewJson->mapToScene(QPointF(viewJson->width() / 2, viewJson->height() / 2)).toPoint());
            auto *detail = window->findChild<QObject *>("applicationDiagnosticJsonDialog");
            require(detail && QTest::qWaitFor([&] { return detail->property("opened").toBool(); }, 1000),
                    "application diagnostic JSON did not open");
            const auto jsonText = detail->property("text").toString();
            const auto json     = QJsonDocument::fromJson(jsonText.toUtf8()).array();
            require(json.size() == app.diagnostics().size(), "application JSON omitted log entries");
            require(json.at(0).toObject().value("message") == json.at(1).toObject().value("message"),
                    "application JSON omitted a same-text new event");
            require(window->grabWindow().save(
                        QStringLiteral(VISIONRT_BINARY_DIR "/evidence/application_diagnostics_json.png")),
                    "cannot capture application diagnostic JSON");
            require(QMetaObject::invokeMethod(detail, "close"), "cannot close application diagnostic JSON");
            require(QTest::qWaitFor([&] { return !detail->property("visible").toBool(); }, 1000),
                    "application JSON did not close");
            auto *copyJson = findText(findText, window->contentItem(), "复制 JSON");
            require(copyJson, "application JSON copy action is missing");
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                              copyJson->mapToScene(QPointF(copyJson->width() / 2, copyJson->height() / 2)).toPoint());
            require(QGuiApplication::clipboard()->text() == jsonText, "application JSON clipboard text is incomplete");
            require(
                window->grabWindow().save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/application_diagnostics.png")),
                "cannot capture application diagnostics");
            require(QMetaObject::invokeMethod(notice, "close"), "cannot close application diagnostics");
        }
    }
}
