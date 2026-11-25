#include "core/Project.h"

namespace visionrt {

PortValue copyProjectLiteral(const PortValue &value)
{
    return std::visit(
        [](const auto &payload) -> PortValue
        {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, Array>)
                return payload.clone();
            else if constexpr (std::is_same_v<Payload, Direction> || std::is_same_v<Payload, CoordinateFrame2D> || isShapeValue<Payload>)
                return payload.clone();
            else if constexpr (std::is_same_v<Payload, DataRef<Image>>)
            {
                Image images;
                for (const auto &image : payload.get()) images.push_back(image.clone());
                return DataRef<Image>(std::move(images));
            }
            else if constexpr (std::is_same_v<Payload, DataRef<TrainingSet>>)
            {
                auto set = payload.get();
                for (auto &[id, image] : set.images)
                    if (!image.pixels.empty())
                        image.pixels = image.pixels.clone();
                for (auto &[id, record] : set.records)
                    for (auto &[field, value] : record.fields) value = copyProjectLiteral(value);
                return DataRef<TrainingSet>(std::move(set));
            }
            else
                return payload;
        },
        value);
}

NodeDefinition copyNodeConfiguration(const NodeDefinition &node)
{
    auto result = node;
    for (auto &[id, value] : result.trainParams) value = copyProjectLiteral(value);
    for (auto &[id, value] : result.initParams) value = copyProjectLiteral(value);
    for (auto &[id, binding] : result.inputs) binding.literal = copyProjectLiteral(binding.literal);
    return result;
}

}
