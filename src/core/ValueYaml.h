#pragma once

#include "visionrt/contracts/Value.h"
#include <yaml-cpp/yaml.h>

namespace visionrt {

TypeSpec decodeTypeSpec(const YAML::Node& node);
YAML::Node encodeTypeSpec(const TypeSpec& type);
PortValue decodeValue(const YAML::Node& node, const TypeSpec& type);
YAML::Node encodeValue(const PortValue& value, const TypeSpec& type);

} // namespace visionrt
