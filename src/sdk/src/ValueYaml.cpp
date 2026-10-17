#include "visionrt/sdk/ValueYaml.h"

#include <array>
#include <cmath>
#include <limits>
#include <set>

namespace visionrt {
namespace {
constexpr std::array typeNames{"Int", "Float", "Bool", "String", "ImageBatch", "NumericArray", "StringArray", "RoiBatch"};
constexpr std::array dtypeNames{"uint8", "int32", "int64", "float32", "float64", "bool"};

void mapping(const YAML::Node& node, const char* description) {
    if (!node.IsMap()) throw ContractError(std::string(description) + " must be a mapping");
    std::set<std::string> keys;
    for (const auto& field : node) {
        if (!field.first.IsScalar() || !keys.insert(field.first.Scalar()).second)
            throw ContractError(std::string(description) + " contains a non-scalar or duplicate key");
    }
}
void sequence(const YAML::Node& node, const char* description) {
    if (!node.IsSequence()) throw ContractError(std::string(description) + " must be a sequence");
}
std::string scalar(const YAML::Node& node, const char* description) {
    if (!node.IsScalar()) throw ContractError(std::string(description) + " must be a scalar");
    return node.Scalar();
}
template<std::size_t N> std::size_t named(const YAML::Node& node,
    const std::array<const char*, N>& names, const char* description) {
    const auto value = scalar(node, description);
    for (std::size_t i = 0; i < names.size(); ++i)
        if (value == names[i]) return i;
    throw ContractError(std::string("Unknown ") + description + ": " + value);
}
std::int64_t integer(const YAML::Node& node) { return parseInt64(scalar(node, "Integer")); }

template<class T> T number(const YAML::Node& node) {
    if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
        const auto value = integer(node);
        if (value < std::numeric_limits<T>::min() || value > std::numeric_limits<T>::max())
            throw ContractError("Numeric element is outside the declared dtype range");
        return static_cast<T>(value);
    } else {
        (void)scalar(node, "Numeric element");
        try { return node.as<T>(); }
        catch (const YAML::Exception& error) { throw ContractError(error.what()); }
    }
}
std::vector<std::int64_t> shape(const YAML::Node& node, int rank) {
    mapping(node, "Array buffer");
    for (const auto& field : node) {
        const auto key = field.first.Scalar();
        if (key != "shape" && key != "data") throw ContractError("Unknown array buffer field: " + key);
    }
    sequence(node["shape"], "Array shape");
    sequence(node["data"], "Array data");
    if (node["shape"].size() != static_cast<std::size_t>(rank))
        throw ContractError("Array shape does not match its declared rank");
    std::vector<std::int64_t> dimensions;
    dimensions.reserve(node["shape"].size());
    for (const auto& dimension : node["shape"]) dimensions.push_back(integer(dimension));
    if (checkedElementCount(dimensions) != node["data"].size())
        throw ContractError("Array data length does not match shape");
    return dimensions;
}
template<class T> NumericBuffer numericBuffer(const YAML::Node& node, DType dtype, int rank) {
    auto dimensions = shape(node, rank);
    (void)checkedByteCount(dimensions, sizeof(T));
    const auto count = node["data"].size();
    auto values = std::make_unique_for_overwrite<T[]>(count);
    for (std::size_t i = 0; i < count; ++i) values[i] = number<T>(node["data"][i]);
    return NumericBuffer::copy(dtype, std::move(dimensions), std::as_bytes(std::span(values.get(), count)));
}
NumericBuffer decodeNumericBuffer(const YAML::Node& node, DType dtype, int rank) {
    switch (dtype) {
    case DType::UInt8: return numericBuffer<std::uint8_t>(node, dtype, rank);
    case DType::Int32: return numericBuffer<std::int32_t>(node, dtype, rank);
    case DType::Int64: return numericBuffer<std::int64_t>(node, dtype, rank);
    case DType::Float32: return numericBuffer<float>(node, dtype, rank);
    case DType::Float64: return numericBuffer<double>(node, dtype, rank);
    case DType::Bool: return numericBuffer<bool>(node, dtype, rank);
    }
    throw ContractError("Unknown numeric dtype");
}
StringBuffer decodeStringBuffer(const YAML::Node& node, int rank) {
    auto dimensions = shape(node, rank);
    std::vector<std::string> values;
    values.reserve(node["data"].size());
    for (const auto& value : node["data"]) values.push_back(scalar(value, "String element"));
    return StringBuffer(std::move(dimensions), std::move(values));
}
template<class T> YAML::Node encodeElements(std::span<const T> values) {
    YAML::Node data(YAML::NodeType::Sequence);
    for (const auto& value : values) {
        if constexpr (std::is_same_v<T, std::uint8_t>) data.push_back(static_cast<unsigned int>(value));
        else data.push_back(value);
    }
    return data;
}
YAML::Node encodeNumericBuffer(const NumericBuffer& buffer) {
    YAML::Node node(YAML::NodeType::Map);
    node["shape"] = encodeElements(buffer.shape());
    switch (buffer.dtype()) {
    case DType::UInt8: node["data"] = encodeElements(buffer.values<std::uint8_t>()); break;
    case DType::Int32: node["data"] = encodeElements(buffer.values<std::int32_t>()); break;
    case DType::Int64: node["data"] = encodeElements(buffer.values<std::int64_t>()); break;
    case DType::Float32: node["data"] = encodeElements(buffer.values<float>()); break;
    case DType::Float64: node["data"] = encodeElements(buffer.values<double>()); break;
    case DType::Bool: node["data"] = encodeElements(buffer.values<bool>()); break;
    }
    return node;
}
YAML::Node encodeStringBuffer(const StringBuffer& buffer) {
    YAML::Node node(YAML::NodeType::Map);
    node["shape"] = encodeElements(buffer.shape());
    node["data"] = encodeElements(buffer.values());
    return node;
}
} // namespace

