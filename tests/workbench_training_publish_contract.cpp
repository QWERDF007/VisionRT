// Applying training is one publish: the declared trainer reads the draft through ordinary ports,
// the workbench writes one complete asset revision, and the node learns every declared value in a
// single transaction. A failure leaves the draft and the previous result untouched.
#include "DirectoryEvents.h"
#include "SettingsFileGuard.h"
#include "WorkbenchFixtures.h"
#include "core/NodeContracts.h"
#include "core/ToolPaths.h"
#include "log/LogService.h"
#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include "ui/OutputModel.h"
#include "ui/TrainingAssets.h"
#include "ui/UiTypes.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace visionrt;

namespace {
constexpr PortId kPathInput     = 1;
constexpr PortId kCountInput    = 5;
constexpr PortId kImagesOutput  = 2;
constexpr PortId kTrainImage    = 21;
constexpr PortId kModelParam    = 7;
constexpr PortId kFitParam      = 9;
constexpr PortId kMessageInput  = 24;
constexpr PortId kAssetParam    = 0;
constexpr PortId kContrastParam = 1;

const char *kFitDeclaration = R"json({
  "schema_version": 2,
  "name_en": "ProbeFit",
  "name_zh": "探针拟合",
  "version": "1.0.0",
  "implementation": {
    "kind": "cpp"
  },
  "ui": {
    "visibility": "internal"
  },
  "inputs": [
    {
      "id": 1,
      "name_zh": "训练集",
      "type": "training_set"
    },
    {
      "id": 2,
      "name_zh": "字段编号",
      "type": "int",
      "default": 0
    },
    {
      "id": 3,
      "name_zh": "参数",
      "type": "int",
      "default": 0
    },
    {
      "id": 4,
      "name_zh": "常量",
      "type": "int",
      "default": 0
    }
  ],
  "outputs": [
    {
      "id": 2,
      "name_zh": "模型",
      "type": {
        "type": "array",
        "dtype": "uint8",
        "rank": 1
      }
    },
    {
      "id": 3,
      "name_zh": "拟合值",
      "type": "int"
    },
    {
      "id": 4,
      "name_zh": "说明",
      "type": "string"
    }
  ]
})json";

const char *kTrainableDeclaration = R"json({
  "schema_version": 2,
  "name_en": "ProbeTrainable",
  "name_zh": "探针可训练",
  "version": "1.0.0",
  "implementation": {
    "kind": "cpp"
  },
  "init_params": [
    {
      "id": 7,
      "name_zh": "模型",
      "type": "string",
      "ui": {
        "editor": "path"
      }
    },
    {
      "id": 9,
      "name_zh": "拟合值",
      "type": "int",
      "default": 0,
      "constraints": {"max": 100}
    }
  ],
  "inputs": [
    {
      "id": 21,
      "name_zh": "图像",
      "type": "image",
      "display": {
        "visible": true
      }
    },
    {
      "id": 24,
      "name_zh": "说明",
      "type": "string",
      "default": ""
    }
  ],
  "outputs": [
    {
      "id": 2,
      "name_zh": "就绪",
      "type": "int"
    }
  ],
  "training": {
    "image": {
      "stage": "inputs",
      "id": 21
    },
    "records": {
      "max": 1,
      "fields": [
        {
          "id": 2,
          "name_zh": "名称",
          "type": "string"
        }
      ]
    },
    "call": {
      "tool": "ProbeFit",
      "inputs": [
        {
          "port": 1,
          "context": "training_set"
        },
        {
          "port": 2,
          "field_id": 2
        },
        {
          "port": 3,
          "parameter": 1
        },
        {
          "port": 4,
          "literal": {
            "type": "int",
            "value": 5
          }
        }
      ]
    },
    "parameters": [
      {
        "id": 1,
        "name_zh": "对比度",
        "type": "int",
        "default": 30,
        "constraints": {
          "min": 0,
          "max": 1024
        }
      },
      {
        "id": 2,
        "name_zh": "最低完整度",
        "type": "float",
        "default": 0.4,
        "constraints": {
          "min": 0.0,
          "max": 1.0
        },
        "ui": {
          "editor": "number",
          "decimals": 2
        }
      }
    ],
    "writes": [
      {
        "source_port": 3,
        "target": {
          "stage": "init",
          "id": 9
        }
      },
      {
        "source_port": 4,
        "target": {
          "stage": "inputs",
          "id": 24
        }
      }
    ],
    "artifacts": [
      {
        "id": "model",
        "source_port": 2,
        "format": "probe.model",
        "version": 1,
        "target": {
          "stage": "init",
          "id": 7
        }
      }
    ]
  }
})json";

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

