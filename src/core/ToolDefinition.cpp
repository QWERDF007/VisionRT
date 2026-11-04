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

std::uint64_t ToolDefinition::nextPortId(PortStage stage) const noexcept
{
    switch (stage)
    {
    case PortStage::Init:
        return nextInitPortId;
    case PortStage::Inputs:
        return nextInputPortId;
    case PortStage::Outputs:
        return nextOutputPortId;
    }
    return nextInputPortId;
}

void ToolDefinition::setNextPortId(PortStage stage, std::uint64_t value)
{
    switch (stage)
    {
    case PortStage::Init:
        nextInitPortId = value;
        return;
    case PortStage::Inputs:
        nextInputPortId = value;
        return;
    case PortStage::Outputs:
        nextOutputPortId = value;
        return;
    }
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

PortId floorId(PortStage stage) noexcept
{
    return stage == PortStage::Init ? 0 : stage == PortStage::Inputs ? 1 : 2;
}

void validateNumericPortValue(const PortValue& val, Type type, const std::string& fieldName, PortId portId) {
    const auto where = " on port " + portLabel(portId);
    if (type == Type::Int) {
        if (!std::holds_alternative<std::int64_t>(val)) {
            throw ContractError(fieldName + " type mismatch (expected Int)" + where);
        }
    } else if (type == Type::Float) {
        if (!std::holds_alternative<double>(val)) {
            throw ContractError(fieldName + " type mismatch (expected Float)" + where);
        }
        if (!std::isfinite(std::get<double>(val))) {
            throw ContractError(fieldName + " must be a finite numeric value" + where);
        }
    } else {
        throw ContractError(fieldName + " requires an Int or Float port" + where);
    }
}

bool lessNumber(const PortValue& left, const PortValue& right, Type type) {
    if (type == Type::Int) return std::get<std::int64_t>(left) < std::get<std::int64_t>(right);
    return std::get<double>(left) < std::get<double>(right);
}

const PortDefinition* findPort(const std::vector<PortDefinition>& ports, PortId id) {
    const auto it = std::find_if(ports.begin(), ports.end(), [&](const auto& p) { return p.id == id; });
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
    return findPort(stagePorts(def, reference.stage), reference.id);
}

// A declaration never repeats what the type already says, so an output carries display hints and
// relations only: values, constraints and editors belong to the editing side.
void validatePorts(const std::vector<PortDefinition>& ports, PortStage stage) {
    const bool output = stage == PortStage::Outputs;
    std::set<PortId> ids;

    for (const auto& port : ports) {
        if (!ids.insert(port.id).second) {
            throw ContractError("Duplicate " + std::string(stageName(stage)) + " port id: "
                                + std::to_string(port.id));
        }
        if (port.nameZh.empty()) {
            throw ContractError("Port name_zh must not be empty: " + portLabel(port.id));
        }

        if (port.typeFrom) {
            if (!output)
                throw ContractError("type_from requires a business output: " + portLabel(port.id));
            if (!isMissing(port.defaultValue) || !port.editor.empty() || port.minimum || port.maximum || port.step
                || port.decimals || !port.options.empty() || !port.optionsProvider.empty() || port.image
                || port.alignment || port.pathModeFrom || port.sameGeometryAs || !port.colorOrder.empty())
                throw ContractError("type_from cannot have concrete value or display attributes: " + portLabel(port.id));
            continue;
        }
        validateTypeSpec(port.type);

        if (output) {
            if (!port.editor.empty()) throw ContractError("editor is not an output field: " + portLabel(port.id));
            if (port.minimum.has_value()) throw ContractError("constraints.min is not an output field: " + portLabel(port.id));
            if (port.maximum.has_value()) throw ContractError("constraints.max is not an output field: " + portLabel(port.id));
            if (port.step.has_value()) throw ContractError("ui.step is not an output field: " + portLabel(port.id));
            if (port.decimals.has_value()) throw ContractError("ui.decimals is not an output field: " + portLabel(port.id));
            if (!port.options.empty()) throw ContractError("constraints.options is not an output field: " + portLabel(port.id));
            if (!port.optionsProvider.empty()) throw ContractError("ui.options_provider is not an output field: " + portLabel(port.id));
            if (port.alignment || port.pathModeFrom)
                throw ContractError("relations.alignment and relations.path_mode apply to inputs: " + portLabel(port.id));
        } else {
            if (!port.colorOrder.empty())
                throw ContractError("display.color_order applies only to outputs: " + portLabel(port.id));
        }

        if (!port.editor.empty()) {
            const std::set<std::string> validEditors
                = {"number", "text", "bool", "switch", "recursive", "file", "directory", "path", "array", "roi",
                   "rect"};
            if (!validEditors.contains(port.editor)) {
                throw ContractError("Unknown editor: " + port.editor + " on port " + portLabel(port.id));
            }
        }

        const bool pathEditor = port.editor == "file" || port.editor == "directory" || port.editor == "path";
        if (port.editor == "rect" && port.type.type != Type::ROI)
            throw ContractError("矩形编辑器要求 ROI 类型端口：" + portLabel(port.id));
        if (pathEditor && port.type != TypeSpec{Type::String})
            throw ContractError("路径编辑器要求 String 类型端口：" + portLabel(port.id));

        if (port.pathModeFrom && (output || port.editor != "path"))
            throw ContractError("relations.path_mode requires a path editor: " + portLabel(port.id));

        if (!port.tab.empty()) {
            const auto tabs = toolTabs();
            if (std::find(tabs.begin(), tabs.end(), port.tab) == tabs.end()) {
                throw ContractError("Unknown tab: " + port.tab + " on port " + portLabel(port.id));
            }
        }

        const bool numeric = (port.type.type == Type::Int || port.type.type == Type::Float);
        if (!numeric) {
            if (port.minimum.has_value() || port.maximum.has_value() || port.step.has_value() || port.decimals.has_value()) {
                throw ContractError("Numeric constraints (min, max, step, decimals) require an Int or Float port: " + portLabel(port.id));
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
                    throw ContractError("min must not exceed max on port: " + portLabel(port.id));
                }
            }
            if (port.step.has_value()) {
                validateNumericPortValue(*port.step, port.type.type, "step", port.id);
                const PortValue zero = port.type.type == Type::Int ? PortValue{std::int64_t{0}} : PortValue{0.0};
                if (!lessNumber(zero, *port.step, port.type.type)) {
                    throw ContractError("step must be positive on port: " + portLabel(port.id));
                }
            }
            if (port.decimals.has_value() && *port.decimals < 0) {
                throw ContractError("decimals must be non-negative on port: " + portLabel(port.id));
            }
            if (!isMissing(port.defaultValue)) {
                validateNumericPortValue(port.defaultValue, port.type.type, "default", port.id);
                if (port.minimum.has_value() && lessNumber(port.defaultValue, *port.minimum, port.type.type)) {
                    throw ContractError("Default is less than min on port: " + portLabel(port.id)
                                        + "（类型空值不满足约束时必须显式声明 default）");
                }
                if (port.maximum.has_value() && lessNumber(*port.maximum, port.defaultValue, port.type.type)) {
                    throw ContractError("Default exceeds max on port: " + portLabel(port.id));
                }
            }
        }

        for (const auto& opt : port.options) {
            if (numeric) {
                validateNumericPortValue(opt, port.type.type, "option", port.id);
                if (port.minimum.has_value() && lessNumber(opt, *port.minimum, port.type.type)) {
                    throw ContractError("Option is less than min on port: " + portLabel(port.id));
                }
                if (port.maximum.has_value() && lessNumber(*port.maximum, opt, port.type.type)) {
                    throw ContractError("Option exceeds max on port: " + portLabel(port.id));
                }
            } else {
                validateValue(port.type, opt);
            }
        }
        if (!port.options.empty() && !isMissing(port.defaultValue)) {
            const bool found = std::any_of(port.options.begin(), port.options.end(),
                [&](const auto& opt) { return equalValues(opt, port.defaultValue); });
            if (!found) {
                throw ContractError("default value is not one of declared options on port: " + portLabel(port.id));
            }
        }

        if (!port.optionsProvider.empty() && port.optionsProvider != "platform.gpu_devices") {
            throw ContractError("Unknown options_provider: " + port.optionsProvider + " on port " + portLabel(port.id));
        }

        if (port.image && port.type.type != Type::ROI)
            throw ContractError("relations.image requires an ROI port: " + portLabel(port.id));

        if (port.alignment && (stage != PortStage::Inputs || port.type.type != Type::ROI))
            throw ContractError("relations.alignment requires an ROI input: " + portLabel(port.id));

        if (!port.colorOrder.empty()) {
            if (port.type.type != Type::Image)
            {
                throw ContractError("display.color_order requires an Image output: " + portLabel(port.id));
            }
            const std::set<std::string> validColors = {"Gray", "BGR", "BGRA", "RGB", "RGBA"};
            if (!validColors.contains(port.colorOrder)) {
                throw ContractError("Unknown color_order: " + port.colorOrder + " on port " + portLabel(port.id));
            }
        }

        if (port.sameGeometryAs && port.type.type != Type::Image)
            throw ContractError("relations.same_geometry_as requires an Image output: " + portLabel(port.id));
    }
}

