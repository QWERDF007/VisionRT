#pragma once

#include "ui/Export.h"

#include <visionrt/contracts/Value.h>
#include <QString>

namespace visionrt {
inline constexpr int kValueDisplayDecimals = 6;

VISIONRT_UI_API QString displayValueText(const PortValue &value, const TypeSpec &type);
VISIONRT_UI_API QString displayRegionText(const EncodedRegion &region);
} // namespace visionrt
