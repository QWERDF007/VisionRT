#include "WorkbenchFixtures.h"
#include "core/DefinitionJson.h"
#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include "ui/OutputModel.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTest>
#include <iostream>

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
        require(QDir().mkpath(QFileInfo(path).absolutePath()), "completion fixture parent unavailable");
        QFile file(path);
        require(file.open(QIODevice::WriteOnly) && file.write(content) == content.size(),
                "completion fixture write failed");
    };
    ToolDefinition definition;
    definition.id          = "test.completion";
    definition.displayName = "完成顺序";
    definition.kind        = ToolKind::PythonFunction;
    definition.entry       = {"completion", "tool.py", "source"};
    definition.outputs     = {
        {"values", "数据", {Type::Array, DType::Int64, 1}},
        {"images", "图像",                  {Type::Image}}
    };
    definition.outputs.back().colorOrder = "BGR";
    const auto declaration               = package + "tools/custom/completion/source.json";
    write(declaration, QByteArray::fromStdString(emitNormalizedToolJson(definition)));
    write(package + "python/custom/completion/tool.py", R"py(import numpy as np

def source(inputs):
    return {'outputs': {'values': [9007199254740993] * 1000000,
                        'images': [np.full((80, 120, 3), 17, dtype=np.uint8)],
                        '__status_code': 0, '__status_message': ''},
            'detail': ''}

def refreshed(inputs):
    return {'outputs': {'values': 'catalog-ready',
                        'images': [np.full((80, 120, 3), 37, dtype=np.uint8)],
                        '__status_code': 0, '__status_message': ''},
            'detail': ''}
)py");
    bool           monitor         = false;
    bool           staleAtIdle     = false;
    int            idleTransitions = 0;
    int            catalogs        = 0;
    QStringList    events;
    QList<QString> texts;
    QQuickWindow   window;
    window.resize(320, 240);
    window.setColor(QColor(17, 23, 31));
    ImageViewport viewport(window.contentItem());
    viewport.setSize({320, 240});
    window.show();
    require(QTest::qWaitForWindowExposed(&window), "completion window was not exposed");
    AppController app(2, root);
    const auto    idle = [&]
    {
        require(QTest::qWaitFor([&] { return !app.busy(); }, 10000), "completion operation did not finish");
    };
    idle();
    app.attachViewport(&viewport);
    auto *outputs = qobject_cast<OutputModel *>(app.outputModel());
    require(outputs, "completion output model unavailable");
    QObject::connect(&app, &AppController::outputTextReady, &app,
                     [&](const QString &text)
                     {
                         texts.push_back(text);
                         if (monitor)
                             events.push_back("text");
                     });
    QObject::connect(&app, &AppController::runtimeChanged, &app,
                     [&]
                     {
                         if (monitor && !app.busy())
                         {
                             ++idleTransitions;
                             events.push_back("idle");
                             const auto value = outputs->capture("values");
                             staleAtIdle      = staleAtIdle || !value || value->type.type != Type::String
                                        || !isMissing(value->value) || viewport.hasImage();
                         }
                     });
    QObject::connect(&app, &AppController::catalogChanged, &app,
                     [&]
                     {
                         if (monitor)
                         {
                             ++catalogs;
                             events.push_back("catalog");
                         }
                     });
    const auto frame = [&](QColor expected)
    {
        require(QTest::qWaitFor(
                    [&]
                    {
                        const auto image = window.grabWindow();
                        return !image.isNull() && image.pixelColor(image.width() / 2, image.height() / 2) == expected;
                    },
                    5000),
                "completion viewport has stale pixels");
    };
    // Copy is a bounded background task: a large value is formatted once and never gated behind a
    // second confirmation or truncated into something that looks complete.
    const auto exportLarge = [&]
    {
        app.requestOutputText(app.portContext("values", true), false, false);
    };
    const auto verifyText = [&](const QString &text)
    {
        const auto      bytes = text.toUtf8();
        QJsonParseError error;
        const auto      document = QJsonDocument::fromJson(bytes, &error);
        require(error.error == QJsonParseError::NoError && document.isArray(), "large export is not complete JSON");
        const auto array = document.array();
        require(array.size() == 1000000 && array[0].toInteger() == 9007199254740993LL
                    && array[500000].toInteger() == 9007199254740993LL
                    && array[999999].toInteger() == 9007199254740993LL,
                "large export truncated or narrowed the native int64 data");
        return bytes.size();
    };
    app.addNode("test.completion", 0, 0);
    idle();
    app.runNode();
    idle();
    frame(QColor(17, 17, 17));
    QSignalSpy stateChanges(&app, &AppController::runtimeChanged);
    exportLarge();
    require(QTest::qWaitFor([&] { return texts.size() == 1; }, 10000), "Idle large export completion was lost");
    require(!app.busy() && stateChanges.isEmpty(), "Idle export fabricated a runtime state transition");
    const auto bytes = verifyText(texts[0]);
    std::cout << "Idle export: elements=1000000 bytes=" << bytes << " runtime_changes=0\n";
    definition.outputs.front().type = {Type::String};
    definition.entry.symbol         = "refreshed";
    write(declaration, QByteArray::fromStdString(emitNormalizedToolJson(definition)));
    monitor = true;
    exportLarge();
    app.rescanTools();
    require(app.busy(), "refresh was not submitted from Idle");
    idle();
    const bool delivered = QTest::qWaitFor([&] { return texts.size() == 2; }, 3000);
    frame(QColor(17, 23, 31));
    std::cout << "Refresh completion: idle_transitions=" << idleTransitions << " stale_at_idle=" << staleAtIdle
              << " catalogs=" << catalogs << " exports=" << texts.size() << " order=" << events.join(',').toStdString()
              << '\n';
    require(delivered, "refresh discarded an already completed explicit export");
    require(!staleAtIdle && idleTransitions == 1 && catalogs == 1,
            "Idle notification preceded output reconciliation or exposed an intermediate Idle state");
    require(events.indexOf("text") >= 0 && events.indexOf("text") < events.indexOf("idle")
                && events.indexOf("catalog") < events.indexOf("idle"),
            "refresh completed before its pending text or catalog update was delivered");
    require(verifyText(texts[1]) == bytes, "refresh changed the completed export payload");
    monitor = false;
    app.runNode();
    idle();
    const auto refreshed = outputs->capture("values");
    require(
        refreshed && refreshed->type.type == Type::String && std::get<std::string>(refreshed->value) == "catalog-ready",
        "final Idle did not permit execution of the refreshed implementation");
    frame(QColor(37, 37, 37));
    std::cout << "Completion transaction: exports=2 final_value=catalog-ready rendered_pixel=37\n";
    window.hide();
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
