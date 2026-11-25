#pragma once

#include "ui/Export.h"

#include <filesystem>

namespace visionrt::training {

inline constexpr auto kAssetsDirectory = "assets";

VISIONRT_UI_API std::filesystem::path newTrainingArchivePath(const std::filesystem::path &resourceRoot,
                                                             const std::filesystem::path &fileName);

} // namespace visionrt::training
