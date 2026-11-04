#pragma once

#include <visionrt/contracts/Tool.h>

namespace visionrt::testing::parameters {

// The parameter fixture declares its inputs in this order; the numbers are the identity the
// product carries, so tests name them once here instead of repeating a literal at every call site.
inline constexpr PortId Ratio     = 1;
inline constexpr PortId Count     = 2;
inline constexpr PortId Path      = 3;
inline constexpr PortId Enabled   = 4;
inline constexpr PortId Mode      = 5;
inline constexpr PortId Labels    = 6;
inline constexpr PortId Note      = 7;
inline constexpr PortId Directory = 8;
inline constexpr PortId Weights   = 9;
inline constexpr PortId Images    = 10;
inline constexpr PortId Rois      = 11;

// Business outputs start after the reserved status pair.
inline constexpr PortId RatioOutput = 2;
inline constexpr PortId CountOutput = 3;

} // namespace visionrt::testing::parameters
