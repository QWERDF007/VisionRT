#include "core/CustomToolStore.h"
#include "ui/AppController.h"
#include "ui/Theme.h"
#include "ui/ToolEditorController.h"

#include <quickui/Color.h>

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <filesystem>
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

QQuickItem *findByText(QQuickItem *parent, const QString &text)
{
    for (auto *child : parent->childItems())
    {
        if (child->property("text").toString() == text && child->isVisible())
            return child;
        if (auto *found = findByText(child, text))
            return found;
    }
    return nullptr;
}

int countByClass(QQuickItem *parent, const char *name)
{
    int count = 0;
    for (auto *child : parent->childItems())
    {
        if (QString(child->metaObject()->className()).contains(QLatin1String(name)))
            ++count;
        count += countByClass(child, name);
    }
    return count;
}

void click(QQuickWindow *window, QQuickItem *item)
{
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
}

} // namespace

int main(int argc, char **argv)
try
{
    QGuiApplication app(argc, argv);
    QCoreApplication::addLibraryPath(QStringLiteral("E:/Softwares/Qt/6.11.0/msvc2022_64/plugins"));

    QTemporaryDir directory;
    require(directory.isValid(), "cannot create editor store");
    const std::filesystem::path root(directory.path().toStdWString());

    CustomToolStore store(root);
    ToolEditorController controller;
    controller.setCustomStore(&store);

    QQmlEngine engine;
    themeForEngine(&engine);
    // The workbench selects the light palette at startup; the dialog must be checked in it.
    quickui::QuiColor::getInstance()->setThemeMode(quickui::QuiColor::Light);
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.addImportPath(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml"));

    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/CustomToolDialog.qml")));
    std::unique_ptr<QObject> owner(component.create());
    if (!owner)
        throw std::runtime_error(component.errorString().toStdString());
    auto *window = qobject_cast<QQuickWindow *>(owner.get());
    require(window != nullptr, "custom tool editor is not a window");
    window->setProperty("controller", QVariant::fromValue(&controller));
    window->show();
    require(QTest::qWaitForWindowExposed(window), "custom tool editor was not exposed");

    AppController appController(1, "E:/Softwares/VisionRT");
    require(QTest::qWaitFor([&] { return !appController.busy(); }, 5000), "app controller busy timed out");
    controller.setAppController(&appController);
    controller.setEditBuiltins(true);
    require(QTest::qWaitFor([&] { return findByText(window->contentItem(), "预处理") != nullptr; }, 2000),
            "custom tool dialog did not render builtin tools after setEditBuiltins(true)");
    controller.setEditBuiltins(false);
    require(QTest::qWaitFor([&] { return findByText(window->contentItem(), "预处理") == nullptr; }, 2000),
            "custom tool dialog did not hide builtin tools after setEditBuiltins(false)");

    require(controller.addTool("用户扩展/示例工具"), "editor could not add a tool");
    require(QTest::qWaitFor([&] { return findByText(window->contentItem(), "保存") != nullptr; }, 2000),
            "the editor did not render its save action");

    auto *saveButton = findByText(window->contentItem(), "保存");
    require(saveButton->isEnabled(), "a dirty editor must offer Save");
    controller.setCurrentToolName("图像阈值");
    controller.setCurrentToolId("custom.image.threshold");
    require(controller.inputModel()->addPort("阈值", "int", "128"), "editor could not add an input");
    require(controller.inputModel()->updatePort(1, {{"min", "0"}, {"max", "255"}}), "range update failed");
    require(controller.inputModel()->addPort("输入图像", "image"), "editor could not add an image input");
    require(!controller.inputModel()->updatePort(1, {{"showImage", true}}),
            "a non-image parameter accepted the image display flag");
    require(controller.inputModel()->updatePort(2, {{"showImage", false}}), "the image display flag was rejected");
    require(controller.outputModel()->addPort("输出图像", "image"), "editor could not add an output");
    require(controller.outputModel()->updatePort(2, {{"showImage", true}}), "the output showImage flag was rejected");

    // The two parameter columns are real cards: the fixed status row plus the two authored inputs.
    require(QTest::qWaitFor([&] { return countByClass(window->contentItem(), "ToolPortCard") >= 3; }, 2000),
            "the input column did not render its parameter cards");

    const QString screenshot = qEnvironmentVariable("VISIONRT_EDITOR_SCREENSHOT");
    if (!screenshot.isEmpty())
    {
        QTest::qWait(300);
        QImage image = window->grabWindow();
        std::cout << "screenshot path: [" << screenshot.toStdString() << "]" << std::endl;
        std::cout << "grabWindow: isNull=" << image.isNull() << " size=" << image.width() << "x" << image.height() << std::endl;
        bool ok = image.save(screenshot);
        std::cout << "save result=" << ok << std::endl;
        require(ok, "could not capture the editor window");
    }

    click(window, saveButton);
    require(QTest::qWaitFor([&] { return !window->isVisible(); }, 3000), "Save did not close the editor");
    require(std::filesystem::exists(root / "tools" / "custom.image.threshold.json")
            || std::filesystem::exists(root / "tools" / "custom.tool.1.json"),
            "Save did not write the declaration");

    std::cout << "[PASS] tool_editor_dialog_test" << std::endl;
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << "FAIL: " << error.what() << std::endl;
    return 1;
}
