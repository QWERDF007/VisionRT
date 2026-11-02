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

const char *stageName(PortStage stage) noexcept
{
    switch (stage)
    {
    case PortStage::Init:
        return "init";
    case PortStage::Inputs:
        return "inputs";
    case PortStage::Outputs:
        return "outputs";
    }
    return "inputs";
}

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

const PortDefinition* findPort(const std::vector<PortDefinition>& ports, std::string_view name) {
    const auto it = std::find_if(ports.begin(), ports.end(), [&](const auto& p) { return p.nameEn == name; });
    return it == ports.end() ? nullptr : &*it;
}

const std::vector<PortDefinition>& stagePorts(const ToolDefinition& def, PortStage stage)
{
    switch (stage)
    {
    case PortStage::Init:
        return def.initParams;
    case PortStage::Inputs:
        return def.inputs;
    case PortStage::Outputs:
        return def.outputs;
    }
    return def.inputs;
}

const PortDefinition* findPort(const ToolDefinition& def, const PortRef& reference)
{
    return findPort(stagePorts(def, reference.stage), reference.port);
}

// A declaration never repeats what the type already says, so an output carries display hints and
// relations only: values, constraints and editors belong to the editing side.
void validatePorts(const std::vector<PortDefinition>& ports, PortStage stage) {
    const bool output = stage == PortStage::Outputs;
    std::set<std::string> names;

    for (const auto& port : ports) {
        const bool isStatus = output && isHostOwnedPort(port.nameEn);
        validateIdentifier(port.nameEn, !isStatus);
        if (!names.insert(port.nameEn).second) {
            throw ContractError("Duplicate " + std::string(stageName(stage)) + " port name: " + port.nameEn);
        }
        if (port.nameZh.empty()) {
            throw ContractError("Port name_zh must not be empty: " + port.nameEn);
        }

        if (port.typeFrom) {
            if (!output || isStatus)
                throw ContractError("type_from requires a business output: " + port.nameEn);
            if (!isMissing(port.defaultValue) || !port.editor.empty() || port.minimum || port.maximum || port.step
                || port.decimals || !port.options.empty() || !port.optionsProvider.empty() || port.image
                || port.alignment || port.pathModeFrom || port.sameGeometryAs || !port.colorOrder.empty())
                throw ContractError("type_from cannot have concrete value or display attributes: " + port.nameEn);
            continue;
        }
        validateTypeSpec(port.type);

        if (output) {
            if (!port.editor.empty()) throw ContractError("editor is not an output field: " + port.nameEn);
            if (port.minimum.has_value()) throw ContractError("constraints.min is not an output field: " + port.nameEn);
            if (port.maximum.has_value()) throw ContractError("constraints.max is not an output field: " + port.nameEn);
            if (port.step.has_value()) throw ContractError("ui.step is not an output field: " + port.nameEn);
            if (port.decimals.has_value()) throw ContractError("ui.decimals is not an output field: " + port.nameEn);
            if (!port.options.empty()) throw ContractError("constraints.options is not an output field: " + port.nameEn);
            if (!port.optionsProvider.empty()) throw ContractError("ui.options_provider is not an output field: " + port.nameEn);
            if (port.alignment || port.pathModeFrom)
                throw ContractError("relations.alignment and relations.path_mode apply to inputs: " + port.nameEn);
        } else {
            if (!port.colorOrder.empty())
                throw ContractError("display.color_order applies only to outputs: " + port.nameEn);
        }

        if (!port.editor.empty()) {
            const std::set<std::string> validEditors
                = {"number", "text", "bool", "file", "directory", "path", "array", "roi", "rect"};
            if (!validEditors.contains(port.editor)) {
                throw ContractError("Unknown editor: " + port.editor + " on port " + port.nameEn);
            }
        }

        const bool pathEditor = port.editor == "file" || port.editor == "directory" || port.editor == "path";
        if (port.editor == "rect" && port.type.type != Type::ROI)
            throw ContractError("矩形编辑器要求 ROI 类型端口：" + port.nameEn);
        if (pathEditor && port.type != TypeSpec{Type::String})
            throw ContractError("路径编辑器要求 String 类型端口：" + port.nameEn);

        if (port.pathModeFrom && (output || port.editor != "path"))
            throw ContractError("relations.path_mode requires a path editor: " + port.nameEn);

        if (!port.tab.empty()) {
            const auto tabs = toolTabs();
            if (std::find(tabs.begin(), tabs.end(), port.tab) == tabs.end()) {
                throw ContractError("Unknown tab: " + port.tab + " on port " + port.nameEn);
            }
        }

        const bool numeric = (port.type.type == Type::Int || port.type.type == Type::Float);
        if (!numeric) {
            if (port.minimum.has_value() || port.maximum.has_value() || port.step.has_value() || port.decimals.has_value()) {
                throw ContractError("Numeric constraints (min, max, step, decimals) require an Int or Float port: " + port.nameEn);
            }
            if (!isMissing(port.defaultValue)) {
                validateValue(port.type, port.defaultValue);
            }
        } else {
            if (port.minimum.has_value()) {
                validateNumericPortValue(*port.minimum, port.type.type, "min", port.nameEn);
            }
            if (port.maximum.has_value()) {
                validateNumericPortValue(*port.maximum, port.type.type, "max", port.nameEn);
            }
            if (port.minimum.has_value() && port.maximum.has_value()) {
                if (lessNumber(*port.maximum, *port.minimum, port.type.type)) {
                    throw ContractError("min must not exceed max on port: " + port.nameEn);
                }
            }
            if (port.step.has_value()) {
                validateNumericPortValue(*port.step, port.type.type, "step", port.nameEn);
                const PortValue zero = port.type.type == Type::Int ? PortValue{std::int64_t{0}} : PortValue{0.0};
                if (!lessNumber(zero, *port.step, port.type.type)) {
                    throw ContractError("step must be positive on port: " + port.nameEn);
                }
            }
            if (port.decimals.has_value() && *port.decimals < 0) {
                throw ContractError("decimals must be non-negative on port: " + port.nameEn);
            }
            if (!isMissing(port.defaultValue)) {
                validateNumericPortValue(port.defaultValue, port.type.type, "default", port.nameEn);
                if (port.minimum.has_value() && lessNumber(port.defaultValue, *port.minimum, port.type.type)) {
                    throw ContractError("Default is less than min on port: " + port.nameEn
                                        + "（类型空值不满足约束时必须显式声明 default）");
                }
                if (port.maximum.has_value() && lessNumber(*port.maximum, port.defaultValue, port.type.type)) {
                    throw ContractError("Default exceeds max on port: " + port.nameEn);
                }
            }
        }

        for (const auto& opt : port.options) {
            if (numeric) {
                validateNumericPortValue(opt, port.type.type, "option", port.nameEn);
                if (port.minimum.has_value() && lessNumber(opt, *port.minimum, port.type.type)) {
                    throw ContractError("Option is less than min on port: " + port.nameEn);
                }
                if (port.maximum.has_value() && lessNumber(*port.maximum, opt, port.type.type)) {
                    throw ContractError("Option exceeds max on port: " + port.nameEn);
                }
            } else {
                validateValue(port.type, opt);
            }
        }
        if (!port.options.empty() && !isMissing(port.defaultValue)) {
            const bool found = std::any_of(port.options.begin(), port.options.end(),
                [&](const auto& opt) { return equalValues(opt, port.defaultValue); });
            if (!found) {
                throw ContractError("default value is not one of declared options on port: " + port.nameEn);
            }
        }

        if (!port.optionsProvider.empty() && port.optionsProvider != "platform.gpu_devices") {
            throw ContractError("Unknown options_provider: " + port.optionsProvider + " on port " + port.nameEn);
        }

        if (port.image && port.type.type != Type::ROI)
            throw ContractError("relations.image requires an ROI port: " + port.nameEn);

        if (port.alignment && (stage != PortStage::Inputs || port.type.type != Type::ROI))
            throw ContractError("relations.alignment requires an ROI input: " + port.nameEn);

        if (!port.colorOrder.empty()) {
            if (port.type.type != Type::Image)
            {
                throw ContractError("display.color_order requires an Image output: " + port.nameEn);
            }
            const std::set<std::string> validColors = {"Gray", "BGR", "BGRA", "RGB", "RGBA"};
            if (!validColors.contains(port.colorOrder)) {
                throw ContractError("Unknown color_order: " + port.colorOrder + " on port " + port.nameEn);
            }
        }

        if (port.sameGeometryAs && port.type.type != Type::Image)
            throw ContractError("relations.same_geometry_as requires an Image output: " + port.nameEn);
    }
}

} // namespace

