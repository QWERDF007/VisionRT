#pragma once

#include "core/Tool.h"

#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace visionrt
{

struct PythonImplementation
{
    std::string toolId;
    ToolStatus  status;
};

// Facts reported by the selected interpreter itself. The host never guesses a prefix or a search
// path: the probe process is the single source, and the bridge starts CPython from these values
// instead of re-deriving them from an environment variable or a compile-time default.
struct PythonEnvironment
{
    std::filesystem::path              executable;
    std::filesystem::path              prefix;
    std::filesystem::path              basePrefix;
    // Absolute path of the CPython DLL that the running interpreter itself loaded.
    std::filesystem::path              dll;
    std::vector<std::filesystem::path> searchPaths;
    // "3.12.4" as the interpreter reports it.
    std::string                        version;
    std::string                        numpyVersion;
    // The file the interpreter itself imported NumPy from. A version string alone cannot prove that
    // the selected environment is the one providing it; the path can.
    std::string                        numpyFile;
};

// The one host-side answer to "can Python tools run, and if not, why". VisionRT owns it: the probe
// writes the environment verdict, the backend writes the bridge verdict, and the tool registry, the
// window and the diagnostics all read this single value. VisionTools never reports one.
//
// The text is UTF-8 and deliberately Qt-free: this header reaches translation units that include the
// CPython API, where Qt's keyword macros would rewrite an interpreter structure.
enum class PythonEnvironmentPhase
{
    Unconfigured,  // no interpreter was chosen: a legal setting, not a failure
    Probing,       // one check is running; Python tools stay unavailable until it answers
    Ready,         // the bridge runs the selected interpreter
    Invalid,       // the selected interpreter was rejected; `reason` states exactly why
    BackendFailed, // the interpreter was accepted but the bridge could not start
};

struct PythonEnvironmentStatus
{
    PythonEnvironmentPhase phase = PythonEnvironmentPhase::Unconfigured;
    // What the user selected: a file or a directory, kept for the settings window.
    std::string selected;
    // What the probe resolved and accepted. Empty unless the phase reached Ready.
    std::string executable;
    std::string version;
    // Facts the selected interpreter reported about itself. They are filled only when Ready, and
    // exist so release evidence can name the NumPy the tools will actually import instead of
    // assuming it comes from the environment the user pointed at.
    std::string numpyVersion;
    std::string numpyFile;
    // The sentence the user has to act on. Empty only while Ready.
    std::string reason;

    bool available() const noexcept
    {
        return phase == PythonEnvironmentPhase::Ready;
    }
};

// Stable identifier for the window: the phase never crosses the language boundary as a bare number.
const char *pythonEnvironmentPhaseKey(PythonEnvironmentPhase phase) noexcept;

// CPython reports the paths it was started with in its own spelling: either separator, any case and
// with or without a trailing one. Those are two names for one directory, not two environments — a
// standard venv answers with backslashes even when it was configured with forward ones.
inline std::string normalizedEnvironmentPath(std::string value)
{
    for (auto &character : value)
        character = character == '\\'
                        ? '/'
                        : static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    while (value.size() > 1 && value.back() == '/')
        value.pop_back();
    return value;
}

inline bool environmentPathEquals(const std::string &reported, const std::filesystem::path &expected)
{
    const auto bytes = expected.u8string();
    return normalizedEnvironmentPath(reported)
        == normalizedEnvironmentPath(std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
}

} // namespace visionrt
