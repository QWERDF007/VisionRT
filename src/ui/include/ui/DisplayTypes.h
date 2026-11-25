#pragma once

#include "ui/Export.h"

#include <visionrt/contracts/TypeSpec.h>
#include <QString>

namespace visionrt {

// Single source of truth for Chinese UI type descriptions.
VISIONRT_UI_API QString displayDtypeName(DType dtype);
VISIONRT_UI_API QString displayTypeName(const TypeSpec &type);

} // namespace visionrt
