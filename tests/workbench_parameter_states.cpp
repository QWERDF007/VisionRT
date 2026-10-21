#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include "ui/Theme.h"

#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <iostream>
#include <memory>
#include <stdexcept>

void parameterStatesContract(visionrt::AppController &app)
{
    using namespace visionrt;
    const auto require = [](bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    };
    qmlRegisterType<ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);
    engine.rootContext()->setContextProperty("Theme", Theme::instance());
    engine.rootContext()->setContextProperty("demoMenu", false);
    engine.rootContext()->setContextProperty("demoSelect", false);
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/Main.qml")));
    std::unique_ptr<QObject> owner(component.create());
    if (!owner) throw std::runtime_error(component.errorString().toStdString());
    auto *window = qobject_cast<QQuickWindow *>(owner.get());
    require(window != nullptr, "parameter state fixture did not create the actual main window");
    window->resize(1600, 1000);
    window->requestActivate();
    require(QTest::qWaitForWindowActive(window), "parameter state window did not become active");
    QTest::qWait(100);
    const auto findItem = [](auto &&self, QQuickItem *parent, const QString &name) -> QQuickItem *
    {
        for (auto *child : parent->childItems())
        {
            if (child->objectName() == name) return child;
            if (auto *found = self(self, child, name)) return found;
        }
        return nullptr;
    };
    QQuickItem *panel = findItem(findItem, window->contentItem(), "parameter_ratio");
    require(panel != nullptr, "actual main window has no parameter rows");
    while (panel && !panel->property("parameterGroups").isValid()) panel = panel->parentItem();
    require(panel != nullptr, "parameter rows do not belong to a parameter panel");
    const auto divider = panel->mapToScene(QPointF(panel->width() / 2, -1)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, divider);
    QTest::mouseMove(window, divider - QPoint(0, 250), 30);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, divider - QPoint(0, 250));
    QTest::qWait(100);
    require(panel->height() > 520, "actual split handle did not expose every parameter row");
    for (const auto &parameter : app.parameters())
    {
        auto *row = findItem(findItem, panel, "parameter_" + parameter.toMap().value("id").toString());
        require(row && row->isVisible() && row->mapToItem(panel, QPointF(0, row->height())).y() < panel->height(),
                "parameter state evidence leaves a field outside the visible panel");
    }
    auto *labelsRow = findItem(findItem, panel, "parameter_labels");
    require(labelsRow != nullptr, "structure row is missing from actual field state review");
    QQuickItem *structureText = nullptr;
    for (auto *candidate : labelsRow->findChildren<QQuickItem *>())
        if (candidate->property("text").toString().startsWith("StringArray")
            && candidate->metaObject()->indexOfSignal("clicked()") >= 0)
            structureText = qobject_cast<QQuickItem *>(candidate->property("contentItem").value<QObject *>());
    require(structureText && structureText->property("truncated").toBool(),
            "long structure value overflows its field instead of being visibly elided");
    require(QDir().mkpath("build/evidence"), "cannot create parameter state evidence directory");
    const auto capture = [&](const QString &state)
    {
        QTest::qWait(50);
        require(window->grabWindow().save("build/evidence/workbench_parameters_" + state + ".png"),
                "cannot capture actual parameter field states");
    };
    const auto inputControl = [&](const QString &id) -> QQuickItem *
    {
        auto *row = findItem(findItem, panel, "parameter_" + id);
        require(row != nullptr, "parameter state row is missing");
        for (auto *candidate : row->findChildren<QQuickItem *>())
            if (candidate->property("inputItem").isValid() && candidate->property("readOnly").isValid()) return candidate;
        return nullptr;
    };
    auto *ratio = inputControl("ratio");
    auto *path = inputControl("path");
    require(ratio && path, "parameter text and numerical controls are unavailable");
    auto *ratioInput = qobject_cast<QQuickItem *>(ratio->property("inputItem").value<QObject *>());
    auto *pathInput = qobject_cast<QQuickItem *>(path->property("inputItem").value<QObject *>());
    require(ratioInput && pathInput, "parameter state input aliases are unavailable");
    QSignalSpy changes(&app, &AppController::parametersChanged);
    const auto baseline = app.parameters();
    capture("normal");
    ratioInput->forceActiveFocus();
    capture("numeric_focus");
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(window, Qt::Key_X);
    QTest::keyClick(window, Qt::Key_Return);
    require(ratio->property("invalid").toBool(), "actual numeric field did not reject an invalid keyboard draft");
    capture("invalid");
    QTest::keyClick(window, Qt::Key_Escape);
    auto *choiceRow = findItem(findItem, panel, "parameter_mode");
    QQuickItem *choice = nullptr;
    require(choiceRow != nullptr, "actual enum row is missing");
    for (auto *candidate : choiceRow->findChildren<QQuickItem *>())
        if (candidate->property("popup").isValid() && candidate->property("currentIndex").isValid()) choice = candidate;
    require(choice != nullptr, "actual enum control is missing");
    choice->forceActiveFocus();
    capture("choice_focus");
    pathInput->forceActiveFocus();
    capture("text_focus");
    panel->forceActiveFocus();
    QTest::mouseMove(window, pathInput->mapToScene(QPointF(pathInput->width() / 2, pathInput->height() / 2)).toPoint());
    capture("hover");
    ratio->setProperty("readOnly", true);
    path->setProperty("readOnly", true);
    capture("read_only");
    ratio->setProperty("readOnly", false);
    path->setProperty("readOnly", false);
    panel->setEnabled(false);
    capture("disabled");
    panel->setEnabled(true);
    require(app.parameters() == baseline && changes.isEmpty(), "parameter state review changed committed parameters");
    std::cout << "[Evidence D-04] Actual Main.qml field states captured using the host palette; invalid draft rejected without a parameter change.\n";
}
