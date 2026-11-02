#pragma once

#include "common/Version.h"

#include <QString>

#include <optional>

namespace visionrt {

// The about panel is pure presentation: the caller has already resolved every component's build
// metadata, this value is what the text is rendered from. No lifetime, so no manager class.
struct AboutReport
{
    // Compile-time version/commit plus the built-at stamp written after linking.
    std::optional<ComponentBuildInfo> host;
    bool                              toolsInstalled = false;
    std::optional<ComponentBuildInfo> tools;
    ComponentBuildInfo                contracts{};
    std::optional<ComponentBuildInfo> contractsBuild;
    bool                              pythonAvailable = false;
};

// Renders the three component blocks and the runtime block. A missing field reads 未知; no component
// borrows another component's build time.
QString aboutReportText(const AboutReport &report);

} // namespace visionrt