PortRef parsePortRef(std::string_view text)
{
    const auto dot = text.find('.');
    if (dot == std::string_view::npos)
        throw ContractError("端口引用必须带阶段前缀（init./inputs./outputs.）：" + std::string(text));
    const auto stage = text.substr(0, dot);
    const auto port  = text.substr(dot + 1);
    if (port.empty() || port.find('.') != std::string_view::npos)
        throw ContractError("端口引用必须形如 inputs.images：" + std::string(text));
    if (stage == "init")
        return {PortStage::Init, std::string(port)};
    if (stage == "inputs")
        return {PortStage::Inputs, std::string(port)};
    if (stage == "outputs")
        return {PortStage::Outputs, std::string(port)};
    throw ContractError("未知端口引用阶段：" + std::string(stage));
}

std::string formatPortRef(const PortRef& reference)
{
    return std::string(stageName(reference.stage)) + "." + reference.port;
}

std::string categoryPath(const ToolDefinition& def)
{
    std::string path;
    for (const auto &name : def.categories)
        path += (path.empty() ? "" : "/") + name;
    return path;
}

bool isPathPort(const PortDefinition& port) {
    return port.type == TypeSpec{Type::String} &&
        (port.editor == "file" || port.editor == "directory" || port.editor == "path");
}

bool isDerivedInitParam(std::string_view id) noexcept
{
    return id == "variables" || id == "program" || id == "condition_mode";
}

