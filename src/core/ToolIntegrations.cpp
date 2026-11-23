#include "core/ToolIntegrations.h"

#include "core/JsonFields.h"

#include <limits>

namespace visionrt {
namespace {
ParameterLocator readLocator(const QJsonValue &value)
{
    const auto item = json::object(value, "locator");
    json::fields(item,
                 {"algorithm", "image_source", "region_target", "frame_input", "search_input", "search_radius",
                  "candidate_output", "preview_output", "valid_output", "message_output", "seed_input",
                  "alignment_input", "parameters"},
                 "locator");
    const auto id = [](const QJsonObject &owner, const char *key) -> PortId
    {
        const auto number = owner.value(key).toInteger(-1);
        if (!owner.value(key).isDouble() || owner.value(key).toDouble() != number || number < 0
            || number > std::numeric_limits<PortId>::max())
            throw ContractError(std::string("locator 无效端口：") + key);
        return static_cast<PortId>(number);
    };
    ParameterLocator result;
    result.algorithm    = json::text(item.value("algorithm"), "locator.algorithm");
    result.imageSource  = id(item, "image_source");
    result.regionTarget = id(item, "region_target");
    result.frameInput   = id(item, "frame_input");
    result.searchInput  = id(item, "search_input");
    result.searchRadius = item.value("search_radius").toDouble();
    if (!(result.searchRadius > 0))
        throw ContractError("locator.search_radius 必须为正数");
    result.candidateOutput = id(item, "candidate_output");
    result.previewOutput   = id(item, "preview_output");
    result.validOutput     = id(item, "valid_output");
    if (item.contains("seed_input"))
        result.seedInput = id(item, "seed_input");
    if (item.contains("alignment_input"))
        result.alignmentInput = id(item, "alignment_input");
    if (item.contains("message_output"))
        result.messageOutput = id(item, "message_output");
    if (item.contains("parameters"))
        for (const auto &mapping : json::array(item.value("parameters"), "locator.parameters"))
        {
            const auto  row    = json::object(mapping, "locator.parameters");
            const char *target = row.contains("init") ? "init" : "input";
            json::fields(row, {"output", target, "limit"}, "locator.parameters");
            SuggestedLimit limit = SuggestedLimit::Exact;
            if (row.contains("limit"))
            {
                const auto value = json::text(row.value("limit"), "locator.parameters.limit");
                if (value == "minimum")
                    limit = SuggestedLimit::Minimum;
                else if (value == "maximum")
                    limit = SuggestedLimit::Maximum;
                else
                    throw ContractError("未知建议约束：" + value);
            }
            result.parameters.push_back({
                id(row, "output"),
                {row.contains("init") ? PortStage::Init : PortStage::Inputs, id(row, target)},
                limit
            });
        }
    return result;
}

QJsonObject writeLocator(const ParameterLocator &locator)
{
    QJsonObject result{
        {       "algorithm",    QString::fromStdString(locator.algorithm)},
        {    "image_source",     static_cast<qint64>(locator.imageSource)},
        {   "region_target",    static_cast<qint64>(locator.regionTarget)},
        {     "frame_input",      static_cast<qint64>(locator.frameInput)},
        {    "search_input",     static_cast<qint64>(locator.searchInput)},
        {   "search_radius",                         locator.searchRadius},
        {"candidate_output", static_cast<qint64>(locator.candidateOutput)},
        {  "preview_output",   static_cast<qint64>(locator.previewOutput)},
        {    "valid_output",     static_cast<qint64>(locator.validOutput)}
    };
    if (locator.messageOutput)
        result.insert("message_output", static_cast<qint64>(*locator.messageOutput));
    if (locator.seedInput)
        result.insert("seed_input", static_cast<qint64>(*locator.seedInput));
    if (locator.alignmentInput)
        result.insert("alignment_input", static_cast<qint64>(*locator.alignmentInput));
    QJsonArray parameters;
    for (const auto &parameter : locator.parameters)
    {
        const char *target = parameter.target.stage == PortStage::Init ? "init" : "input";
        QJsonObject row{
            {"output", static_cast<qint64>(parameter.sourcePort)},
            {  target,  static_cast<qint64>(parameter.target.id)}
        };
        if (parameter.limit != SuggestedLimit::Exact)
            row.insert("limit", parameter.limit == SuggestedLimit::Minimum ? "minimum" : "maximum");
        parameters.append(row);
    }
    if (!parameters.empty())
        result.insert("parameters", parameters);
    return result;
}
} // namespace

ToolIntegrations readToolIntegrations(const QJsonObject &object)
{
    ToolIntegrations result;
    if (object.contains("processing_locator"))
        result.processing = readLocator(object.value("processing_locator"));
    if (object.contains("training_locator"))
        result.training = readLocator(object.value("training_locator"));
    return result;
}

QJsonObject writeToolIntegrations(const ToolIntegrations &integrations)
{
    QJsonObject result;
    if (integrations.processing)
        result.insert("processing_locator", writeLocator(*integrations.processing));
    if (integrations.training)
        result.insert("training_locator", writeLocator(*integrations.training));
    return result;
}
} // namespace visionrt
