#pragma once

#include <QTemporaryDir>
#include <QString>
#include <filesystem>
#include <stdexcept>

namespace visionrt::testing {

class WorkbenchInstallation
{
public:
    WorkbenchInstallation()
    {
        if (!directory_.isValid())
            throw std::runtime_error("Cannot create workbench installation fixture");
        std::filesystem::copy(std::filesystem::path(u8"" VISIONRT_TEST_TOOLS_PACKAGE),
                              std::filesystem::path(directory_.path().toStdU16String()) / "tools-package",
                              std::filesystem::copy_options::recursive);
    }

    QString directory() const
    {
        return directory_.path();
    }

private:
    QTemporaryDir directory_;
};

}