bool isHostOwnedPort(std::string_view id) noexcept
{
    return id == kUpstreamStatusPort || id == kStatusCodePort || id == kStatusMessagePort;
}

bool supportsExecutionGuard(NodeModel model) noexcept
{
    switch (model)
    {
    case NodeModel::Expression:
    case NodeModel::Condition:
    case NodeModel::Select:
        return true;
    case NodeModel::Fixed:
        return false;
    }
    return false;
}

std::span<const std::string_view> toolTabs() noexcept {
    static constexpr std::array<std::string_view, 4> tabs{"基本输入", "预处理", "检测", "结果处理"};
    return tabs;
}

PortValue portDefault(const PortDefinition& port)
{
    if (const auto *array = std::get_if<Array>(&port.defaultValue))
        return array->clone();
    return port.defaultValue;
}

void normalizeToolDefinition(ToolDefinition &def)
{
    // Every contract carries the two status outputs, whatever the node model: the tool writes them
    // like any other port and the host never adds or rewrites one. Materialising here keeps a
    // hand-built contract and a parsed declaration identical.
    for (const auto id : {kStatusMessagePort, kStatusCodePort})
    {
        const auto existing = std::find_if(def.outputs.begin(), def.outputs.end(),
                                           [&](const auto &port) { return port.nameEn == id; });
        PortDefinition canonical;
        canonical.nameEn       = std::string(id);
        canonical.nameZh       = id == kStatusCodePort ? "状态码" : "状态消息";
        canonical.type         = TypeSpec{id == kStatusCodePort ? Type::Int : Type::String};
        canonical.defaultValue = Missing{};
        if (existing == def.outputs.end())
            def.outputs.insert(def.outputs.begin(), std::move(canonical));
        else
            *existing = std::move(canonical);
    }

    const auto canonical = [](PortDefinition port)
    {
        port.nameZh       = port.nameZh.empty() ? std::string("上层工具状态") : port.nameZh;
        port.type         = TypeSpec{Type::Int};
        port.defaultValue = PortValue{std::int64_t{0}};
        port.editor.clear();
        port.tab          = "基本输入";
        port.group.clear();
        port.minimum.reset();
        port.maximum.reset();
        port.step.reset();
        port.decimals.reset();
        port.options.clear();
        port.optionsProvider.clear();
        port.image.reset();
        port.alignment.reset();
        port.pathModeFrom.reset();
        port.sameGeometryAs.reset();
        port.colorOrder.clear();
        port.typeFrom.reset();
        return port;
    };

    if (def.nodeModel == NodeModel::Fixed)
    {
        const auto existing = std::find_if(def.inputs.begin(), def.inputs.end(),
                                           [](const auto &port) { return port.nameEn == kUpstreamStatusPort; });
        if (existing == def.inputs.end())
        {
            PortDefinition status;
            status.nameEn = std::string(kUpstreamStatusPort);
            def.inputs.insert(def.inputs.begin(), canonical(std::move(status)));
        }
        else if (existing->type != TypeSpec{Type::Int})
            throw ContractError("upstream_status 必须是 Int 输入：" + def.nameEn);
        else if (existing->typeFrom)
            throw ContractError("upstream_status 不能由 type_from 派生：" + def.nameEn);
        else if (!existing->options.empty() || !existing->optionsProvider.empty())
            throw ContractError("upstream_status 不接受 options 约束：" + def.nameEn);
        else if (existing->minimum || existing->maximum || existing->step || existing->decimals)
            throw ContractError("upstream_status 不接受数值范围约束：" + def.nameEn);
        else if (!isMissing(existing->defaultValue) && !equalValues(existing->defaultValue, PortValue{std::int64_t{0}}))
            throw ContractError("upstream_status 的默认值必须为 0：" + def.nameEn);
        else if (existing != def.inputs.begin())
        {
            auto replacement = canonical(*existing);
            def.inputs.erase(existing);
            def.inputs.insert(def.inputs.begin(), std::move(replacement));
        }
        else
            *existing = canonical(*existing);
    }

    // The one place a port without an explicit default receives the type's empty value. A status
    // port keeps Missing: the run's status, not a 0, says whether the tool succeeded.
    for (auto *list : {&def.initParams, &def.inputs, &def.outputs})
        for (auto &port : *list)
            if (!port.typeFrom && !isHostOwnedPort(port.nameEn) && isMissing(port.defaultValue))
                port.defaultValue = defaultValue(port.type);
}

