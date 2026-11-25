#include "core/ProjectArchive.h"

#include <visionrt/contracts/ZipArchive.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace visionrt {

QByteArray ProjectArchive::readProgramJson(const std::filesystem::path &path)
{
    const auto bytes = ZipArchive::readEntry(path, kProgramEntry);
    return QByteArray(reinterpret_cast<const char *>(bytes.data()), static_cast<qsizetype>(bytes.size()));
}

void ProjectArchive::writeProgramJson(const std::filesystem::path &path, const QByteArray &json)
{
    const auto parent = path.parent_path();
    if (!parent.empty() && !std::filesystem::is_directory(parent))
        throw std::runtime_error("工程保存目录不存在：" + parent.string());
    const auto *begin = reinterpret_cast<const std::uint8_t *>(json.constData());
    ZipArchive::write(path, {{kProgramEntry, std::vector<std::uint8_t>(begin, begin + json.size())}});
}

} // namespace visionrt