QQuickItem *trainingEditor(QQuickItem *parent, const QVariantMap &parameter)
{
    for (auto *item : parent->childItems())
    {
        const auto field = item->property("field").toMap();
        if (field.value("id") == parameter.value("id") && field.value("name") == parameter.value("name"))
            return item;
        if (auto *editor = trainingEditor(item, parameter))
            return editor;
    }
    return nullptr;
}

void writeText(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        throw std::runtime_error("probe fixture is not writable: " + path.toStdString());
    if (file.write(content) != content.size())
        throw std::runtime_error("probe fixture was not fully written: " + path.toStdString());
}

QJsonObject readObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("probe fixture is unreadable: " + path.toStdString());
    return QJsonDocument::fromJson(file.readAll()).object();
}

void writeObject(const QString &path, const QJsonObject &object)
{
    writeText(path, QJsonDocument(object).toJson(QJsonDocument::Indented));
}

void installProbe(const QString &root, const QString &probeDll)
{
    const QString package           = root + "/tools-package";
    const auto    readerPath        = package + "/tools/builtin/采集/图像采集/图像读取.json";
    auto          readerDeclaration = readObject(readerPath);
    auto          readerOutputs     = readerDeclaration.value("outputs").toArray();
    for (int index = 0; index < readerOutputs.size(); ++index)
    {
        auto port = readerOutputs[index].toObject();
        if (port.value("id").toInt() != kImagesOutput)
            continue;
        auto display           = port.value("display").toObject();
        display["color_order"] = "bgr";
        port["display"]        = display;
        readerOutputs[index]   = port;
    }
    readerDeclaration["outputs"] = readerOutputs;
    writeObject(readerPath, readerDeclaration);
    QDir().mkpath(package + "/bin");
    const QString library = package + "/bin/test_authoring_probe.dll";
    QFile::remove(library);
    if (!QFile::copy(probeDll, library))
        throw std::runtime_error("probe library could not be copied into the installation");
    writeText(package + "/tools/builtin/探针/探针拟合.json", kFitDeclaration);
    writeText(package + "/tools/builtin/探针/探针可训练.json", kTrainableDeclaration);

    const QString index   = package + "/module_index.json";
    auto          object  = readObject(index);
    auto          modules = object.value("modules").toArray();
    modules.append(QJsonObject{
        {     "id",         "test_authoring_probe"},
        {"library", "bin/test_authoring_probe.dll"}
    });
    auto tools = object.value("tools").toArray();
    tools.append(QJsonObject{
        {   "tool_id",                               "ProbeFit"},
        {    "module",                   "test_authoring_probe"},
        {"definition", "tools/builtin/探针/探针拟合.json"}
    });
    tools.append(QJsonObject{
        {   "tool_id",                            "ProbeTrainable"},
        {    "module",                      "test_authoring_probe"},
        {"definition", "tools/builtin/探针/探针可训练.json"}
    });
    object["modules"] = modules;
    object["tools"]   = tools;
    writeObject(index, object);
}

int revisions(const QString &directory)
{
    int        count = 0;
    const QDir base(directory);
    for (const auto &name : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (name.toInt() > 0)
            ++count;
    return count;
}
} // namespace

