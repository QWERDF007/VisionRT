#include "SettingsFileGuard.h"
#include "WorkbenchFixtures.h"
#include "core/CustomToolStore.h"
#include "ui/AppController.h"
#include "log/LogLevel.h"
#include "ui/UiTypes.h"
#include "ui/ToolEditorController.h"

#include <quickui/Color.h>
#include <quickui/Font.h>

#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSignalSpy>
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
}
int main(int argc, char **argv)
try
{
    QGuiApplication app(argc, argv);
    testing::SettingsFileGuard settingsFile;

    QTemporaryDir directory;
    require(directory.isValid(), "cannot create editor store");
    const std::filesystem::path root(directory.path().toStdWString());

    CustomToolStore store(root);
    ToolEditorController controller;
    controller.setCustomStore(&store);

    QQmlEngine engine;
    registerUiTypes();
    // The workbench selects the light palette at startup; the dialog must be checked in it.
    quickui::QuiColor::getInstance()->setThemeMode(quickui::QuiColor::Light);
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.addImportPath(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml"));

    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/CustomToolWindow.qml")));
    std::unique_ptr<QObject> owner(component.create());
    if (!owner)
        throw std::runtime_error(component.errorString().toStdString());
    auto *window = qobject_cast<QQuickWindow *>(owner.get());
    require(window != nullptr, "custom tool editor is not a window");
    require(window->modality() == Qt::WindowModal, "editor must block its owner window");
    window->setProperty("controller", QVariant::fromValue(&controller));
    window->show();
    require(QTest::qWaitForWindowExposed(window), "custom tool editor was not exposed");

    // The editor lists what the installed catalog contains, so the fixture supplies the same
    // package every other workbench test uses instead of a machine-specific install path.
    const testing::WorkbenchInstallation installation;
    AppController                     appController(1, installation.directory());
    if (!QTest::qWaitFor([&] { return !appController.busy(); }, 15000))
        throw std::runtime_error(("editor catalog did not settle: " + appController.runStateText()
                                 + " " + appController.diagnostics().join(" | ")).toStdString());
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
    auto *cancelButton = window->findChild<QQuickItem *>("cancelBtn");
    require(cancelButton != nullptr, "the editor did not render its cancel action");
    controller.setCurrentToolName("图像阈值");
    controller.setCurrentToolId("custom.image.threshold");
    require(controller.inputModel()->addPort("阈值", (QVariantMap{{"type", static_cast<int>(Type::Int)}}), "128"), "editor could not add an input");
    require(controller.inputModel()->updatePort(1, {{"min", "0"}, {"max", "255"}}), "range update failed");
    require(controller.inputModel()->addPort("输入图像", (QVariantMap{{"type", static_cast<int>(Type::Image)}})), "editor could not add an image input");
    require(!controller.inputModel()->updatePort(1, {{"showImage", true}}),
            "a non-image parameter accepted the image display flag");
    require(controller.inputModel()->updatePort(2, {{"showImage", false}}), "the image display flag was rejected");
    require(controller.outputModel()->addPort("输出图像", (QVariantMap{{"type", static_cast<int>(Type::Image)}})), "editor could not add an output");
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
    // The declaration is written inside the folder the tool was added to: that folder is its
    // category.
    require(std::filesystem::exists(root / "tools" / u8"用户扩展" / u8"示例工具" / "custom.image.threshold.json"),
            "Save did not write the declaration");

    QQuickWindow workbench;
    workbench.resize(1000, 700);
    workbench.show();
    require(QTest::qWaitForWindowExposed(&workbench), "settings owner was not exposed");
    engine.rootContext()->setContextProperty("app", &appController);
    const auto originalSettings = appController.settingsSnapshot();
    const auto restoreSettings = qScopeGuard([&] { appController.applySettings(originalSettings); });
    const int changedLevel = originalSettings.value("loggingLevel").toInt() == static_cast<int>(LogLevel::Debug)
        ? static_cast<int>(LogLevel::Trace) : static_cast<int>(LogLevel::Debug);
    QQmlComponent settingsComponent(&engine, QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/SettingsWindow.qml")));
    std::unique_ptr<QObject> settingsOwner(settingsComponent.create());
    if (!settingsOwner) throw std::runtime_error(settingsComponent.errorString().toStdString());
    auto *settings = qobject_cast<QQuickWindow *>(settingsOwner.get());
    require(settings && settings->modality() == Qt::WindowModal, "settings must be a modal Window");
    settings->setTransientParent(&workbench);
    require(QMetaObject::invokeMethod(settings, "openDialog"), "cannot open settings");
    require(QTest::qWaitForWindowExposed(settings), "settings was not exposed");
    require(QGuiApplication::modalWindow() == settings, "settings did not block its owner");
    auto *apply = settings->findChild<QQuickItem *>("settingsApply");
    settings->setProperty("draftLevel", changedLevel);
    require(QTest::qWaitFor([&] { return apply->isEnabled(); }), "editing did not enable Apply");
    click(settings, apply);
    require(QTest::qWaitFor([&] { return !apply->isEnabled() && !appController.settingsApplying(); }), "Apply did not settle");
    require(settings->isVisible() && appController.settingsSnapshot().value("loggingLevel").toInt() == changedLevel,
            "Apply must commit and keep the settings window open");
    settings->setProperty("draftLevel", static_cast<int>(LogLevel::Error));
    settings->close();

    window->setTransientParent(&workbench);
    QMetaObject::invokeMethod(window, "openDialog");
    require(QTest::qWaitForWindowExposed(window), "editor did not reopen");
    auto *catalog = window->findChild<QQuickItem *>("toolCatalog");
    require(catalog && QMetaObject::invokeMethod(catalog, "openPrompt", Q_ARG(QVariant, "addRoot"),
                Q_ARG(QVariant, ""), Q_ARG(QVariant, "")), "cannot open folder prompt");
    require(QTest::qWaitFor([&] { return window->findChild<QQuickItem *>("folderNameField") != nullptr; }),
            "folder prompt did not load its field");
    auto *folderInput = window->findChild<QQuickItem *>("folderNameField")->property("inputItem").value<QQuickItem *>();
    require(folderInput && QTest::qWaitFor([&] { return folderInput->hasActiveFocus(); }), "folder prompt must focus its input");
    QInputMethodEvent folderText;
    folderText.setCommitString("目录契约");
    QCoreApplication::sendEvent(folderInput, &folderText);
    auto *confirmFolder = findByText(window->contentItem(), "确定");
    require(confirmFolder && confirmFolder->height() == 32, "folder prompt must use standard buttons");
    click(window, confirmFolder);
    require(QTest::qWaitFor([&] { return findByText(window->contentItem(), "目录契约") != nullptr; }),
            "folder prompt did not create the folder");
    require(QTest::qWaitFor([&] { return !window->findChild<QQuickItem *>("folderNameField"); }),
            "folder prompt did not finish closing");
    require(controller.addTool("目录契约"), "cannot create a dirty tool for close confirmation");
    window->close();
    require(window->isVisible(), "dirty editor must ask before closing");
    auto *discardPopup = window->findChild<QObject *>("discardConfirmation");
    require(discardPopup != nullptr, "discard confirmation was not instantiated");
    require(QTest::qWaitFor([&] { return discardPopup->property("opened").toBool(); }),
            "discard confirmation did not finish opening");
    auto *discard = findByText(window->contentItem(), "确定");
    require(discard != nullptr, "discard confirmation was not shown");
    click(window, discard);
    require(QTest::qWaitFor([&] { return !window->isVisible(); }), "confirming discard must close the editor");
    QSignalSpy settingsFrames(settings, &QQuickWindow::frameSwapped);
    QMetaObject::invokeMethod(settings, "openDialog");
    require(QTest::qWaitForWindowExposed(settings) && QTest::qWaitFor([&] { return !settingsFrames.isEmpty(); }),
            "reopened settings must render before inspection");
    require(settings->property("draftLevel").toInt() == changedLevel, "closing settings must discard unapplied edits");
    const QString settingsScreenshot = qEnvironmentVariable("VISIONRT_SETTINGS_SCREENSHOT");
    if (!settingsScreenshot.isEmpty()) require(settings->grabWindow().save(settingsScreenshot), "cannot capture settings");
    settings->setProperty("draftPython", qEnvironmentVariable("SystemRoot") + "/System32/where.exe");
    require(QMetaObject::invokeMethod(settings, "apply", Q_ARG(QVariant, false)), "cannot submit environment check");
    require(appController.settingsApplying(), "environment validation must be asynchronous");
    require(!settings->close() && settings->isVisible(), "pending environment check must reject window close");
    require(QTest::qWaitFor([&] { return !appController.settingsApplying(); }), "environment validation did not settle");
    require(!settings->property("errorText").toString().isEmpty(), "invalid environment must keep its error visible");
    settings->close();

    std::cout << "[PASS] tool_editor_dialog_test" << std::endl;
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << "FAIL: " << error.what() << std::endl;
    return 1;
}
