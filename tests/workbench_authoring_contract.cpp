// Contract for ticket 02: the workbench binds a tool's authoring.easy_touch capability by what the
// declaration says. A probe tool and its helper use business numbers no production tool uses for
// the same roles, so a success proves the host resolves ports from the declaration instead of
// looking up a tool name or the first ROI input.
#include "TestPythonConfiguration.h"
#include "WorkbenchFixtures.h"
#include "log/LogService.h"
#include "ui/AppController.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace visionrt;

namespace {

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

// ReadImage: 图像路径 1, 图像数量 5.
// ProbeMeasure: 图像 21, 测量区域 34, 半搜索宽度 47, 边缘极性 12, 标签 init 0.
constexpr PortId kPathInput    = 1;
constexpr PortId kCountInput   = 5;
constexpr PortId kImageInput   = 21;
constexpr PortId kRoiInput     = 34;
constexpr PortId kWidthInput   = 47;
constexpr PortId kPolarInput   = 12;
constexpr PortId kTagParam     = 0;
constexpr PortId kImagesOutput = 2;
constexpr PortId kRoiOutput    = 36;

const char *kMeasureDeclaration = R"json({
  "schema_version": 2,
  "name_en": "ProbeMeasure",
  "name_zh": "探针测量",
  "version": "1.0.0",
  "implementation": {
    "kind": "cpp"
  },
  "init_params": [
    {
      "id": 0,
      "name_zh": "标签",
      "type": "string",
      "default": "初始",
      "ui": {
        "editor": "text"
      }
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
      "id": 34,
      "name_zh": "测量区域",
      "type": "roi",
      "default": [],
      "ui": {
        "editor": "array"
      },
      "relations": {
        "image": {
          "stage": "inputs",
          "id": 21
        }
      }
    },
    {
      "id": 55,
      "name_zh": "参考变换",
      "type": {
        "type": "array",
        "dtype": "float64",
        "rank": 3
      },
      "ui": {
        "editor": "array"
      }
    },
    {
      "id": 47,
      "name_zh": "半搜索宽度",
      "type": "float",
      "default": 8.0,
      "constraints": {
        "min": 0.5,
        "max": 4096.0
      },
      "ui": {
        "editor": "number",
        "decimals": 2
      }
    },
    {
      "id": 12,
      "name_zh": "边缘极性",
      "type": "string",
      "default": "any",
      "constraints": {
        "options": [
          "any",
          "dark_to_light",
          "light_to_dark"
        ]
      },
      "ui": {
        "editor": "text"
      }
    }
  ],
  "outputs": [
    {
      "id": 2,
      "name_zh": "计数",
      "type": "int"
    },
    {
      "id": 36,
      "name_zh": "测量区域",
      "type": "roi",
      "relations": {
        "image": {
          "stage": "inputs",
          "id": 21
        }
      }
    }
  ]
})json";

const char *kPickDeclaration = R"json({
  "schema_version": 2,
  "name_en": "ProbePick",
  "name_zh": "探针点选",
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
      "name_zh": "图像",
      "type": "image",
      "display": {
        "visible": true
      }
    },
    {
      "id": 2,
      "name_zh": "搜索区域",
      "type": "roi",
      "ui": {
        "editor": "array"
      }
    },
    {
      "id": 4,
      "name_zh": "已有区域",
      "type": "roi",
      "default": [],
      "ui": {
        "editor": "array"
      }
    },
    {
      "id": 5,
      "name_zh": "参考变换",
      "type": {
        "type": "array",
        "dtype": "float64",
        "rank": 3
      },
      "ui": {
        "editor": "array"
      }
    },
    {
      "id": 7,
      "name_zh": "半径",
      "type": "int",
      "default": 18,
      "constraints": {
        "min": 1,
        "max": 400
      }
    },
    {
      "id": 8,
      "name_zh": "标签",
      "type": "string",
      "default": ""
    }
  ],
  "outputs": [
    {
      "id": 30,
      "name_zh": "预览",
      "type": "roi"
    },
    {
      "id": 31,
      "name_zh": "候选",
      "type": "roi"
    },
    {
      "id": 32,
      "name_zh": "有效",
      "type": "bool"
    },
    {
      "id": 33,
      "name_zh": "说明",
      "type": "string"
    },
    {
      "id": 34,
      "name_zh": "建议半宽",
      "type": "float"
    },
    {
      "id": 35,
      "name_zh": "建议极性",
      "type": "string"
    },
    {
      "id": 36,
      "name_zh": "标签",
      "type": "string"
    }
  ]
})json";