int main(int argc, char **argv)
try
{
    QGuiApplication            application(argc, argv);
    testing::SettingsFileGuard settings;
    QQuickStyle::setStyle("Basic");
    registerUiTypes();
    qmlRegisterType<ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
    testing::WorkbenchInstallation installation;
    const auto                     root   = installation.directory();
    const auto                     images = root + "/publish-images";
    require(QDir().mkpath(images), "publish image directory could not be created");
    QImage sourceImage(150, 100, QImage::Format_RGB888);
    sourceImage.fill(QColor(18, 36, 72));
    require(sourceImage.save(images + "/0.png"), "publish image could not be written");
    installProbe(root, QString::fromUtf8(VISIONRT_TEST_AUTHORING_PROBE));

    // This contract publishes into the software directory the executable was launched from, the
    // same root a real installation uses; a previous run's revisions would hide a new one.
    std::error_code ignored;
    std::filesystem::remove_all(softwareDataDir() / "training", ignored);
    require(LogService::instance().start({}, root + "/logs/publish.log"), "publish log did not start");
    AppController app(2, root);
    const auto    settle = [&]
    {
        require(QTest::qWaitFor([&] { return !app.busy(); }, 20000), "publish operation did not settle");
    };
    const auto stable = [&]
    {
        settle();
        QTest::qWait(200);
        settle();
    };
    require(QTest::qWaitFor([&] { return !app.busy(); }, 10000), "publish catalog did not become ready");
    app.rescanTools();
    settle();

    // The training actions are offered by the image panel, so the workbench has to exist even
    // though this contract never clicks it.
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);
    engine.rootContext()->setContextProperty("demoMenu", false);
    engine.rootContext()->setContextProperty("demoFileMenu", false);
    engine.rootContext()->setContextProperty("demoSettings", false);
    engine.rootContext()->setContextProperty("demoSelect", false);
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/Main.qml")));
    std::unique_ptr<QObject> window(component.create());
    if (!window)
        throw std::runtime_error(component.errorString().toStdString());

    app.addNode("ReadImage", 0, 0);
    settle();
    const auto reader = app.selectedNode();
    require(!reader.isEmpty(), "publish reader node is missing: " + app.diagnostic().toStdString() + " "
                                   + app.diagnostics().join(" | ").toStdString());
    require(app.setParameter(kCountInput, false, "1"), "publish image count was rejected");
    require(app.setParameterPath(kPathInput, false, QUrl::fromLocalFile(images)), "publish image path was rejected");
    settle();
    app.addNode("ProbeTrainable", 0, 4);
    settle();
    const auto node = app.selectedNode();
    require(!node.isEmpty(), "trainable node is missing: " + app.diagnostic().toStdString());
    const auto manualParameters = app.parameters();
    require(std::any_of(manualParameters.begin(), manualParameters.end(),
                        [](const QVariant &value)
                        {
                            const auto row = value.toMap();
                            return row.value("id").toUInt() == kModelParam && row.value("init").toBool();
                        }),
            "an unpublished node must expose its existing model input");
    const auto existingModel = root + "/existing.model";
    writeText(existingModel, "PROBEMODEL");
    require(app.setParameterPath(kModelParam, true, QUrl::fromLocalFile(existingModel)),
            "an unpublished node must accept an existing model through its ordinary parameter");
    settle();
    require(app.bindInput(app.portContext(kTrainImage), reader, kImagesOutput),
            "trainable image input could not be bound");
    settle();
    const auto project = root + "/publish.vrt.json";
    require(app.save(project), "publish project could not be saved: " + app.diagnostic().toStdString());
    stable();
    app.runOnce();
    settle();
    auto *manualOutputs = qobject_cast<OutputModel *>(app.outputModel());
    require(manualOutputs != nullptr, "the business tool has no output model");
    const auto manualResult = manualOutputs->capture(PortId{2});
    require(manualResult && std::get<std::int64_t>(manualResult->value) == 1,
            "the ordinary model parameter did not initialize the business tool");
    app.setImageOutput(QStringLiteral("inputs:%1").arg(kTrainImage));
    stable();

    require(app.trainingAvailable(), "the declared training capability is missing");
    require(app.property("canAddTrainingImage").toBool(),
            "the published frame is not offered to training: output=" + app.selectedImageOutput().toStdString()
                + " shape=" + std::to_string(app.shapeToolsAvailable()) + " " + app.diagnostic().toStdString());
