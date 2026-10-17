#include "visionrt/sdk/Schema.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace visionrt {

namespace {

void validateIdentifier(const std::string& value, bool isPort) {
    const auto letter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    if (value.empty() || (!letter(value.front()) && value.front() != '_') ||
        !std::all_of(value.begin(), value.end(), [&](char c) {
            return letter(c) || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        })) {
        throw ContractError("ID must be a stable English identifier: " + value);
    }
    if (isPort && value.starts_with("__")) {
        throw ContractError("Port ID uses reserved __ prefix: " + value);
    }
}

void validateNumericPortValue(const PortValue& val, Type type, const std::string& fieldName, const std::string& portId) {
    if (type == Type::Int) {
        if (!std::holds_alternative<std::int64_t>(val)) {
            throw ContractError(fieldName + " type mismatch (expected Int) on port: " + portId);
        }
    } else if (type == Type::Float) {
        if (!std::holds_alternative<double>(val)) {
            throw ContractError(fieldName + " type mismatch (expected Float) on port: " + portId);
        }
        if (!std::isfinite(std::get<double>(val))) {
            throw ContractError(fieldName + " must be a finite numeric value on port: " + portId);
        }
    } else {
        throw ContractError(fieldName + " requires an Int or Float port: " + portId);
    }
}

bool lessNumber(const PortValue& left, const PortValue& right, Type type) {
    if (type == Type::Int) return std::get<std::int64_t>(left) < std::get<std::int64_t>(right);
    return std::get<double>(left) < std::get<double>(right);
}

const PortDefinition* findPort(const std::vector<PortDefinition>& ports, std::string_view id) {
    const auto it = std::find_if(ports.begin(), ports.end(), [&](const auto& p) { return p.id == id; });
    return it == ports.end() ? nullptr : &*it;
}

void validatePorts(const std::vector<PortDefinition>& ports, const std::string& direction) {
    const bool output = (direction == "outputs");
    std::set<std::string> ids;

    for (const auto& port : ports) {
        const bool isStatus = output && (port.id == "__status_code" || port.id == "__status_message");
        validateIdentifier(port.id, !isStatus);
        if (!ids.insert(port.id).second) {
            throw ContractError("Duplicate " + direction + " port ID: " + port.id);
        }
        if (port.name.empty()) {
            throw ContractError("Port name must not be empty: " + port.id);
        }

        validateTypeSpec(port.type);

        if (output) {
            if (!port.editor.empty()) throw ContractError("editor is not an output field: " + port.id);
            if (port.minimum.has_value()) throw ContractError("min is not an output field: " + port.id);
            if (port.maximum.has_value()) throw ContractError("max is not an output field: " + port.id);
            if (port.step.has_value()) throw ContractError("step is not an output field: " + port.id);
            if (port.decimals.has_value()) throw ContractError("decimals is not an output field: " + port.id);
            if (!port.options.empty()) throw ContractError("options is not an output field: " + port.id);
            if (!port.optionsProvider.empty()) throw ContractError("options_provider is not an output field: " + port.id);
            if (!port.imageInput.empty()) throw ContractError("image_input is not an output field: " + port.id);
        } else {
            if (!port.colorOrder.empty() || !port.sameGeometryAsInput.empty() || !port.imagePort.empty()) {
                throw ContractError("display and image_port apply only to outputs: " + port.id);
            }
        }

        if (!port.editor.empty()) {
            const std::set<std::string> validEditors = {"number", "text", "bool", "file", "directory", "array", "roi"};
            if (!validEditors.contains(port.editor)) {
                throw ContractError("Unknown editor: " + port.editor + " on port " + port.id);
            }
        }

        if (!port.tab.empty()) {
            const std::set<std::string> validTabs = {"基本输入", "预处理", "检测", "结果处理"};
            if (!validTabs.contains(port.tab)) {
                throw ContractError("Unknown tab: " + port.tab + " on port " + port.id);
            }
        }

        const bool numeric = (port.type.type == Type::Int || port.type.type == Type::Float);
        if (!numeric) {
            if (port.minimum.has_value() || port.maximum.has_value() || port.step.has_value() || port.decimals.has_value()) {
                throw ContractError("Numeric constraints (min, max, step, decimals) require an Int or Float port: " + port.id);
            }
            if (!isMissing(port.defaultValue)) {
                validateValue(port.type, port.defaultValue);
            }
        } else {
            if (port.minimum.has_value()) {
                validateNumericPortValue(*port.minimum, port.type.type, "min", port.id);
            }
            if (port.maximum.has_value()) {
                validateNumericPortValue(*port.maximum, port.type.type, "max", port.id);
            }
            if (port.minimum.has_value() && port.maximum.has_value()) {
                if (lessNumber(*port.maximum, *port.minimum, port.type.type)) {
                    throw ContractError("min must not exceed max on port: " + port.id);
                }
            }
            if (port.step.has_value()) {
                validateNumericPortValue(*port.step, port.type.type, "step", port.id);
                const PortValue zero = port.type.type == Type::Int ? PortValue{std::int64_t{0}} : PortValue{0.0};
                if (!lessNumber(zero, *port.step, port.type.type)) {
                    throw ContractError("step must be positive on port: " + port.id);
                }
            }
            if (port.decimals.has_value() && *port.decimals < 0) {
                throw ContractError("decimals must be non-negative on port: " + port.id);
            }
            if (!isMissing(port.defaultValue)) {
                validateNumericPortValue(port.defaultValue, port.type.type, "default", port.id);
                if (port.minimum.has_value() && lessNumber(port.defaultValue, *port.minimum, port.type.type)) {
                    throw ContractError("Default is less than min on port: " + port.id);
                }
                if (port.maximum.has_value() && lessNumber(*port.maximum, port.defaultValue, port.type.type)) {
                    throw ContractError("Default exceeds max on port: " + port.id);
                }
            }
        }

        for (const auto& opt : port.options) {
            if (numeric) {
                validateNumericPortValue(opt, port.type.type, "option", port.id);
                if (port.minimum.has_value() && lessNumber(opt, *port.minimum, port.type.type)) {
                    throw ContractError("Option is less than min on port: " + port.id);
                }
                if (port.maximum.has_value() && lessNumber(*port.maximum, opt, port.type.type)) {
                    throw ContractError("Option exceeds max on port: " + port.id);
                }
            } else {
                validateValue(port.type, opt);
            }
        }
        if (!port.options.empty() && !isMissing(port.defaultValue)) {
            const bool found = std::any_of(port.options.begin(), port.options.end(),
                [&](const auto& opt) { return equalValues(opt, port.defaultValue); });
            if (!found) {
                throw ContractError("default value is not one of declared options on port: " + port.id);
            }
        }

        if (!port.optionsProvider.empty() && port.optionsProvider != "platform.gpu_devices") {
            throw ContractError("Unknown options_provider: " + port.optionsProvider + " on port " + port.id);
        }

        if (!port.imageInput.empty()) {
            if (direction != "inputs" || port.type.type != Type::RoiBatch) {
                throw ContractError("image_input requires a RoiBatch input: " + port.id);
            }
        }

        if (!port.imagePort.empty()) {
            if (port.type.type != Type::RoiBatch) {
                throw ContractError("image_port requires a RoiBatch output: " + port.id);
            }
        }

        if (!port.colorOrder.empty()) {
            if (port.type.type != Type::ImageBatch) {
                throw ContractError("display.color_order requires an ImageBatch output: " + port.id);
            }
            const std::set<std::string> validColors = {"Gray", "BGR", "BGRA", "RGB", "RGBA"};
            if (!validColors.contains(port.colorOrder)) {
                throw ContractError("Unknown color_order: " + port.colorOrder + " on port " + port.id);
            }
        }

        if (!port.sameGeometryAsInput.empty()) {
            if (port.type.type != Type::ImageBatch) {
                throw ContractError("display.same_geometry_as_input requires an ImageBatch output: " + port.id);
            }
        }
    }
}

} // namespace

