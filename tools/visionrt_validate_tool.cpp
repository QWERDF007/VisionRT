#include "core/ToolPackage.h"
#include "core/ToolRegistry.h"
#include "plugins/PluginLoader.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication   app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VisionRT 工具声明、索引与模块校验"));
    parser.addHelpOption();
    parser.addOption({"package", QStringLiteral("校验完整工具包"), "directory"});
    parser.addOption({"index", QStringLiteral("校验模块索引"), "file"});
    parser.addOption({"emit-normalized-json", QStringLiteral("输出规范化工具声明 JSON"), "file"});
    parser.addOption({"load", QStringLiteral("实际加载并注册原生工厂，不创建实例（需要 --package）")});
    parser.addPositionalArgument("file.json", QStringLiteral("工具声明或 module_index.json"), "[file.json]");
    if (!parser.parse(app.arguments()))
    {
        std::cerr << parser.errorText().toUtf8().toStdString() << '\n';
        return 1;
    }
    if (parser.isSet("help"))
    {
        std::cout << parser.helpText().toUtf8().toStdString();
        return 0;
    }
    const bool packageMode    = parser.isSet("package");
    const bool indexMode      = parser.isSet("index");
    const bool normalizedMode = parser.isSet("emit-normalized-json");
    const bool load           = parser.isSet("load");
    const auto positional     = parser.positionalArguments();
    const int  modes          = int(packageMode) + int(indexMode) + int(normalizedMode);
    if (modes > 1 || (modes == 1 && !positional.empty()) || (modes == 0 && positional.size() != 1)
        || (load && !packageMode))
    {
        std::cerr << parser.helpText().toUtf8().toStdString();
        return 1;
    }
    const auto target = packageMode    ? parser.value("package")
                      : indexMode      ? parser.value("index")
                      : normalizedMode ? parser.value("emit-normalized-json")
                                       : positional.front();
    const auto path   = std::filesystem::u8path(target.toUtf8().toStdString());
    try
    {
        if (packageMode)
        {
            visionrt::validateToolsPackage(path);
            if (load)
            {
                visionrt::ToolRegistry registry;
                registry.setPluginLoader(std::make_shared<visionrt::PluginLoader>());
                registry.registerPackage(visionrt::readToolsPackage(path));
                registry.loadNativeImplementations();
                bool available = true;
                for (const auto &definition : registry.definitions())
                {
                    if (definition.kind != visionrt::ToolKind::Cpp)
                        continue;
                    const auto state = registry.availability(definition.id);
                    if (state.state == visionrt::ToolAvailability::Available)
                        std::cout << "Native factory available: " << definition.id << '\n';
                    else
                    {
                        available = false;
                        std::cerr << "Native factory unavailable: " << definition.id << ": " << state.message << '\n';
                        if (!state.detail.empty())
                            std::cerr << state.detail << '\n';
                    }
                }
                if (!available)
                    return 2;
            }
            std::cout << "Valid tools package: " << target.toUtf8().toStdString() << '\n';
        }
        else if (indexMode || (!normalizedMode && path.filename() == "module_index.json"))
        {
            const auto index = visionrt::parseModuleIndexFile(path);
            std::cout << "Valid module index: " << index.tools.size() << " tools across " << index.modules.size()
                      << " modules\n";
        }
        else if (normalizedMode)
            std::cout << visionrt::emitNormalizedToolJson(visionrt::parseToolDefinitionFile(path));
        else
        {
            const auto definition = visionrt::parseToolDefinitionFile(path);
            std::cout << "Valid tool definition: " << definition.id << " (" << definition.displayName << ")\n";
        }
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Validation error: " << error.what() << '\n';
        return 2;
    }
}
