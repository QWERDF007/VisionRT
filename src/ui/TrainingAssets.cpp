#include "ui/TrainingAssets.h"

namespace visionrt::training {

std::filesystem::path newTrainingArchivePath(const std::filesystem::path &resourceRoot,
                                             const std::filesystem::path &fileName)
{
    const auto directory = resourceRoot / kAssetsDirectory;
    auto       path      = directory / fileName;
    for (int suffix = 2; std::filesystem::exists(path); ++suffix)
        path = directory
             / (fileName.stem().wstring() + L"_" + std::to_wstring(suffix) + fileName.extension().wstring());
    return path.lexically_normal();
}

} // namespace visionrt::training
