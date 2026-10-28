#include "WorkbenchFixtures.h"
#include "core/DefinitionJson.h"
#include "ui/AppController.h"
#include "ui/OutputModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTest>
#include <iostream>
#include <stdexcept>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

QString text(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly | QIODevice::Text), "hot-path evidence file unavailable");
    return QString::fromUtf8(file.readAll()).trimmed();
}
}

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    testing::WorkbenchInstallation installation;
    const auto                     root = installation.directory();
    const auto                     write = [&](const QString &path, const QByteArray &bytes)
    {
        require(QDir().mkpath(QFileInfo(path).absolutePath()), "hot-path fixture parent unavailable");
        QFile file(path);
        require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "hot-path fixture write failed");
    };
    const auto package = root + "/tools-package/";
    const auto script  = package + "python/custom/hotpath/tool.py";
    const auto json    = package + "tools/custom/hotpath/source.json";
    ToolDefinition definition;
    definition.id          = "test.hotpath";
    definition.displayName = "热路径";
    definition.kind        = ToolKind::PythonClass;
    definition.entry       = {"hotpath", "tool.py", "Counter"};
    definition.outputs     = {
        { "value", "值", {Type::Int}}
    };
    write(json, QByteArray::fromStdString(emitNormalizedToolJson(definition)));
    write(script, R"py(from pathlib import Path

root = Path(__file__).parent

def bump(name):
    path = root / name
    value = int(path.read_text()) + 1 if path.exists() else 1
    path.write_text(str(value))
    return value

class Counter:
    def initialize(self, parameters):
        bump('init')
        self.count = 0
        return {'code': 0, 'message': '', 'detail': ''}

    def execute(self, inputs):
        self.count += 1
        bump('exec')
        return {'outputs': {'value': self.count, '__status_code': 0, '__status_message': ''}, 'detail': ''}

    def release(self):
        return {'code': 0, 'message': '', 'detail': ''}
)py");
    AppController app(2, root);
    const auto    idle = [&]
    {
        require(QTest::qWaitFor([&] { return !app.busy(); }, 10000), "hot-path operation did not finish");
    };
    idle();
    auto *outputs = qobject_cast<OutputModel *>(app.outputModel());
    require(outputs, "hot-path output model unavailable");
    app.addNode("test.hotpath", 0, 0);
    idle();
    app.runNode();
    idle();
    const auto first = outputs->capture("value");
    require(first && OutputModel::fullText(*first) == "1", "hot-path first execution failed");
    require(text(package + "python/custom/hotpath/init") == "1" && text(package + "python/custom/hotpath/exec") == "1",
            "hot-path first execution did not initialize once");
    require(QFile::remove(json) && QFile::remove(package + "module_index.json") && QFile::remove(script),
            "hot-path fixture could not remove cached declarations");
    require(!QFile::exists(json) && !QFile::exists(script), "hot-path declarations are still readable");
    app.runNode();
    idle();
    const auto repeated = outputs->capture("value");
    require(repeated && OutputModel::fullText(*repeated) == "2", "hot-path repeated execution failed");
    require(text(package + "python/custom/hotpath/init") == "1", "hot-path execution re-initialized the instance");
    require(text(package + "python/custom/hotpath/exec") == "2", "hot-path execution did not reuse the instance");
    require(app.nodes().first().toMap().value("state").toString() == "succeeded",
            "hot-path repeated execution did not succeed");
    std::cout << "HOTPATH_CONTRACT_PASS init=1 exec=2 json_removed=1 script_removed=1\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
