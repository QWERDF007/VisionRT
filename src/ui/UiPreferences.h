#pragma once

namespace visionrt {

// User-scoped interaction preferences. They never enter the project file, the
// tool declaration or the runtime inputs, and changing them cannot dirty a
// project or rerun a tool.
class UiPreferences
{
public:
    // Smart point selection is on by default so the first installation keeps the
    // automatic behaviour; an explicit user choice survives nodes and restarts.
    static bool easyTouchEnabled();
    static void setEasyTouchEnabled(bool enabled);
};

} // namespace visionrt
