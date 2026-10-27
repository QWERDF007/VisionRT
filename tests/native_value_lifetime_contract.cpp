#include "plugins/PluginLoader.h"
#include "result_view.h"

#include <QTemporaryDir>
#include <cassert>
#include <fstream>
#include <iostream>

using namespace visionrt;

int main(int argc, char **argv)
{
    assert(argc == 2);
    const auto    library = std::filesystem::absolute(argv[1]);
    QTemporaryDir directory;
    assert(directory.isValid());
    for (const auto *port : {"image", "array", "roi"})
    {
        const auto marker = directory.filePath(QString::fromUtf8(port) + ".txt").toUtf8().toStdString();
        auto       loader = std::make_shared<PluginLoader>();
        ToolStatus status;
        auto       module = loader->loadModule(library, status);
        assert(status && module);
        std::weak_ptr<const LoadedModule> alive = module;
        auto instance = std::make_unique<DomainToolAdapter>(module, "test.value.lifetime", PlatformToolServices{});
        assert(instance->initialize({
            {"marker", marker}
        }));
        auto result = instance->execute({});
        assert(testview::status(result) && testview::businessCount(result) == 3);
        PortValue value = result.outputs.at(port);
        PortValue alias = value;
        result.outputs.clear();
        assert(instance->release());
        instance.reset();
        loader.reset();
        module.reset();
        assert(!alive.expired());
#ifdef _WIN32
        assert(GetModuleHandleW(library.c_str()) != nullptr);
#endif
        assert(!std::filesystem::exists(std::filesystem::u8path(marker)));
        if (std::string_view(port) == "image")
        {
            auto &image = std::get<DataRef<Image>>(value).get().at(0);
            assert(image.step == 4 && image.data[0] == 1);
            image.at<std::uint8_t>(1, 2) = 91;
            assert(std::get<DataRef<Image>>(alias).get()[0].at<std::uint8_t>(1, 2) == 91);
        }
        else if (std::string_view(port) == "array")
        {
            std::get<Array>(value).get<NestedVector<std::int64_t, 2>>()[0][0] = 9007199254740993LL;
            assert((std::get<Array>(alias).get<NestedVector<std::int64_t, 2>>()[0][0] == 9007199254740993LL));
        }
        else
        {
            auto &record = std::get<DataRef<ROI>>(value).get()[0][0];
            assert(record[1] == 16777217.125);
            record[1] += 0.5;
            assert(std::get<DataRef<ROI>>(alias).get()[0][0][1] == 16777217.625);
        }
        value = Missing{};
        assert(!alive.expired() && !std::filesystem::exists(std::filesystem::u8path(marker)));
        alias = Missing{};
        assert(alive.expired());
        std::ifstream released(std::filesystem::u8path(marker));
        std::string   message;
        std::getline(released, message);
        assert(message == "released" && released.peek() == std::char_traits<char>::eof());
#ifdef _WIN32
        assert(GetModuleHandleW(library.c_str()) == nullptr);
#endif
    }
    std::cout
        << "Module-created writable Image, Array and ROI outlive adapters and unload after final payload release\n";
}
