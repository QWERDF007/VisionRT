#include "core/Project.h"

namespace visionrt {

PortValue copyProjectLiteral(const PortValue &value)
{
    return std::visit(
        [](const auto &payload) -> PortValue
        {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, Array>)
                return payload.visit([](const auto &data) { return Array(data.get()); });
            else if constexpr (std::is_same_v<Payload, DataRef<ROI>>)
                return DataRef<ROI>(payload.get());
            else if constexpr (std::is_same_v<Payload, DataRef<Image>>)
            {
                if (!payload.get().empty())
                    throw ContractError("工程图像字面量只能是 Missing 或空列表");
                return DataRef<Image>(Image{});
            }
            else
                return payload;
        },
        value);
}

NodeDefinition copyNodeConfiguration(const NodeDefinition &node)
{
    auto result = node;
    for (auto &[id, value] : result.initParams) value = copyProjectLiteral(value);
    for (auto &[id, binding] : result.inputs) binding.literal = copyProjectLiteral(binding.literal);
    return result;
}

}
