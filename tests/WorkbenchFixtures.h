#pragma once

#include "ProjectFixtures.h"

#include <QString>
#include <QTemporaryDir>
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

inline QString copyExample(const std::filesystem::path &source, const QString &installation,
                           const std::filesystem::path &sample, const std::filesystem::path &projectFile)
{
    const auto root = std::filesystem::path(installation.toStdWString());
    std::filesystem::create_directories((root / sample).parent_path());
    std::filesystem::copy(source / sample, root / sample, std::filesystem::copy_options::recursive);
    const auto images = std::filesystem::path("examples/images") / projectFile.stem();
    std::filesystem::create_directories((root / images).parent_path());
    std::filesystem::copy(source / images, root / images, std::filesystem::copy_options::recursive);
    const auto fileName = root / sample / projectFile;
    auto       content  = ProjectArchive::readProgramJson(fileName);
    const auto relativeImages = QString::fromStdWString(images.generic_wstring()).toUtf8();
    content.replace(relativeImages, installation.toUtf8() + '/' + relativeImages);
    const auto relativeSample = QString::fromStdWString(sample.generic_wstring()).toUtf8();
    content.replace(relativeSample, installation.toUtf8() + '/' + relativeSample);
    ProjectArchive::writeProgramJson(fileName, content);
    return QString::fromStdWString(fileName.generic_wstring());
}

inline QString copyCableExample(const std::filesystem::path &source, const QString &installation)
{
    return copyExample(source, installation, std::filesystem::path(u8"examples/综合示例/识别线缆颜色"),
                       std::filesystem::path(u8"识别线缆颜色.vrtapp"));
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

} // namespace visionrt::testing