void validateToolDefinition(const ToolDefinition& def) {
    if (def.schemaVersion != 5)
    {
        throw ContractError("Unsupported schema_version: " + std::to_string(def.schemaVersion));
    }
    validateIdentifier(def.nameEn, false);
    if (def.nameZh.empty()) {
        throw ContractError("Tool name_zh must not be empty: " + def.nameEn);
    }
    if (def.version.empty()) {
        throw ContractError("Tool version must not be empty: " + def.nameEn);
    }
    for (const auto &name : def.categories)
        if (name.empty())
            throw ContractError("ui.category must not contain an empty name: " + def.nameEn);

    const int kindInt = static_cast<int>(def.kind);
    if (kindInt < 0 || kindInt > 3) {
        throw ContractError("Unknown tool kind on: " + def.nameEn);
    }

    if (def.kind == ToolKind::PythonFunction || def.kind == ToolKind::PythonClass) {
        if (def.entry.package.empty() || def.entry.file.empty() || def.entry.symbol.empty())
        {
            throw ContractError("Python tool requires implementation.package, implementation.file and implementation.symbol: " + def.nameEn);
        }
        for (const auto &path : {def.entry.package, def.entry.file})
            if (path.is_absolute() || path.has_root_name() || path.has_root_directory()
                || *path.lexically_normal().begin() == "..")
                throw ContractError("Python entry paths must remain inside their selected root: " + def.nameEn);
        if (def.entry.file.extension() != ".py")
            throw ContractError("Python implementation.file must name a .py source: " + def.nameEn);
    }
    else if (!def.entry.package.empty() || !def.entry.file.empty() || !def.entry.symbol.empty())
        throw ContractError("Non-Python tool cannot declare a Python entry: " + def.nameEn);
    for (const auto &resource : def.resources)
        if (resource.empty() || resource.is_absolute() || resource.has_root_name() || resource.has_root_directory())
            throw ContractError("resources must contain relative paths: " + def.nameEn);
    if (def.kind == ToolKind::Host && def.hostBinding.empty())
        throw ContractError("宿主工具必须声明 implementation.binding：" + def.nameEn);
    if (def.kind != ToolKind::Host && !def.hostBinding.empty())
        throw ContractError("非宿主工具不能声明 implementation.binding：" + def.nameEn);

    validatePorts(def.initParams, PortStage::Init);
    if (def.kind == ToolKind::PythonFunction) {
        if (!def.initParams.empty()) {
            throw ContractError("Function tool cannot have init params: " + def.nameEn);
        }
    }
    validatePorts(def.inputs, PortStage::Inputs);
    validatePorts(def.outputs, PortStage::Outputs);

    for (const auto& port : def.initParams) {
        if (findPort(def.inputs, port.nameEn)) {
            throw ContractError("init and inputs share a name: " + port.nameEn);
        }
    }

    const auto requireImage = [&](const PortRef &reference, const char *context)
    {
        const auto *target = findPort(def, reference);
        if (!target || target->type.type != Type::Image)
            throw ContractError(std::string(context) + " must reference an Image port: " + formatPortRef(reference));
    };

    for (const auto *ports : {&def.initParams, &def.inputs})
        for (const auto& port : *ports) {
            if (!port.pathModeFrom) continue;
            if (port.pathModeFrom->stage != (ports == &def.initParams ? PortStage::Init : PortStage::Inputs))
                throw ContractError("relations.path_mode must reference the same stage: " + port.nameEn);
            const auto* mode = findPort(*ports, port.pathModeFrom->port);
            if (!mode || mode->nameEn == port.nameEn || mode->type != TypeSpec{Type::String})
                throw ContractError("relations.path_mode must reference another String parameter: " + port.nameEn);
        }

    for (const auto *list : {&def.initParams, &def.inputs, &def.outputs})
        for (const auto& port : *list)
        {
            if (port.image)
            {
                if (port.image->stage == PortStage::Outputs && port.image->port == port.nameEn
                    && list == &def.outputs)
                    throw ContractError("relations.image must not reference itself: " + port.nameEn);
                requireImage(*port.image, "relations.image");
            }
            if (port.sameGeometryAs)
                requireImage(*port.sameGeometryAs, "relations.same_geometry_as");
        }

    for (const auto &port : def.outputs) {
        if (port.typeFrom) {
            if (def.kind != ToolKind::Host)
                throw ContractError("type_from 仅适用于宿主工具：" + def.nameEn);
            for (const auto& reference : {port.typeFrom->flowParameter, port.typeFrom->nodeParameter,
                    port.typeFrom->portParameter}) {
                if (reference.stage != PortStage::Init)
                    throw ContractError("type_from must reference initialization parameters: "
                                        + formatPortRef(reference));
                const auto* target = findPort(def.initParams, reference.port);
                if (!target || target->typeFrom || target->type != TypeSpec{Type::String})
                    throw ContractError("type_from must reference a String initialization parameter: "
                                        + formatPortRef(reference));
            }
        }
    }

    for (const auto &port : def.inputs)
    {
        if (!port.alignment)
            continue;
        const auto *target = findPort(def, *port.alignment);
        if (!target || target->nameEn == port.nameEn || target->typeFrom
            || target->type != TypeSpec{Type::Array, DType::Float64, 3})
            throw ContractError("relations.alignment 必须引用另一个 Array(float64, rank=3) 输入：" + port.nameEn);
    }

    const auto requireParameter = [&](const char *name)
    {
        const auto *parameter = findPort(def.initParams, name);
        if (!parameter || parameter->type != TypeSpec{Type::Array, DType::String, 2})
            throw ContractError(std::string("节点模型需要 vec_vec_string 初始化参数：") + name);
    };
    switch (def.nodeModel)
    {
    case NodeModel::Fixed:
        break;
    case NodeModel::Expression:
    case NodeModel::Condition:
        if (def.kind != ToolKind::PythonClass)
            throw ContractError("表达式与条件节点必须是 python_class 工具：" + def.nameEn);
        requireParameter("variables");
        requireParameter("program");
        if (def.nodeModel == NodeModel::Condition)
        {
            const auto *mode = findPort(def.initParams, "condition_mode");
            const bool  validMode
                = mode && mode->type == TypeSpec{Type::String} && mode->options.size() == 2
               && std::any_of(mode->options.begin(), mode->options.end(),
                              [](const auto &value) { return equalValues(value, PortValue{std::string{"formula"}}); })
               && std::any_of(mode->options.begin(), mode->options.end(),
                              [](const auto &value) { return equalValues(value, PortValue{std::string{"upstream"}}); })
               && equalValues(mode->defaultValue, PortValue{std::string{"formula"}});
            if (!validMode)
                throw ContractError("条件节点必须声明 condition_mode:string，选项 formula/upstream，默认 formula：" + def.nameEn);
            if (def.outputs.size() != 6 || !findPort(def.outputs, "on_true") || !findPort(def.outputs, "on_false")
                || !findPort(def.outputs, "on_true_status") || !findPort(def.outputs, "on_false_status"))
                throw ContractError("条件节点必须声明两个布尔输出与两路状态输出：" + def.nameEn);
        }
        break;
    case NodeModel::Select:
        if (def.kind != ToolKind::Host)
            throw ContractError("合流节点必须是宿主工具：" + def.nameEn);
        if (findPort(def.inputs, "selector") || findPort(def.inputs, "if_true") || findPort(def.inputs, "if_false")
            || findPort(def.outputs, "value"))
            throw ContractError("合流节点端口由节点类型派生，声明中不能固定声明：" + def.nameEn);
        break;
    }

    if (def.visibility == ToolVisibility::Assist)
    {
        if (def.nodeModel != NodeModel::Fixed || def.interaction)
            throw ContractError("辅助工具必须是固定节点且不能声明交互映射：" + def.nameEn);
        if (def.kind == ToolKind::Host)
            throw ContractError("辅助工具不能是宿主工具：" + def.nameEn);
    }
    if (def.interaction)
    {
        if (def.visibility != ToolVisibility::Normal)
            throw ContractError("只有普通工具可以声明交互映射：" + def.nameEn);
        if (def.interaction->helperNameEn.empty())
            throw ContractError("交互映射缺少 interaction.helper：" + def.nameEn);
        if (def.interaction->roiKinds.empty())
            throw ContractError("交互映射必须声明允许的 ROI kind：" + def.nameEn);
        for (const auto &[helper, reference] : def.interaction->inputs)
        {
            if (helper.empty() || !findPort(def, reference))
                throw ContractError("交互 interaction.inputs 引用了不存在的端口：" + formatPortRef(reference)
                                    + "（" + def.nameEn + "）");
        }
        for (const auto &[helper, reference] : def.interaction->proposals)
        {
            const auto *target = findPort(def, reference);
            if (helper.empty() || !target || reference.stage == PortStage::Outputs || target->type.type == Type::Image)
                throw ContractError("交互 interaction.proposals 引用了不可写输入：" + formatPortRef(reference)
                                    + "（" + def.nameEn + "）");
        }
    }
}

} // namespace visionrt
