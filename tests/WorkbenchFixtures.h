#pragma once

#include <QTemporaryDir>
#include <QFile>
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
    QFile file(QString::fromStdWString(fileName.wstring()));
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("example could not be read");
    auto content = file.readAll();
    file.close();
    const auto relativeImages = QString::fromStdWString(images.generic_wstring()).toUtf8();
    content.replace(relativeImages, installation.toUtf8() + '/' + relativeImages);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(content) != content.size())
        throw std::runtime_error("example could not be relocated");
    return file.fileName();
}

inline QString copyCableExample(const std::filesystem::path &source, const QString &installation)
{
    return copyExample(source, installation, std::filesystem::path(u8"examples/03_综合示例/02_识别线缆颜色"),
                       std::filesystem::path(u8"识别线缆颜色.json"));
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
