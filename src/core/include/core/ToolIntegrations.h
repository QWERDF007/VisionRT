#pragma once

#include "core/Export.h"
#include "visionrt/contracts/Declaration.h"

#include <optional>
#include <vector>

class QJsonObject;

namespace visionrt {
enum class SuggestedLimit
{
    Exact,
    Minimum,
    Maximum
};

struct ParameterSuggestion
{
    PortId         sourcePort;
    PortRef        target;
    SuggestedLimit limit                                         = SuggestedLimit::Exact;
    bool           operator==(const ParameterSuggestion &) const = default;
};

// A package can offer a geometric locator for an existing node input or training record.
// Port identities refer to the locator unless explicitly named source or target.
struct ParameterLocator
{
    std::string                      algorithm;
    PortId                           imageSource{}, regionTarget{}, frameInput{}, searchInput{};
    double                           searchRadius{};
    PortId                           candidateOutput{}, previewOutput{}, validOutput{};
    std::optional<PortId>            messageOutput, seedInput, alignmentInput;
    std::optional<PortId>            datasetParameter;
    std::vector<ParameterSuggestion> parameters;
    bool                             operator==(const ParameterLocator &) const = default;
};

struct ToolIntegrations
{
    std::optional<ParameterLocator> processing, training;
    bool                            operator==(const ToolIntegrations &) const = default;
};

VISIONRT_CORE_API ToolIntegrations readToolIntegrations(const QJsonObject &object);
VISIONRT_CORE_API QJsonObject      writeToolIntegrations(const ToolIntegrations &integrations);
} // namespace visionrt
