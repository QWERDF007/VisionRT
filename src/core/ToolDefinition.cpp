#include "core/ToolDefinition.h"
#include "core/ToolPaths.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>

namespace visionrt {

std::filesystem::path ToolDefinition::pythonPackage() const
{
    return resolveToolPath(source.roots.python, entry.package);
}

std::filesystem::path ToolDefinition::pythonScript() const
{
    return resolveToolPath(pythonPackage(), entry.file);
}

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

        if (port.typeFrom) {
            if (!output || isStatus) throw ContractError("type_from requires a business output: " + port.id);
            if (!isMissing(port.defaultValue) || !port.editor.empty() || port.minimum || port.maximum || port.step
                || port.decimals || !port.options.empty() || !port.optionsProvider.empty() || !port.imageInput.empty()
                || !port.imagePort.empty() || !port.colorOrder.empty() || !port.sameGeometryAsInput.empty()
                || !port.pathModeFrom.empty())
                throw ContractError("type_from cannot have concrete value or display attributes: " + port.id);
            continue;
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
            const std::set<std::string> validEditors
                = {"number", "text", "bool", "file", "directory", "path", "array", "roi", "rect"};
            if (!validEditors.contains(port.editor)) {
                throw ContractError("Unknown editor: " + port.editor + " on port " + port.id);
            }
        }

        const bool pathEditor = port.editor == "file" || port.editor == "directory" || port.editor == "path";
        if (port.editor == "rect" && port.type.type != Type::ROI)
            throw ContractError("矩形编辑器要求 ROI 类型端口：" + port.id);
        if (pathEditor && port.type != TypeSpec{Type::String})
            throw ContractError("路径编辑器要求 String 类型端口：" + port.id);

        if (!port.pathModeFrom.empty() && (output || port.editor != "path"))
            throw ContractError("path_mode_from requires a path editor: " + port.id);
        if (port.editor == "path" && port.pathModeFrom.empty())
            throw ContractError("path editor requires path_mode_from: " + port.id);

        if (!port.tab.empty()) {
            const auto tabs = toolTabs();
            if (std::find(tabs.begin(), tabs.end(), port.tab) == tabs.end()) {
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
            if (direction != "inputs" || port.type.type != Type::ROI)
            {
                throw ContractError("image_input requires an ROI input: " + port.id);
            }
        }

        if (!port.alignmentInput.empty() && (direction != "inputs" || port.type.type != Type::ROI))
            throw ContractError("alignment_input requires an ROI input: " + port.id);

        if (!port.imagePort.empty()) {
            if (port.type.type != Type::ROI)
            {
                throw ContractError("image_port requires an ROI output: " + port.id);
            }
        }

        if (!port.colorOrder.empty()) {
            if (port.type.type != Type::Image)
            {
                throw ContractError("display.color_order requires an Image output: " + port.id);
            }
            const std::set<std::string> validColors = {"Gray", "BGR", "BGRA", "RGB", "RGBA"};
            if (!validColors.contains(port.colorOrder)) {
                throw ContractError("Unknown color_order: " + port.colorOrder + " on port " + port.id);
            }
        }

        if (!port.sameGeometryAsInput.empty()) {
            if (port.type.type != Type::Image)
            {
                throw ContractError("display.same_geometry_as_input requires an Image output: " + port.id);
            }
        }
    }
}

} // namespace

bool isPathPort(const PortDefinition& port) {
    return port.type == TypeSpec{Type::String} &&
        (port.editor == "file" || port.editor == "directory" || port.editor == "path");
}

std::span<const std::string_view> toolTabs() noexcept {
    static constexpr std::array<std::string_view, 4> tabs{"基本输入", "预处理", "检测", "结果处理"};
    return tabs;
}