// The high-water counter must stay above every live number and inside the uint32 space.
void validateHighWater(const ToolDefinition &def, PortStage stage)
{
    const auto next = def.nextPortId(stage);
    const auto floor = floorId(stage);
    if (next < floor || next > kPortIdExhausted)
        throw ContractError("next_port_id." + std::string(stageName(stage)) + " 超出范围："
                            + std::to_string(next));
    for (const auto &port : stagePorts(def, stage))
        if (port.id >= next)
            throw ContractError("next_port_id." + std::string(stageName(stage)) + " 必须大于已用编号 "
                                + std::to_string(port.id));
}

} // namespace

void validateIdentifier(const std::string &value, bool isPort)
{
    const auto letter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    if (value.empty() || (!letter(value.front()) && value.front() != '_')
        || !std::all_of(value.begin(), value.end(), [&](char c) {
               return letter(c) || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
           }))
    {
        throw ContractError("ID must be a stable English identifier: " + value);
    }
    if (isPort && value.starts_with("__"))
        throw ContractError("Port ID uses reserved __ prefix: " + value);
}

PortRef parsePortRef(std::string_view text)
{
    const auto dot = text.find('.');
    if (dot == std::string_view::npos)
        throw ContractError("端口引用必须带阶段前缀（init./inputs./outputs.）：" + std::string(text));
    const auto stage = text.substr(0, dot);
    const auto port  = text.substr(dot + 1);
    if (port.empty() || port.find('.') != std::string_view::npos)
        throw ContractError("端口引用必须形如 inputs.7：" + std::string(text));
    PortId    id = 0;
    std::size_t consumed = 0;
    try
    {
        const auto value = std::stoull(std::string(port), &consumed);
        if (consumed != port.size() || value > std::numeric_limits<PortId>::max())
            throw ContractError("端口编号超出范围：" + std::string(text));
        id = static_cast<PortId>(value);
    }
    catch (const ContractError &)
    {
        throw;
    }
    catch (const std::exception &)
    {
        throw ContractError("端口引用必须使用数字编号：" + std::string(text));
    }
    if (stage == "init")
        return {PortStage::Init, id};
    if (stage == "inputs")
        return {PortStage::Inputs, id};
    if (stage == "outputs")
        return {PortStage::Outputs, id};
    throw ContractError("未知端口引用阶段：" + std::string(stage));
}