TypeSpec decodeTypeSpec(const YAML::Node& node) {
    mapping(node, "TypeSpec");
    TypeSpec type;
    type.type = static_cast<Type>(named(node["type"], typeNames, "type"));
    const bool array = type.type == Type::NumericArray || type.type == Type::StringArray;
    if (!array && (node["dtype"] || node["rank"] || node["layout"]))
        throw ContractError("Only arrays may declare dtype, rank, or layout");
    if (node["dtype"]) type.dtype = static_cast<DType>(named(node["dtype"], dtypeNames, "dtype"));
    if (array) {
        const auto rank = integer(node["rank"]);
        if (rank < 0 || rank > std::numeric_limits<int>::max()) throw ContractError("Invalid array rank");
        type.rank = static_cast<int>(rank);
        if (node["layout"]) {
            const auto layout = scalar(node["layout"], "layout");
            if (layout == "batch_ragged") type.layout = Layout::BatchRagged;
            else if (layout != "dense") throw ContractError("Unknown array layout: " + layout);
        }
    }
    validateTypeSpec(type);
    return type;
}
YAML::Node encodeTypeSpec(const TypeSpec& type) {
    validateTypeSpec(type);
    YAML::Node node(YAML::NodeType::Map);
    node["type"] = typeNames[static_cast<std::size_t>(type.type)];
    if (type.type == Type::NumericArray || type.type == Type::StringArray) {
        if (type.dtype) node["dtype"] = dtypeNames[static_cast<std::size_t>(*type.dtype)];
        node["rank"] = type.rank;
        node["layout"] = type.layout == Layout::Dense ? "dense" : "batch_ragged";
    }
    return node;
}
PortValue decodeValue(const YAML::Node& node, const TypeSpec& type) {
    validateTypeSpec(type);
    if (!node.IsDefined() || node.IsNull()) return Missing{};
    switch (type.type) {
    case Type::Int: return integer(node);
    case Type::Float: return number<double>(node);
    case Type::Bool: return number<bool>(node);
    case Type::String: return Utf8(scalar(node, "String"));
    case Type::ImageBatch:
        sequence(node, "Image literal");
        if (node.size() != 0) throw ContractError("Image literals may only be Missing or an empty batch");
        return ImageBatch{};
    case Type::NumericArray: {
        std::vector<NumericBuffer> buffers;
        if (type.layout == Layout::Dense) buffers.push_back(decodeNumericBuffer(node, *type.dtype, type.rank));
        else {
            sequence(node, "Ragged array");
            buffers.reserve(node.size());
            for (const auto& buffer : node) buffers.push_back(decodeNumericBuffer(buffer, *type.dtype, type.rank - 1));
        }
        return NumericArray(*type.dtype, type.rank, type.layout, std::move(buffers));
    }
    case Type::StringArray: {
        std::vector<StringBuffer> buffers;
        if (type.layout == Layout::Dense) buffers.push_back(decodeStringBuffer(node, type.rank));
        else {
            sequence(node, "Ragged string array");
            buffers.reserve(node.size());
            for (const auto& buffer : node) buffers.push_back(decodeStringBuffer(buffer, type.rank - 1));
        }
        return StringArray(type.rank, type.layout, std::move(buffers));
    }
    case Type::RoiBatch: {
        sequence(node, "ROI batch");
        std::vector<std::vector<Roi>> images;
        images.reserve(node.size());
        for (const auto& image : node) {
            sequence(image, "Image ROIs");
            auto& rois = images.emplace_back();
            rois.reserve(image.size());
            for (const auto& roi : image) {
                sequence(roi, "ROI encoding");
                std::vector<float> encoded;
                encoded.reserve(roi.size());
                for (const auto& element : roi) encoded.push_back(number<float>(element));
                rois.push_back(Roi::decode(encoded));
            }
        }
        return RoiBatch(std::move(images));
    }
    }
    throw ContractError("Unknown port type");
}
YAML::Node encodeValue(const PortValue& value, const TypeSpec& type) {
    validateValue(type, value);
    if (isMissing(value)) return YAML::Node(YAML::NodeType::Null);
    switch (type.type) {
    case Type::Int: return YAML::Node(std::get<std::int64_t>(value));
    case Type::Float: return YAML::Node(std::get<double>(value));
    case Type::Bool: return YAML::Node(std::get<bool>(value));
    case Type::String: return YAML::Node(std::get<Utf8>(value).str());
    case Type::ImageBatch:
        if (!std::get<ImageBatch>(value).images().empty())
            throw ContractError("Image pixels cannot be persisted as literals");
        return YAML::Node(YAML::NodeType::Sequence);
    case Type::NumericArray: {
        const auto buffers = std::get<NumericArray>(value).buffers();
        if (type.layout == Layout::Dense) return encodeNumericBuffer(buffers[0]);
        YAML::Node node(YAML::NodeType::Sequence);
        for (const auto& buffer : buffers) node.push_back(encodeNumericBuffer(buffer));
        return node;
    }
    case Type::StringArray: {
        const auto buffers = std::get<StringArray>(value).buffers();
        if (type.layout == Layout::Dense) return encodeStringBuffer(buffers[0]);
        YAML::Node node(YAML::NodeType::Sequence);
        for (const auto& buffer : buffers) node.push_back(encodeStringBuffer(buffer));
        return node;
    }
    case Type::RoiBatch: {
        YAML::Node node(YAML::NodeType::Sequence);
        for (const auto& image : std::get<RoiBatch>(value).images()) {
            YAML::Node rois(YAML::NodeType::Sequence);
            for (const auto& roi : image) rois.push_back(encodeElements(roi.encoded()));
            node.push_back(rois);
        }
        return node;
    }
    }
    throw ContractError("Unknown port type");
}

} // namespace visionrt