QVariantMap rowOf(const QVariantList &rows, PortId id, bool init)
{
    for (const auto &value : rows)
        if (value.toMap().value("id").toUInt() == id && value.toMap().value("init").toBool() == init)
            return value.toMap();
    return {};
}

double numericValue(const QString &text)
{
    bool         ok    = false;
    const double value = text.toDouble(&ok);
    if (!ok)
        throw std::runtime_error("probe parameter is not numeric: " + text.toStdString());
    return value;
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

// Adds the probe pair to the copied installation exactly as a tool package would ship them: a module
// entry, two declarations, and the library on the package's own bin path.
void installProbe(const QString &root, const QString &probeDll)
{
    const QString package = root + "/tools-package";
    QDir().mkpath(package + "/bin");
    const QString library = package + "/bin/test_authoring_probe.dll";
    QFile::remove(library);
    if (!QFile::copy(probeDll, library))
        throw std::runtime_error("probe library could not be copied into the installation");
    writeText(package + "/tools/builtin/探针/探针测量.json", kMeasureDeclaration);
    writeText(package + "/tools/builtin/探针/探针点选.json", kPickDeclaration);

    const QString index   = package + "/module_index.json";
    auto          object  = readObject(index);
    auto          modules = object.value("modules").toArray();
    modules.append(QJsonObject{
        {     "id",         "test_authoring_probe"},
        {"library", "bin/test_authoring_probe.dll"}
    });
    auto tools = object.value("tools").toArray();
    tools.append(QJsonObject{
        {   "tool_id",                           "ProbeMeasure"},
        {    "module",                   "test_authoring_probe"},
        {"definition", "tools/builtin/探针/探针测量.json"}
    });
    tools.append(QJsonObject{
        {   "tool_id",                              "ProbePick"},
        {    "module",                   "test_authoring_probe"},
        {"definition", "tools/builtin/探针/探针点选.json"}
    });
    for (int i = 0; i < tools.size(); ++i)
    {
        auto entry = tools[i].toObject();
        if (entry.value("tool_id") == "ProbeMeasure")
        {
            entry["processing_locator"]
                = QJsonDocument::fromJson(
                      R"json({"algorithm": "ProbePick", "image_source": 21, "region_target": 34, "frame_input": 1, "search_input": 2, "search_radius": 128, "candidate_output": 31, "preview_output": 30, "valid_output": 32, "message_output": 33, "seed_input": 4, "alignment_input": 5, "parameters": [{"output": 34, "input": 47}, {"output": 35, "input": 12}, {"output": 36, "init": 0}]})json")
                      .object();
            tools[i] = entry;
        }
    }
    object["modules"] = modules;
    object["tools"]   = tools;
    writeObject(index, object);
}

// Points one declared call input at a helper port that does not exist, keeping the rest intact.
void breakHelperPort(const QString &root, int missing)
{
    const QString path   = root + "/tools-package/module_index.json";
    auto          object = readObject(path);
    auto          tools  = object.value("tools").toArray();
    for (int i = 0; i < tools.size(); ++i)
    {
        auto entry = tools[i].toObject();
        if (entry.value("tool_id") == "ProbeMeasure")
        {
            auto locator                = entry.value("processing_locator").toObject();
            locator["seed_input"]       = missing;
            entry["processing_locator"] = locator;
            tools[i]                    = entry;
        }
    }
    object["tools"] = tools;
    writeObject(path, object);
}

QImage dots(const QList<QPointF> &centers, const QList<double> &radii)
{
    QImage image(320, 240, QImage::Format_Grayscale8);
    image.fill(20);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(220, 220, 220));
    for (int index = 0; index < centers.size(); ++index)
        painter.drawEllipse(centers[index], radii[index], radii[index]);
    return image;
}
} // namespace