std::string formatPortRef(const PortRef& reference)
{
    return std::string(stageName(reference.stage)) + "." + std::to_string(reference.id);
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

std::vector<std::string_view> editorChoices(const TypeSpec &type)
{
    switch (type.type)
    {
    case Type::Int:
    case Type::Float:
        return {"number"};
    case Type::Bool:
        return {"bool", "switch"};
    case Type::String:
        return {"text", "file", "directory", "path"};
    case Type::ROI:
        return {"roi", "rect", "array"};
    case Type::Array:
        return {"array", "recursive"};
    case Type::Image:
        return {};
    }
    return {};
}

bool isDerivedInitParam(NodeModel model, PortId id) noexcept
{
    if (model == NodeModel::Expression)
        return id == kVariablesInitPort || id == kProgramInitPort;
    if (model == NodeModel::Condition)
        return id == kVariablesInitPort || id == kProgramInitPort || id == kConditionModePort;
    return false;
}

bool isReservedPort(PortStage stage, PortId id) noexcept
{
    if (stage == PortStage::Inputs)
        return id == kUpstreamStatusPort;
    if (stage == PortStage::Outputs)
        return id < 2;
    return false;
}

PortDefinition hostPort(PortStage stage, PortId id)
{
    PortDefinition port;
    port.id = id;
    if (stage == PortStage::Inputs && id == kUpstreamStatusPort)
    {
        port.nameZh       = "上层工具状态";
        port.type         = TypeSpec{Type::Int};
        port.defaultValue = PortValue{std::int64_t{0}};
        port.tab          = "基本输入";
        return port;
    }
    if (stage == PortStage::Outputs && id == kStatusCodePort)
    {
        port.nameZh = "状态码";
        port.type   = TypeSpec{Type::Int};
        return port;
    }
    if (stage == PortStage::Outputs && id == kStatusMessagePort)
    {
        port.nameZh = "状态消息";
        port.type   = TypeSpec{Type::String};
        return port;
    }
    throw ContractError("没有宿主端口：" + std::string(stageName(stage)) + " " + portLabel(id));
}

void stripHostPorts(ToolDefinition &def)
{
    for (auto *list : {&def.initParams, &def.inputs, &def.outputs})
    {
        const auto stage = list == &def.outputs ? PortStage::Outputs
                           : list == &def.inputs ? PortStage::Inputs
                                                 : PortStage::Init;
        std::erase_if(*list, [stage](const PortDefinition &port) { return isReservedPort(stage, port.id); });
    }
}

bool isInjectedInput(NodeModel model, PortId id) noexcept
{
    return model == NodeModel::Fixed && id == kUpstreamStatusPort;
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

PortId allocatePortId(ToolDefinition &def, PortStage stage)
{
    const auto next = def.nextPortId(stage);
    if (next > std::numeric_limits<PortId>::max())
        throw ContractError("端口编号已耗尽：" + std::string(stageName(stage)));
    def.setNextPortId(stage, next + 1);
    return static_cast<PortId>(next);
}

namespace {

std::vector<PortDefinition> &mutableStagePorts(ToolDefinition &def, PortStage stage)
{
    switch (stage)
    {
    case PortStage::Init:    return def.initParams;
    case PortStage::Inputs:  return def.inputs;
    case PortStage::Outputs: return def.outputs;
    }
    return def.inputs;
}

bool valueFitsType(const PortValue &value, const TypeSpec &type)
{
    if (isMissing(value))
        return true;
    try
    {
        validateValue(type, value);
        return true;
    }
    catch (const ContractError &)
    {
        return false;
    }
}

// Editor kinds are type affinities, not permissions: a value editor that cannot show the new type
// would silently misrepresent every field it renders. The generic list editor is declared for ROI
// as well as Array, which is how the installed tools already use it.
bool editorFitsType(const std::string &editor, const TypeSpec &type)
{
    if (editor.empty())
        return true;
    const auto choices = editorChoices(type);
    return std::find(choices.begin(), choices.end(), std::string_view{editor}) != choices.end();
}

bool sameReference(const PortRef &reference, PortStage stage, PortId id)
{
    return reference.stage == stage && reference.id == id;
}

} // namespace

std::vector<std::string> retypePort(ToolDefinition &def, PortStage stage, PortId id, TypeSpec newType)
{
    validateTypeSpec(newType);
    if (isReservedPort(stage, id))
        throw ContractError("宿主公共端口不能改类型：" + portLabel(id));

    auto      &ports = mutableStagePorts(def, stage);
    const auto found = std::find_if(ports.begin(), ports.end(), [id](const PortDefinition &port) { return port.id == id; });
    if (found == ports.end())
        throw ContractError("找不到要改型的参数：" + portLabel(id));

    PortDefinition &port        = *found;
    const PortRef   reference{stage, id};
    if (port.typeFrom)
        throw ContractError("派生类型参数不能直接改类型：" + portLabel(id));

    // A host output derives its type from these parameters; changing one would leave that output
    // without a type, so the edit is refused before anything is touched.
    for (const auto &output : def.outputs)
    {
        if (!output.typeFrom)
            continue;
        if (sameReference(output.typeFrom->flowParameter, stage, id)
            || sameReference(output.typeFrom->nodeParameter, stage, id)
            || sameReference(output.typeFrom->portParameter, stage, id))
            throw ContractError("参数被输出 " + portLabel(output.id) + " 的类型来源引用，不能改类型");
    }

    if (port.type == newType)
        return {};

    const TypeSpec           oldType = port.type;
    std::vector<std::string> cleared;
    const auto               note = [&cleared](std::string text) { cleared.push_back(std::move(text)); };

    port.type = newType;

    // A value the new type cannot hold falls back to the type's own empty value; a default that
    // survives is left exactly as authored.
    const bool explicitDefault = !isMissing(port.defaultValue) && !equalValues(port.defaultValue, defaultValue(oldType));
    if (!valueFitsType(port.defaultValue, newType))
    {
        port.defaultValue = defaultValue(newType);
        if (explicitDefault)
            note("默认值");
    }

    // Scalar constraints mean the same thing only inside one numeric kind: an int bound is not a
    // float bound, so crossing kinds clears them instead of reinterpreting the numbers.
    const bool keepsNumericConstraints
        = oldType.type == newType.type && (newType.type == Type::Int || newType.type == Type::Float);
    if (!keepsNumericConstraints)
    {
        if (port.minimum)
        {
            port.minimum.reset();
            note("最小值");
        }
        if (port.maximum)
        {
            port.maximum.reset();
            note("最大值");
        }
        if (port.step)
        {
            port.step.reset();
            note("步长");
        }
        if (port.decimals)
        {
            port.decimals.reset();
            note("小数位");
        }
        if (!port.options.empty())
        {
            port.options.clear();
            note("候选值");
        }
        if (!port.optionsProvider.empty())
        {
            port.optionsProvider.clear();
            note("候选来源");
        }
    }

    if (!editorFitsType(port.editor, newType))
    {
        port.editor.clear();
        note("编辑器");
    }

    // Relations a port carries describe what its own value is.
    if (port.image && newType.type != Type::ROI)
    {
        port.image.reset();
        note("图像关联");
    }
    if (port.alignment && (stage != PortStage::Inputs || newType.type != Type::ROI))
    {
        port.alignment.reset();
        note("对齐关系");
    }
    if (port.pathModeFrom && port.editor != "path")
    {
        port.pathModeFrom.reset();
        note("路径模式");
    }
    if (port.sameGeometryAs && newType.type != Type::Image)
    {
        port.sameGeometryAs.reset();
        note("同几何关系");
    }
    if (!port.colorOrder.empty() && newType.type != Type::Image)
    {
        port.colorOrder.clear();
        note("颜色次序");
    }

    // References other ports make to this one stay valid only while it still satisfies them.
    for (auto *list : {&def.initParams, &def.inputs, &def.outputs})
    {
        for (auto &other : *list)
        {
            if (&other == &port)
                continue;
            const auto pointsHere = [&](const std::optional<PortRef> &ref) { return ref && sameReference(*ref, stage, id); };
            const auto owner      = "端口 " + std::to_string(other.id) + " 的";
            if (pointsHere(other.image) && newType.type != Type::Image)
            {
                other.image.reset();
                note(owner + "图像关联");
            }
            if (pointsHere(other.sameGeometryAs) && newType.type != Type::Image)
            {
                other.sameGeometryAs.reset();
                note(owner + "同几何关系");
            }
            if (pointsHere(other.alignment) && newType != TypeSpec{Type::Array, DType::Float64, 3})
            {
                other.alignment.reset();
                note(owner + "对齐关系");
            }
            if (pointsHere(other.pathModeFrom) && newType != TypeSpec{Type::String})
            {
                other.pathModeFrom.reset();
                note(owner + "路径模式");
            }
        }
    }

    // Smart selection may only propose into a writable, non-image input.
    if (def.interaction && newType.type == Type::Image)
    {
        for (auto it = def.interaction->proposals.begin(); it != def.interaction->proposals.end();)
        {
            if (sameReference(it->second, stage, id))
            {
                it = def.interaction->proposals.erase(it);
                note("智能点选提议");
                continue;
            }
            ++it;
        }
    }

    return cleared;
}

std::vector<std::string> erasePort(ToolDefinition &def, PortStage stage, PortId id)
{
    if (isReservedPort(stage, id))
        throw ContractError("宿主公共端口不能删除：" + portLabel(id));

    auto       &ports = mutableStagePorts(def, stage);
    const auto  found = std::find_if(ports.begin(), ports.end(), [id](const PortDefinition &port) { return port.id == id; });
    if (found == ports.end())
        throw ContractError("找不到要删除的参数：" + portLabel(id));

    // A host output takes its type from this parameter; deleting it would leave that output without a
    // type, so the deletion is refused before anything is touched.
    for (const auto &output : def.outputs)
    {
        if (!output.typeFrom)
            continue;
        if (sameReference(output.typeFrom->flowParameter, stage, id)
            || sameReference(output.typeFrom->nodeParameter, stage, id)
            || sameReference(output.typeFrom->portParameter, stage, id))
            throw ContractError("参数被输出 " + portLabel(output.id) + " 的类型来源引用，不能删除");
    }

    std::vector<std::string> cleared;
    const auto               note = [&cleared](std::string text) { cleared.push_back(std::move(text)); };

    // References other ports make to this one would dangle; each is dropped and named, so the editor
    // reports the exact loss instead of failing validation with a dangling reference later.
    for (auto *list : {&def.initParams, &def.inputs, &def.outputs})
    {
        for (auto &other : *list)
        {
            if (&other == &*found)
                continue;
            const auto pointsHere = [&](const std::optional<PortRef> &ref) { return ref && sameReference(*ref, stage, id); };
            const auto owner      = "端口 " + std::to_string(other.id) + " 的";
            if (pointsHere(other.image))
            {
                other.image.reset();
                note(owner + "图像关联");
            }
            if (pointsHere(other.sameGeometryAs))
            {
                other.sameGeometryAs.reset();
                note(owner + "同几何关系");
            }
            if (pointsHere(other.alignment))
            {
                other.alignment.reset();
                note(owner + "对齐关系");
            }
            if (pointsHere(other.pathModeFrom))
            {
                other.pathModeFrom.reset();
                note(owner + "路径模式");
            }
        }
    }

    // A smart-selection offer into a port that no longer exists is a dangling entry the validator
    // rejects, exactly like a dangling relation.
    if (def.interaction)
    {
        for (auto it = def.interaction->proposals.begin(); it != def.interaction->proposals.end();)
        {
            if (sameReference(it->second, stage, id))
            {
                it = def.interaction->proposals.erase(it);
                note("智能点选提议");
                continue;
            }
            ++it;
        }
        for (auto it = def.interaction->inputs.begin(); it != def.interaction->inputs.end();)
        {
            if (sameReference(it->second, stage, id))
            {
                it = def.interaction->inputs.erase(it);
                note("交互参数映射");
                continue;
            }
            ++it;
        }
    }

    ports.erase(found);
    return cleared;
}

void normalizeToolDefinition(ToolDefinition &def)
{
    // Every contract carries the two status outputs, whatever the node model: the tool writes them
    // like any other port and the host never adds or rewrites one. Materialising here keeps a
    // hand-built contract and a parsed declaration identical.
    std::vector<PortDefinition> outputs;
    outputs.reserve(def.outputs.size() + 2);
    for (const PortId id : {kStatusCodePort, kStatusMessagePort})
        outputs.push_back(hostPort(PortStage::Outputs, id));
    for (auto &port : def.outputs)
        if (!isReservedPort(PortStage::Outputs, port.id))
            outputs.push_back(std::move(port));
    def.outputs = std::move(outputs);

    if (def.nodeModel == NodeModel::Fixed)
    {
        std::vector<PortDefinition> inputs;
        inputs.reserve(def.inputs.size() + 1);
        inputs.push_back(hostPort(PortStage::Inputs, kUpstreamStatusPort));
        for (auto &port : def.inputs)
        {
            if (!isReservedPort(PortStage::Inputs, port.id))
            {
                inputs.push_back(std::move(port));
                continue;
            }
            if (port.type != TypeSpec{Type::Int})
                throw ContractError("upstream_status 必须是 Int 输入：" + def.nameEn);
            if (port.typeFrom)
                throw ContractError("upstream_status 不能由 type_from 派生：" + def.nameEn);
            if (!port.options.empty() || !port.optionsProvider.empty())
                throw ContractError("upstream_status 不接受 options 约束：" + def.nameEn);
            if (port.minimum || port.maximum || port.step || port.decimals)
                throw ContractError("upstream_status 不接受数值范围约束：" + def.nameEn);
            if (!isMissing(port.defaultValue) && !equalValues(port.defaultValue, PortValue{std::int64_t{0}}))
                throw ContractError("upstream_status 的默认值必须为 0：" + def.nameEn);
        }
        def.inputs = std::move(inputs);
    }
    else
    {
        // A derived model keeps input 0 reserved but is never handed an upstream status.
        std::erase_if(def.inputs, [](const PortDefinition &port) {
            return isReservedPort(PortStage::Inputs, port.id);
        });
    }

    // The one place a port without an explicit default receives the type's empty value. A status
    // port keeps Missing: the run's status, not a 0, says whether the tool succeeded.
    for (auto *list : {&def.initParams, &def.inputs, &def.outputs})
        for (auto &port : *list)
            if (!port.typeFrom && !isReservedPort(list == &def.outputs ? PortStage::Outputs
                                                                     : list == &def.inputs ? PortStage::Inputs
                                                                                           : PortStage::Init,
                                                  port.id)
                && isMissing(port.defaultValue))
                port.defaultValue = defaultValue(port.type);

    // The counters are persisted state; normalisation only raises them above what is live.
    for (const auto stage : {PortStage::Init, PortStage::Inputs, PortStage::Outputs})
    {
        std::uint64_t next = std::max<std::uint64_t>(def.nextPortId(stage), floorId(stage));
        for (const auto &port : stagePorts(def, stage))
            next = std::max<std::uint64_t>(next, static_cast<std::uint64_t>(port.id) + 1);
        def.setNextPortId(stage, next);
    }
}

void validateToolDefinition(const ToolDefinition& def) {
    if (def.schemaVersion != 6)
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
    for (const auto stage : {PortStage::Init, PortStage::Inputs, PortStage::Outputs})
        validateHighWater(def, stage);

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
                throw ContractError("relations.path_mode must reference the same stage: " + portLabel(port.id));
            const auto* mode = findPort(*ports, port.pathModeFrom->id);
            if (!mode || mode->id == port.id || mode->type != TypeSpec{Type::String})
                throw ContractError("relations.path_mode must reference another String parameter: " + portLabel(port.id));
        }

    for (const auto *list : {&def.initParams, &def.inputs, &def.outputs})
        for (const auto& port : *list)
        {
            if (port.image)
            {
                if (list == &def.outputs && port.image->stage == PortStage::Outputs
                    && port.image->id == port.id)
                    throw ContractError("relations.image must not reference itself: " + portLabel(port.id));
                requireImage(*port.image, "relations.image");
            }
            if (port.sameGeometryAs)
                requireImage(*port.sameGeometryAs, "relations.same_geometry_as");
        }

    for (const auto &port : def.outputs) {
        if (port.typeFrom) {
            if (def.kind != ToolKind::Host)
                throw ContractError("type_from 仅适用于宿主工具：" + def.nameEn);
            for (const auto& reference : {port.typeFrom->flowParameter, port.typeFrom->nodeParameter})
            {
                if (reference.stage != PortStage::Init)
                    throw ContractError("type_from must reference initialization parameters: "
                                        + formatPortRef(reference));
                const auto *target = findPort(def.initParams, reference.id);
                if (!target || target->typeFrom || target->type != TypeSpec{Type::String})
                    throw ContractError("type_from must reference a String initialization parameter: "
                                        + formatPortRef(reference));
            }
            if (port.typeFrom->portParameter.stage != PortStage::Init)
                throw ContractError("type_from must reference initialization parameters: "
                                    + formatPortRef(port.typeFrom->portParameter));
            const auto *sourcePort = findPort(def.initParams, port.typeFrom->portParameter.id);
            if (!sourcePort || sourcePort->typeFrom || sourcePort->type != TypeSpec{Type::Int})
                throw ContractError("type_from 的端口参数必须是 Int 初始化参数："
                                    + formatPortRef(port.typeFrom->portParameter));
        }
    }

    for (const auto &port : def.inputs)
    {
        if (!port.alignment)
            continue;
        const auto *target = findPort(def, *port.alignment);
        if (!target || target->id == port.id || target->typeFrom
            || target->type != TypeSpec{Type::Array, DType::Float64, 3})
            throw ContractError("relations.alignment 必须引用另一个 Array(float64, rank=3) 输入：" + portLabel(port.id));
    }

    const auto requireParameter = [&](PortId id, const char *label)
    {
        const auto *parameter = findPort(def.initParams, id);
        if (!parameter || parameter->type != TypeSpec{Type::Array, DType::String, 2})
            throw ContractError(std::string("节点模型需要 vec_vec_string 初始化参数：") + label
                                + "（" + portLabel(id) + "）");
    };
    switch (def.nodeModel)
    {
    case NodeModel::Fixed:
        break;
    case NodeModel::Expression:
    case NodeModel::Condition:
        if (def.kind != ToolKind::PythonClass)
            throw ContractError("表达式与条件节点必须是 python_class 工具：" + def.nameEn);
        requireParameter(kVariablesInitPort, "variables");
        requireParameter(kProgramInitPort, "program");
        if (def.nodeModel == NodeModel::Condition)
        {
            const auto *mode = findPort(def.initParams, kConditionModePort);
            const bool  validMode
                = mode && mode->type == TypeSpec{Type::String} && mode->options.size() == 2
               && std::any_of(mode->options.begin(), mode->options.end(),
                              [](const auto &value) { return equalValues(value, PortValue{std::string{"formula"}}); })
               && std::any_of(mode->options.begin(), mode->options.end(),
                              [](const auto &value) { return equalValues(value, PortValue{std::string{"upstream"}}); })
               && equalValues(mode->defaultValue, PortValue{std::string{"formula"}});
            if (!validMode)
                throw ContractError("条件节点必须声明 condition_mode:string，选项 formula/upstream，默认 formula：" + def.nameEn);
            const std::array<PortId, 4> branches{2, 3, 4, 5};
            if (def.outputs.size() != 6
                || !std::all_of(branches.begin(), branches.end(), [&](PortId id) { return findPort(def.outputs, id); }))
                throw ContractError("条件节点必须声明两个布尔输出与两路状态输出：" + def.nameEn);
        }
        break;
    case NodeModel::Select:
        if (def.kind != ToolKind::Host)
            throw ContractError("合流节点必须是宿主工具：" + def.nameEn);
        if (!def.inputs.empty() || def.outputs.size() != 2)
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
            if (helper == 0 || !findPort(def, reference))
                throw ContractError("交互 interaction.inputs 引用了不存在的端口：" + formatPortRef(reference)
                                    + "（" + def.nameEn + "）");
        }
        for (const auto &[helper, reference] : def.interaction->proposals)
        {
            const auto *target = findPort(def, reference);
            if (helper == 0 || !target || reference.stage == PortStage::Outputs || target->type.type == Type::Image)
                throw ContractError("交互 interaction.proposals 引用了不可写输入：" + formatPortRef(reference)
                                    + "（" + def.nameEn + "）");
        }
    }
}

} // namespace visionrt
