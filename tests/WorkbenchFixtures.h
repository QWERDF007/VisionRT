#pragma once

#include <QTemporaryDir>
#include <QString>
#include <filesystem>
#include <stdexcept>

namespace visionrt::testing {

// Copies the installed tools package for a test installation. The GPU inference runtime is skipped:
// the workbench contracts only load declarations and CPU tools, while those libraries cost gigabytes
// per test and turn a temporary directory into a disk-space failure.
inline void copyToolsPackage(const std::filesystem::path &source, const std::filesystem::path &target)
{
    std::filesystem::create_directories(target);
    for (const auto &entry : std::filesystem::directory_iterator(source))
    {
        const auto name = entry.path().filename().string();
        const auto dest = target / entry.path().filename();
        if (entry.is_directory())
            copyToolsPackage(entry.path(), dest);
        else if (name.rfind("nvinfer", 0) != 0)
            std::filesystem::copy_file(entry.path(), dest, std::filesystem::copy_options::overwrite_existing);
    }
}

class WorkbenchInstallation
{
public:
    WorkbenchInstallation()
    {
        if (!directory_.isValid())
            throw std::runtime_error("Cannot create workbench installation fixture");
        copyToolsPackage(std::filesystem::path(u8"" VISIONRT_TEST_TOOLS_PACKAGE),
                         std::filesystem::path(directory_.path().toStdU16String()) / "tools-package");
    }

    QString directory() const
    {
        return directory_.path();
    }

private:
    QTemporaryDir directory_;
};

}