void validateToolDefinition(const ToolDefinition& def) {
    if (def.schemaVersion != 1) {
        throw ContractError("Unsupported schema_version: " + std::to_string(def.schemaVersion));
    }
    validateIdentifier(def.id, false);
    if (def.displayName.empty()) {
        throw ContractError("Tool displayName must not be empty: " + def.id);
    }
    if (def.version.empty()) {
        throw ContractError("Tool version must not be empty: " + def.id);
    }

    const int kindInt = static_cast<int>(def.kind);
    if (kindInt < 0 || kindInt > 3) {
        throw ContractError("Unknown tool kind on: " + def.id);
    }

    if (def.kind == ToolKind::CppFunction || def.kind == ToolKind::CppClass) {
        if (def.registryName.empty()) {
            throw ContractError("C++ tool requires entry.registry_name: " + def.id);
        }
    } else if (def.kind == ToolKind::PythonFunction || def.kind == ToolKind::PythonClass) {
        if (def.script.empty() || def.symbol.empty()) {
            throw ContractError("Python tool requires non-empty entry.file and entry.symbol: " + def.id);
        }
    }

    validatePorts(def.initParams, "init_params");
    validatePorts(def.inputs, "inputs");
    validatePorts(def.outputs, "outputs");

    for (const auto& port : def.initParams) {
        if (findPort(def.inputs, port.id)) {
            throw ContractError("init_params and inputs share ID: " + port.id);
        }
    }

    const auto checkImageRef = [](const std::vector<PortDefinition>& ports, const std::string& id, const char* context) {
        if (id.empty()) return;
        const auto* target = findPort(ports, id);
        if (!target || target->type.type != Type::ImageBatch) {
            throw ContractError(std::string(context) + " must reference an ImageBatch port: " + id);
        }
    };

    for (const auto& port : def.inputs) {
        checkImageRef(def.inputs, port.imageInput, "image_input");
    }
    for (const auto& port : def.outputs) {
        checkImageRef(def.outputs, port.imagePort, "image_port");
        checkImageRef(def.inputs, port.sameGeometryAsInput, "same_geometry_as_input");
    }

    if ((def.kind == ToolKind::CppFunction || def.kind == ToolKind::PythonFunction) && !def.initParams.empty()) {
        throw ContractError("Function tools must have empty init_params: " + def.id);
    }
}

} // namespace visionrt