#ifdef _WIN32
    testing::DirectoryEvents fileEvents(std::filesystem::path(root.toStdWString()));
    const auto               trainingEvents = [&]
    {
        QStringList result;
        for (auto name : fileEvents.take())
        {
            name.replace('\\', '/');
            if (name.contains(".assets") || name.startsWith("training/"))
                result.append(name);
        }
        return result;
    };
    {
        QFile control(root + "/observer-control.tmp");
        require(control.open(QIODevice::WriteOnly), "event observer control could not be created");
        control.write("probe");
        control.close();
        control.remove();
        require(!fileEvents.take().isEmpty(), "the observer must detect a transient file");
    }
#endif
    std::cout << "TRAINING_PHASE_BEGIN draft" << std::endl;
    require(app.addTrainingImage(), "adding the published frame failed");
    require(app.trainingParameters().size() == 2, "the declared training parameters are missing");
    settle();
    auto *parameterPanel = window->findChild<QQuickItem *>("parameterPanel");
    auto *resultsPanel   = window->findChild<QQuickItem *>("resultsPanel");
    require(parameterPanel && resultsPanel, "the workbench areas are missing");
    for (const auto &value : app.trainingParameters())
    {
        const auto parameter = value.toMap();
        require(trainingEditor(parameterPanel, parameter),
                "a training-only parameter is missing from the parameter area");
        require(!trainingEditor(resultsPanel, parameter), "a training-only parameter was placed in the image area");
    }
    const auto record = app.selectedTrainingDraftRecord();
    require(!record.isEmpty(), "the added frame produced no record");
    app.setTrainingField(record, 2, "参考图");
    require(app.trainingSavable(), "a complete draft must be applicable");
    require(app.setTrainingParameter(kContrastParam, 41), "a declared training parameter was rejected");

    require(!std::filesystem::exists(softwareDataDir() / "training"), "editing a draft created a training directory");
    app.setTrainingField(record, 2, "等待");
#ifdef _WIN32
    require(trainingEvents().empty(), "draft editing produced training file events");
#endif
    std::cout << "TRAINING_PHASE_END draft files=0" << std::endl;
    bool eventLoopResponsive = false;
    QTimer::singleShot(10, &app,
                       [&]
                       {
                           eventLoopResponsive = !app.workspaceEnabled();
                           app.assistCancel();
                           app.setEasyTouchEnabled(false);
                       });
    QSignalSpy projectChanges(&app, &AppController::projectChanged);
    std::cout << "TRAINING_PHASE_BEGIN publish" << std::endl;
    app.applyTraining();
    stable();
    require(eventLoopResponsive, "the GUI event loop must remain responsive while a real trainer is running");
    require(!app.trainingDirty(), "a successful apply must leave a published draft");
    require(app.workspaceEnabled(), "leaving the canvas must not lose the training completion");
#ifdef _WIN32
    const auto publicationEvents = trainingEvents();
    require(!publicationEvents.empty(), "publication must be observed by the operating system");
    require(publicationEvents.join('\n').contains("manifest.json"), "the published manifest was not observed");