int main(int argc, char **argv)
try
{
    testsupport::useBuildInterpreter();
    QGuiApplication                application(argc, argv);
    testing::WorkbenchInstallation installation;
    const auto                     root   = installation.directory();
    const auto                     images = root + "/authoring-images";
    require(QDir().mkpath(images), "authoring image directory could not be created");
    require(dots(
                {
                    {160.0, 120.0},
                    { 60.0,  60.0}
    },
                {40.0, 30.0})
                .save(images + "/0.png"),
            "authoring image could not be written");
    installProbe(root, QString::fromUtf8(VISIONRT_TEST_AUTHORING_PROBE));

    require(LogService::instance().start({}, root + "/logs/authoring.log"), "authoring log did not start");
    AppController app(2, root);
    const auto    settle = [&]
    {
        require(QTest::qWaitFor([&] { return !app.busy(); }, 20000), "authoring operation did not settle");
    };
    const auto stable = [&]
    {
        settle();
        QTest::qWait(200);
        settle();
    };
    require(QTest::qWaitFor([&] { return !app.busy(); }, 10000), "authoring catalog did not become ready");
    app.rescanTools();
    settle();

    app.addNode("ReadImage", 0, 0);
    settle();
    const auto reader = app.selectedNode();
    require(!reader.isEmpty(), "authoring reader node is missing");
    require(app.setParameter(kCountInput, false, "2"), "authoring image count was rejected");
    require(app.setParameterPath(kPathInput, false, QUrl::fromLocalFile(images)), "authoring image path was rejected");
    settle();

    app.addNode("ProbeMeasure", 0, 4);
    settle();
    const auto probe = app.selectedNode();
    require(!probe.isEmpty(), "probe node is missing: " + app.diagnostic().toStdString());
    require(app.bindInput(app.portContext(kImageInput), reader, kImagesOutput), "probe image input could not be bound");
    settle();
    if (!app.save(root + "/authoring.json"))
        throw std::runtime_error("probe project could not be saved: " + app.diagnostic().toStdString());
    stable();
    app.runOnce();
    settle();
    app.setImageOutput(QStringLiteral("inputs:%1").arg(kImageInput));
    require(app.selectedImageOutput() == QStringLiteral("inputs:%1").arg(kImageInput),
            "the probe image could not be selected");
    stable();

    require(QTest::qWaitFor([&] { return app.assistActive(); }, 5000),
            "declared point selection did not become available: " + app.assistDisabledReason().toStdString());

    // Hover previews only; nothing is written and the project stays clean.
    const auto before = app.parameters();
    require(app.assistHover(160.0, 120.0), "probe hover was rejected: " + app.assistDisabledReason().toStdString());
    require(QTest::qWaitFor([&] { return !app.assistPreview().isEmpty(); }, 5000), "probe hover produced no candidate");
    require(!app.dirty(), "a hover preview must not dirty the project");
    const auto preview = app.assistPreview().front().toList();
    require(preview.size() == 4 && static_cast<int>(preview.at(0).toDouble()) == 3
                && std::abs(preview.at(1).toDouble() - 160.0) <= 1.5,
            "probe candidate is not the helper's circle");

    // One click writes every declared target in one transaction, including an initialization value.
    QSignalSpy projectChanges(&app, &AppController::projectChanged);
    require(app.assistClick(160.0, 120.0, false),
            "probe click was rejected: " + app.assistDisabledReason().toStdString());
    require(QTest::qWaitFor([&] { return app.dirty(); }, 10000), "probe click did not commit a value");
    stable();
    require(projectChanges.count() == 1, "a declared proposal must raise exactly one project transaction");
    const auto rows = app.parameters();
    require(rowOf(rows, kRoiInput, false).value("roiCount").toInt() == 1, "the declared ROI target was not written");
    require(std::abs(numericValue(rowOf(rows, kWidthInput, false).value("literal").toString()) - 12.5) < 1e-9,
            "the declared float target was not written");
    require(rowOf(rows, kPolarInput, false).value("valueText").toString() == "dark_to_light",
            "the declared string target was not written");
    require(rowOf(rows, kTagParam, true).value("valueText").toString() == "已选",
            "the declared initialization target was not written");
    require(rowOf(rows, 55, false).value("literal").toString() == rowOf(before, 55, false).value("literal").toString(),
            "an undeclared target changed during the proposal");

    const auto beforeInvalid = app.parameters();
    QSignalSpy invalidChanges(&app, &AppController::projectChanged);
    require(app.assistClick(260, 120, false), "the invalid business suggestion could not be evaluated");
    stable();
    require(invalidChanges.count() == 0 && app.parameters() == beforeInvalid,
            "an out-of-range business suggestion must reject the complete proposal");

    // A declaration that names a helper port the helper no longer has is reported with both names
    // and the port, and the committed values stay untouched.
    const auto committed = app.parameters();
    breakHelperPort(root, 99);
    app.rescanTools();
    stable();
    app.selectNode(probe);
    stable();
    require(!app.assistActive(), "a declaration with a missing helper port must not take over the canvas");
    require(app.assistDisabledReason().contains(QStringLiteral("缺失"))
                && app.assistDisabledReason().contains(QStringLiteral("99"))
                && app.assistDisabledReason().contains(QStringLiteral("ProbePick")),
            "the missing helper port was not named with the tool and the port: "
                + app.assistDisabledReason().toStdString());
    require(!app.assistClick(160.0, 120.0, false), "a click with a missing helper port must be refused");
    require(rowOf(app.parameters(), kWidthInput, false).value("literal")
                == rowOf(committed, kWidthInput, false).value("literal"),
            "a refused click must not change a parameter");

    // The restored declaration is the only thing that changed: the same click succeeds again.
    breakHelperPort(root, 4);
    app.rescanTools();
    stable();
    app.selectNode(probe);
    stable();
    require(
        QTest::qWaitFor([&] { return app.assistActive(); }, 5000),
        "point selection did not return after the declaration was fixed: " + app.assistDisabledReason().toStdString());
    require(app.setParameter(kWidthInput, false, "8.0"), "resetting the half width was rejected");
    settle();
    require(app.assistClick(160.0, 120.0, false), "the fixed declaration was not usable");
    require(
        QTest::qWaitFor(
            [&]
            {
                return std::abs(numericValue(rowOf(app.parameters(), kWidthInput, false).value("literal").toString())
                                - 12.5)
                     < 1e-9;
            },
            10000),
        "the fixed declaration did not write its target");

    // A target already fed from upstream is refused at the click. The preview stays available
    // because a hover writes nothing: looking is safe, committing is not, and the user still sees
    // the candidate that explains why the click did nothing.
    app.addNode("ProbeMeasure", 0, 8);
    settle();
    const auto downstream = app.selectedNode();
    require(!downstream.isEmpty(), "downstream probe node is missing");
    require(app.bindInput(app.portContext(kImageInput), reader, kImagesOutput),
            "downstream image input could not be bound");
    settle();
    require(app.bindInput(app.portContext(kRoiInput), probe, kRoiOutput), "upstream ROI binding was rejected");
    settle();
    app.runOnce();
    settle();
    app.setImageOutput(QStringLiteral("inputs:%1").arg(kImageInput));
    stable();
    require(!app.assistClick(160.0, 120.0, false), "a click into a target fed from upstream must be refused");

    std::cout << "WORKBENCH_AUTHORING_PASS preview=1 click=roi+float+string+init transaction=1 "
                 "missing_port=reported upstream_target=refused\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << "[Authoring error] " << error.what() << '\n';
    return 1;
}
