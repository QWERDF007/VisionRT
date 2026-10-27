#pragma once

#include <visionrt/contracts/TypeSpec.h>
#include <QString>

namespace visionrt {

// Single source of truth for Chinese UI type descriptions.
QString displayTypeName(const TypeSpec &type);

} // namespace visionrt
