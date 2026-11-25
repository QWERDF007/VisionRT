#pragma once

#include "core/ProjectArchive.h"

#include <QDir>
#include <QString>
#include <filesystem>
#include <stdexcept>

namespace visionrt::testing {

inline QString copyProjectFixture(const std::filesystem::path &source, const QString &directory,
                                  const std::filesystem::path &projectFile)
{
    auto content = ProjectArchive::readProgramJson(source / "tests/fixtures/projects" / projectFile);
    const auto images = QString::fromStdWString((source / "tests/fixtures/images").generic_wstring()).toUtf8();
    content.replace("tests/fixtures/images/", images + '/');
    if (!QDir().mkpath(directory))
        throw std::runtime_error("project fixture directory could not be created");
    const auto target = std::filesystem::path(directory.toStdWString()) / projectFile;
    ProjectArchive::writeProgramJson(target, content);
    return QString::fromStdWString(target.generic_wstring());
}

} // namespace visionrt::testing