void validateToolDefinition(const ToolDefinition& def) {
    if (def.schemaVersion != 3)
    {
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

    if (def.kind == ToolKind::PythonFunction || def.kind == ToolKind::PythonClass) {
        if (def.entry.package.empty() || def.entry.file.empty() || def.entry.symbol.empty())
        {
            throw ContractError("Python tool requires entry.package, entry.file and entry.symbol: " + def.id);
        }
        for (const auto &path : {def.entry.package, def.entry.file})
            if (path.is_absolute() || path.has_root_name() || path.has_root_directory()
                || *path.lexically_normal().begin() == "..")
                throw ContractError("Python entry paths must remain inside their selected root: " + def.id);
        if (def.entry.file.extension() != ".py")
            throw ContractError("Python entry.file must name a .py source: " + def.id);
    }
    else if (!def.entry.package.empty() || !def.entry.file.empty() || !def.entry.symbol.empty())
        throw ContractError("Non-Python tool cannot declare a Python entry: " + def.id);
    for (const auto &resource : def.resources)
        if (resource.empty() || resource.is_absolute() || resource.has_root_name() || resource.has_root_directory())
            throw ContractError("resources must contain relative paths: " + def.id);
    if (def.kind == ToolKind::Host && def.hostBinding.empty())
        throw ContractError("宿主工具必须声明 entry.binding：" + def.id);
    if (def.kind != ToolKind::Host && !def.hostBinding.empty())
        throw ContractError("非宿主工具不能声明 entry.binding：" + def.id);

    validatePorts(def.initParams, "init_params");
    if (def.kind == ToolKind::PythonFunction) {
        if (!def.initParams.empty()) {
            throw ContractError("Function tool cannot have init_params: " + def.id);
        }
    }
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
        if (!target || target->type.type != Type::Image)
        {
            throw ContractError(std::string(context) + " must reference an Image port: " + id);
        }
    };

    for (const auto* ports : {&def.initParams, &def.inputs})
        for (const auto& port : *ports) {
            if (port.pathModeFrom.empty()) continue;
            const auto* mode = findPort(*ports, port.pathModeFrom);
            if (!mode || mode->id == port.id || mode->type != TypeSpec{Type::String})
                throw ContractError("path_mode_from must reference another String parameter: " + port.id);
        }

    for (const auto& port : def.inputs) {
        checkImageRef(def.inputs, port.imageInput, "image_input");
    }
    for (const auto& port : def.outputs) {
        if (port.typeFrom) {
            if (def.kind != ToolKind::Host)
                throw ContractError("type_from 仅适用于宿主工具：" + def.id);
            for (const auto& parameter : {port.typeFrom->flowParameter, port.typeFrom->nodeParameter,
                    port.typeFrom->portParameter}) {
                const auto* reference = findPort(def.initParams, parameter);
                if (!reference || reference->typeFrom || reference->type != TypeSpec{Type::String})
                    throw ContractError("type_from must reference a String initialization parameter: " + parameter);
            }
        }
        if (!port.imagePort.empty() && !findPort(def.outputs, port.imagePort)
            && !findPort(def.inputs, port.imagePort))
            throw ContractError("image_port must reference an Image port: " + port.imagePort);
        if (!port.imagePort.empty())
        {
            const auto *target = findPort(def.outputs, port.imagePort);
            if (!target)
                target = findPort(def.inputs, port.imagePort);
            if (!target || target->type.type != Type::Image)
                throw ContractError("image_port must reference an Image port: " + port.imagePort);
        }
        checkImageRef(def.inputs, port.sameGeometryAsInput, "same_geometry_as_input");
    }

    for (const auto &port : def.inputs)
    {
        if (port.alignmentInput.empty())
            continue;
        const auto *target = findPort(def.inputs, port.alignmentInput);
        if (!target || target->id == port.id || target->typeFrom
            || target->type != TypeSpec{Type::Array, DType::Float64, 3})
            throw ContractError("alignment_input 必须引用另一个 Array(float64, rank=3) 输入：" + port.id);
    }

    const auto requireParameter = [&](const char *id)
    {
        const auto *parameter = findPort(def.initParams, id);
        if (!parameter || parameter->type != TypeSpec{Type::Array, DType::String, 2})
            throw ContractError(std::string("节点模型需要 Array(string, rank=2) 初始化参数：") + id);
    };
    switch (def.nodeModel)
    {
    case NodeModel::Fixed:
        break;
    case NodeModel::Expression:
    case NodeModel::Condition:
        if (def.kind != ToolKind::PythonClass)
            throw ContractError("表达式与条件节点必须是 python_class 工具：" + def.id);
        requireParameter("variables");
        requireParameter("program");
        if (def.nodeModel == NodeModel::Condition
            && (def.outputs.size() != 4 || !findPort(def.outputs, "on_true") || !findPort(def.outputs, "on_false")))
            throw ContractError("条件节点必须声明 on_true 与 on_false 两个布尔输出：" + def.id);
        break;
    case NodeModel::Select:
        if (def.kind != ToolKind::Host)
            throw ContractError("合流节点必须是宿主工具：" + def.id);
        if (findPort(def.inputs, "selector") || findPort(def.inputs, "if_true") || findPort(def.inputs, "if_false")
            || findPort(def.outputs, "value"))
            throw ContractError("合流节点端口由节点类型派生，声明中不能固定声明：" + def.id);
        break;
    }

    if (def.visibility == ToolVisibility::Assist)
    {
        if (def.nodeModel != NodeModel::Fixed || def.interaction)
            throw ContractError("辅助工具必须是固定节点且不能声明交互映射：" + def.id);
        if (def.kind == ToolKind::Host)
            throw ContractError("辅助工具不能是宿主工具：" + def.id);
    }
    if (def.interaction)
    {
        if (def.visibility != ToolVisibility::Normal)
            throw ContractError("只有普通工具可以声明交互映射：" + def.id);
        if (def.interaction->helperToolId.empty())
            throw ContractError("交互映射缺少 helper_tool_id：" + def.id);
        if (def.interaction->roiKinds.empty())
            throw ContractError("交互映射必须声明允许的 ROI kind：" + def.id);
        for (const auto &[helper, port] : def.interaction->inputMap)
        {
            if (helper.empty() || !findPort(def.inputs, port))
                throw ContractError("交互 input_map 引用了不存在的输入：" + port + "（" + def.id + "）");
        }
        for (const auto &[helper, port] : def.interaction->proposalMap)
        {
            const auto *target = findPort(def.inputs, port);
            if (helper.empty() || !target || target->type.type == Type::Image)
                throw ContractError("交互 proposal_map 引用了不可写输入：" + port + "（" + def.id + "）");
        }
    }
}

} // namespace visionrt