#endif
    std::cout << "TRAINING_PHASE_END publish files=present" << std::endl;

    const auto rows     = app.parameters();
    const auto rowValue = [&](PortId id, bool init) -> QVariant
    {
        for (const auto &value : rows)
            if (value.toMap().value("id").toUInt() == id && value.toMap().value("init").toBool() == init)
                return value.toMap();
        return {};
    };
    require(rowValue(kFitParam, true).toMap().value("valueText").toString() == QStringLiteral("46"),
            "the trainer's parameter write did not land on initialization 9");
    require(rowValue(kMessageInput, false).toMap().value("literal").toString().contains(QStringLiteral("field=2")),
            "the trainer's text write did not land on input 24");
    require(rowValue(kAssetParam, true).toMap().isEmpty() && rowValue(kModelParam, true).toMap().isEmpty(),
            "the reference and model paths must stay derived instead of editable rows");
    require(projectChanges.count() == 1,
            "publishing must raise exactly one project transaction, got " + std::to_string(projectChanges.count()));
    require(app.save(project), "the published project could not be saved");
    stable();

    // The project stores one reference to the revision, never the paths inside it. The node's
    // artifact targets are derived from that reference when the tool is initialized.
    const auto  savedProject = readObject(project);
    QJsonObject savedNode;
    for (const auto &flowValue : savedProject.value("flows").toArray())
        for (const auto &nodeValue : flowValue.toObject().value("nodes").toArray())
            if (nodeValue.toObject().value("node_seq").toString() == node)
                savedNode = nodeValue.toObject();
    const auto reference = savedNode.value("training_asset").toString();
    require(!reference.isEmpty() && QFileInfo(reference).isRelative(),
            "the published reference is missing or absolute: " + reference.toStdString());
    const auto revision
        = std::filesystem::path(project.toStdWString()).parent_path() / std::filesystem::path(reference.toStdWString());
    const auto assetPath = revision / "manifest.json";
    const auto manifest  = readObject(QString::fromStdWString(assetPath.wstring()));
    require(manifest.value("schema_version").toInt() == kTrainingManifestVersion, "manifest version");
    require(manifest.value("parameters").toObject().value("1").toInt() == 41, "manifest parameter");
    const auto artifacts = manifest.value("artifacts").toArray();
    require(artifacts.size() == 1, "manifest artifact");
    require(manifest.value("records").toArray().size() == 1, "manifest record");
    cv::Mat    persistedPixels;
    ColorOrder persistedOrder;
    training::readFrame(
        QString::fromStdWString(
            (revision
             / std::filesystem::path(
                 manifest.value("images").toArray().at(0).toObject().value("file").toString().toStdWString()))
                .wstring()),
        persistedPixels, persistedOrder);
    require(persistedOrder == ColorOrder::BGR, "training must preserve the source output's declared color order");
    require(persistedPixels.type() == CV_8UC3
                && cv::norm(persistedPixels, cv::Mat(100, 150, CV_8UC3, cv::Scalar(72, 36, 18)), cv::NORM_INF) == 0,
            "published training must preserve the original source pixels");
    const auto modelPath
        = revision / std::filesystem::path(artifacts.at(0).toObject().value("file").toString().toStdWString());
    require(std::filesystem::is_regular_file(modelPath), "the published model does not exist");
    {
        QFile model(QString::fromStdWString(modelPath.wstring()));
        require(model.open(QIODevice::ReadOnly), "the published model is unreadable");
        require(model.readAll() == QByteArray("PROBEMODEL"), "the host rewrote the model bytes");
    }
    const auto base = QString::fromStdWString(revision.parent_path().wstring());
    require(revisions(base) == 1, "a successful apply must publish exactly one revision");

    // A trainer that refuses the draft keeps it editable and publishes nothing.
    std::cout << "TRAINING_PHASE_BEGIN refused" << std::endl;
    QSignalSpy failedChanges(&app, &AppController::projectChanged);
    for (const auto *label : {"失败", "非法", "越界", "空模型"})
    {
        app.setTrainingField(record, 2, label);
        app.applyTraining();
        stable();
#ifdef _WIN32
        require(trainingEvents().empty(), "refused training reached the filesystem");
#endif
    }
    std::cout << "TRAINING_PHASE_END refused files=0" << std::endl;
    require(app.trainingDirty(), "a failed apply discarded the draft");
    require(failedChanges.count() == 0, "a failed apply changed the project");
    require(revisions(base) == 1, "a failed apply published a revision");
    require(app.parameters() == rows, "a failed apply changed the node parameters");
    require(std::filesystem::is_regular_file(modelPath), "a failed apply removed the previous model");

    app.setTrainingField(record, 2, "重试");
    require(app.bindInput(app.portContext(kMessageInput), reader, 1),
            "the training write target could not be bound upstream");
    stable();
    QSignalSpy upstreamChanges(&app, &AppController::projectChanged);
    std::cout << "TRAINING_PHASE_BEGIN upstream" << std::endl;
    app.applyTraining();
    stable();
