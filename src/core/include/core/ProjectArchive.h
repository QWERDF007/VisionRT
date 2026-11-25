#pragma once

#include "core/Export.h"

#include <QByteArray>

#include <filesystem>

namespace visionrt {

class VISIONRT_CORE_API ProjectArchive
{
public:
    static constexpr const char *kProgramEntry = "program.json";

    static QByteArray readProgramJson(const std::filesystem::path &path);
    static void       writeProgramJson(const std::filesystem::path &path, const QByteArray &json);
};

} // namespace visionrt