#ifdef _WIN32
    require(trainingEvents().empty(), "an upstream training write target reached the filesystem");
#endif
    require(upstreamChanges.count() == 0 && revisions(base) == 1 && app.trainingDirty(),
            "an upstream target must preserve the draft and published revision");
    std::cout << "TRAINING_PHASE_END upstream files=0" << std::endl;
    require(app.unbindInput(app.portContext(kMessageInput)), "the training target could not be unbound");
    require(app.setParameterMode(kMessageInput, false), "the training target could not be switched to manual input");
    stable();

    // The same button publishes the corrected draft, as a new complete revision.
    app.setTrainingField(record, 2, "重试");
    app.applyTraining();
    stable();
    require(!app.trainingDirty(), "retrying after a failed apply did not publish");
    require(revisions(base) == 2, "the retry published no new revision");

    // The published source reopens with its typed parameters; the node still points at its model.
    require(app.save(project), "the published project could not be saved again");
    stable();
    app.requestOpen(project);
    app.resolveUnsaved(visionrt::UnsavedDecision::Discard);
    stable();
    app.selectNode(node);
    stable();
    require(app.trainingAvailable(), "the reopened project lost the training capability");
    app.setTrainingMode(true);
    settle();
    require(!app.trainingDirty(), "the reopened draft looks unapplied");
    const auto reopened = app.trainingParameters();
    require(reopened.size() == 2 && reopened.at(0).toMap().value("value").toInt() == 41,
            "the published parameters were not restored from the manifest");

    // Save-as carries the published revision beside the copy. The source project and its asset
    // tree are then moved away: the copy must still resolve its own model and training records.
    const auto copy = root + "/copy/copy.vrt.json";
    require(app.save(copy), "save-as did not copy the published project: " + app.diagnostic().toStdString());
    stable();
    QJsonObject copyNode;
    for (const auto &flowValue : readObject(copy).value("flows").toArray())
        for (const auto &nodeValue : flowValue.toObject().value("nodes").toArray())
            if (nodeValue.toObject().value("node_seq").toString() == node)
                copyNode = nodeValue.toObject();
    const auto copyReference = copyNode.value("training_asset").toString();
    require(QFileInfo(copyReference).isRelative(), "the copied project did not rewrite its reference");
    const auto copiedRevision = std::filesystem::path(copy.toStdWString()).parent_path()
                              / std::filesystem::path(copyReference.toStdWString());
    require(copiedRevision != revision, "save-as kept the source reference instead of copying the revision");
    require(std::filesystem::is_regular_file(copiedRevision / L"manifest.json"),
            "save-as did not copy the published manifest");
    require(std::filesystem::is_regular_file(copiedRevision / L"models" / L"model.bin"),
            "save-as did not copy the published model");

    std::error_code       moveError;
    std::filesystem::path tree = std::filesystem::path(base.toStdWString()).parent_path().parent_path();
    tree += L".moved";
    std::filesystem::rename(std::filesystem::path(base.toStdWString()).parent_path().parent_path(), tree, moveError);
    require(!moveError, "the source asset tree could not be moved away");
    require(QFile::rename(project, project + ".moved"), "the source project could not be moved away");

    app.requestOpen(copy);
    app.resolveUnsaved(visionrt::UnsavedDecision::Discard);
    stable();
    app.selectNode(node);
    stable();
    require(app.trainingAvailable(), "the copied project lost the training capability");
    app.setTrainingMode(true);
    settle();
    const auto copied = app.trainingParameters();
    require(copied.size() == 2 && copied.at(0).toMap().value("value").toInt() == 41,
            "the copied project did not restore its training parameters");
    require(app.trainingRecords().size() == 1, "the copied project did not restore its training records");

    app.setTrainingMode(false);
    app.runOnce();
    settle();
    auto *outputs = qobject_cast<OutputModel *>(app.outputModel());
    require(outputs != nullptr, "the copied project has no output model");
    bool readModel = false;
    for (int row = 0; row < outputs->rowCount(); ++row)
    {
        const auto captured = outputs->capture(row);
        if (captured && captured->port == 2)
            if (const auto *value = std::get_if<std::int64_t>(&captured->value))
                readModel = *value == 1;
    }
    require(readModel, "the copied project did not initialize with its own published model");

    // A target whose asset tree cannot be created refuses the save instead of writing a project
    // that points at assets it does not have.
    const auto blocked = root + "/blocked";
    require(QDir().mkpath(blocked), "blocked save directory could not be created");
    const auto blockedTree = blocked + "/" + copyReference.section(QLatin1Char('/'), 0, 0);
    QFile      blocker(blockedTree);
    require(blocker.open(QIODevice::WriteOnly) && blocker.write("x") == 1, "blocked asset tree could not be created");
    blocker.close();
    require(!app.save(blocked + "/blocked.vrt.json"), "a save with an unavailable asset tree was accepted");
    require(!QFile::exists(blocked + "/blocked.vrt.json"), "a failed save wrote the project file");

    const auto blockedProject = root + "/blocked-project/project.vrt.json";
    require(QDir().mkpath(blockedProject), "the project path blocker could not be created");
    require(!app.save(blockedProject), "a directory was accepted as a project file");
    for (const auto &entry : std::filesystem::recursive_directory_iterator(
             std::filesystem::path((root + "/blocked-project").toStdWString())))
        require(!entry.is_regular_file(), "a failed project save left an unreferenced training file");

    app.addNode("SelectBranch", 0, 2);
    stable();
    const auto selector = app.selectedNode();
    require(!selector.isEmpty()
                && app.applySelectType(selector, static_cast<int>(Type::Image), static_cast<int>(DType::Float64), 0),
            "the dynamic image source could not be configured");
    stable();
    require(app.setParameter(SelectPorts::Selector, false, "true"), "the image selector value was rejected");
    app.setParameterMode(SelectPorts::Selector, false);
    require(app.bindInput(app.portContext(SelectPorts::IfTrue), reader, kImagesOutput),
            "the true image branch could not be bound");
    stable();
    require(app.bindInput(app.portContext(SelectPorts::IfFalse), reader, kImagesOutput),
            "the false image branch could not be bound");
    app.selectNode(node);
    stable();
    require(app.bindInput(app.portContext(kTrainImage), selector, SelectPorts::Value),
            "the dynamic training image source could not be bound");
    stable();
    app.runOnce();
    stable();
    app.setImageOutput(QStringLiteral("inputs:%1").arg(kTrainImage));
    stable();
    require(app.addTrainingImage(),
            "a published dynamic image output must be accepted as a training source: " + app.diagnostic().toStdString()
                + " " + app.diagnostics().join(" | ").toStdString() + " canAdd="
                + std::to_string(app.canAddTrainingImage()) + " images=" + std::to_string(app.imageCount()));

    LogService::instance().shutdown();
    std::cout << "WORKBENCH_TRAINING_PUBLISH_PASS\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    LogService::instance().shutdown();
    return 1;
}
