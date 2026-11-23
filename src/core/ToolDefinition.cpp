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

PortId floorId(PortStage stage) noexcept
{
    return stage == PortStage::Init ? 0 : stage == PortStage::Inputs ? 1 : 2;
}

void validateNumericPortValue(const PortValue &val, Type type, const std::string &fieldName, PortId portId)
{
    const auto where = "，参数：" + portLabel(portId);
    if (type == Type::Int)
    {
        if (!std::holds_alternative<std::int64_t>(val))
        {
            throw ContractError(fieldName + " type mismatch (expected Int)" + where);
        }
    }
    else if (type == Type::Float)
    {
        if (!std::holds_alternative<double>(val))
        {
            throw ContractError(fieldName + " type mismatch (expected Float)" + where);
        }
        if (!std::isfinite(std::get<double>(val)))
        {
            throw ContractError(fieldName + " must be a finite numeric value" + where);
        }
    }
    else
    {
        throw ContractError(fieldName + " requires an Int or Float port" + where);
    }
}

bool lessNumber(const PortValue &left, const PortValue &right, Type type)
{
    if (type == Type::Int)
        return std::get<std::int64_t>(left) < std::get<std::int64_t>(right);
    return std::get<double>(left) < std::get<double>(right);
}

const PortDefinition *findPort(const std::vector<PortDefinition> &ports, PortId id)
{
    const auto it = std::find_if(ports.begin(), ports.end(), [&](const auto &p) { return p.id == id; });
    return it == ports.end() ? nullptr : &*it;
}

const std::vector<PortDefinition> &stagePorts(const ToolDefinition &def, PortStage stage)
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

const PortDefinition *findPort(const ToolDefinition &def, const PortRef &reference)
{
    return findPort(stagePorts(def, reference.stage), reference.id);
}

// A declaration never repeats what the type already says, so an output carries display hints and
// relations only: values, constraints and editors belong to the editing side.
void validatePorts(const std::vector<PortDefinition> &ports, PortStage stage)
{
    const bool       output = stage == PortStage::Outputs;
    std::set<PortId> ids;

    for (const auto &port : ports)
    {
        if (!ids.insert(port.id).second)
        {
            throw ContractError("Duplicate " + std::string(stageName(stage)) + " port id: " + std::to_string(port.id));
        }
        if (port.nameZh.empty())
        {
            throw ContractError("Port name_zh must not be empty: " + port.nameZh);
        }

        if (port.typeFrom)
        {
            if (!output)
                throw ContractError("type_from requires a business output: " + port.nameZh);
            if (!isMissing(port.defaultValue) || port.editor != EditorKind::Default || port.minimum || port.maximum
                || port.step || port.decimals || !port.options.empty() || port.optionsProvider != OptionsProvider::None
                || port.image || port.alignment || port.sameGeometryAs || port.colorOrder != ColorOrder::Automatic
                || port.imageVisible)
                throw ContractError("type_from cannot have concrete value or display attributes: " + port.nameZh);
            continue;
        }
        validateTypeSpec(port.type);

        if (output)
        {
            if (port.editor != EditorKind::Default || port.minimum || port.maximum || port.step || port.decimals
                || !port.options.empty() || port.optionsProvider != OptionsProvider::None || port.alignment)
                throw ContractError("输出端口不能包含输入编辑配置：" + port.nameZh);
        }
        else if (port.colorOrder != ColorOrder::Automatic)
        {
            throw ContractError("display.color_order 仅属于输出端口：" + port.nameZh);
        }
        const bool pathEditor = port.editor == EditorKind::Path;
        if (port.editor == EditorKind::Rect && port.type.type != Type::ROI)
            throw ContractError("矩形编辑器要求 ROI 类型端口：" + port.nameZh);
        if (pathEditor && port.type != TypeSpec{Type::String})
            throw ContractError("路径编辑器要求 String 类型端口：" + port.nameZh);

        const bool numeric = (port.type.type == Type::Int || port.type.type == Type::Float);
        if (!numeric)
        {
            if (port.minimum.has_value() || port.maximum.has_value() || port.step.has_value()
                || port.decimals.has_value())
            {
                throw ContractError("Numeric constraints (min, max, step, decimals) require an Int or Float port: "
                                    + port.nameZh);
            }
            if (!isMissing(port.defaultValue))
            {
                validateValue(port.type, port.defaultValue);
            }
        }
        else
        {
            if (port.minimum.has_value())
            {
                validateNumericPortValue(*port.minimum, port.type.type, "min", port.id);
            }
            if (port.maximum.has_value())
            {
                validateNumericPortValue(*port.maximum, port.type.type, "max", port.id);
            }
            if (port.minimum.has_value() && port.maximum.has_value())
            {
                if (lessNumber(*port.maximum, *port.minimum, port.type.type))
                {
                    throw ContractError("最小值不能大于最大值：" + port.nameZh);
                }
            }
            if (port.step.has_value())
            {
                validateNumericPortValue(*port.step, port.type.type, "step", port.id);
                const PortValue zero = port.type.type == Type::Int ? PortValue{std::int64_t{0}} : PortValue{0.0};
                if (!lessNumber(zero, *port.step, port.type.type))
                {
                    throw ContractError("步长必须大于零：" + port.nameZh);
                }
            }
            if (port.decimals.has_value() && *port.decimals < 0)
            {
                throw ContractError("小数位数不能为负数：" + port.nameZh);
            }
            if (!isMissing(port.defaultValue))
            {
                validateNumericPortValue(port.defaultValue, port.type.type, "default", port.id);
                if (port.minimum.has_value() && lessNumber(port.defaultValue, *port.minimum, port.type.type))
                {
                    throw ContractError("默认值小于允许的最小值：" + port.nameZh
                                        + "（类型空值不满足约束时必须显式声明 default）");
                }
                if (port.maximum.has_value() && lessNumber(*port.maximum, port.defaultValue, port.type.type))
                {
                    throw ContractError("默认值大于允许的最大值：" + port.nameZh);
                }
            }
        }

        for (const auto &opt : port.options)
        {
            if (numeric)
            {
                validateNumericPortValue(opt, port.type.type, "option", port.id);
                if (port.minimum.has_value() && lessNumber(opt, *port.minimum, port.type.type))
                {
                    throw ContractError("Option is less than min on port: " + port.nameZh);
                }
                if (port.maximum.has_value() && lessNumber(*port.maximum, opt, port.type.type))
                {
                    throw ContractError("Option exceeds max on port: " + port.nameZh);
                }
            }
            else
            {
                validateValue(port.type, opt);
            }
        }
        if (!port.options.empty() && !isMissing(port.defaultValue))
        {
            const bool found = std::any_of(port.options.begin(), port.options.end(),
                                           [&](const auto &opt) { return equalValues(opt, port.defaultValue); });
            if (!found)
            {
                throw ContractError("默认值不在可选范围内：" + port.nameZh);
            }
        }

        if (port.image && port.type.type != Type::ROI)
            throw ContractError("relations.image requires an ROI port: " + port.nameZh);

        if (port.alignment && (stage != PortStage::Inputs || port.type.type != Type::ROI))
            throw ContractError("relations.alignment requires an ROI input: " + port.nameZh);

        if (port.colorOrder != ColorOrder::Automatic && port.type.type != Type::Image)
            throw ContractError("display.color_order requires an Image output: " + port.nameZh);

        if (port.imageVisible && port.type.type != Type::Image)
            throw ContractError("display.visible requires an Image port: " + port.nameZh);

        if (port.sameGeometryAs && port.type.type != Type::Image)
            throw ContractError("relations.same_geometry_as requires an Image output: " + port.nameZh);
    }
}

} // namespace

void validateIdentifier(const std::string &value, bool isPort)
{
    const auto letter = [](char c)
    {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    };
    if (value.empty() || (!letter(value.front()) && value.front() != '_')
        || !std::all_of(value.begin(), value.end(), [&](char c)
                        { return letter(c) || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-'; }))
    {
        throw ContractError("ID must be a stable English identifier: " + value);
    }
    if (isPort && value.starts_with("__"))
        throw ContractError("Port ID uses reserved __ prefix: " + value);
}

std::string categoryPath(const ToolDefinition &def)
{
    std::string path;
    for (const auto &name : def.categories) path += (path.empty() ? "" : "/") + name;
    return path;
}

const char *editorLabel(EditorKind editor)
{
    static constexpr const char *labels[] = {"默认", "数值", "文本", "布尔",     "开关",     "递归",   "路径",
                                             "数组", "区域", "矩形", "条件公式", "上层条件", "表达式", "合流类型"};
    return labels[static_cast<int>(editor)];
}

std::vector<EditorKind> editorChoices(const TypeSpec &type)
{
    switch (type.type)
    {
    case Type::Int:
    case Type::Float:
        return {EditorKind::Number};
    case Type::Bool:
        return {EditorKind::Bool, EditorKind::Switch};
    case Type::String:
        return {EditorKind::Text, EditorKind::Path};
    case Type::ROI:
        return {EditorKind::Roi, EditorKind::Rect, EditorKind::Array};
    case Type::Array:
        return {EditorKind::Array, EditorKind::Recursive};
    case Type::Image:
        return {};
    }
    return {};
}

void stripHostPorts(ToolDefinition &def)
{
    for (auto *list : {&def.initParams, &def.inputs, &def.outputs})
    {
        const auto stage = list == &def.outputs ? PortStage::Outputs
                         : list == &def.inputs  ? PortStage::Inputs
                                                : PortStage::Init;
        std::erase_if(*list, [stage](const PortDefinition &port) { return isReservedPort(stage, port.id); });
    }
}

std::span<const std::string_view> toolTabs() noexcept
{
    static constexpr std::array<std::string_view, 4> tabs{"基本输入", "预处理", "检测", "结果处理"};
    return tabs;
}

PortValue portDefault(const PortDefinition &port)
{
    if (const auto *array = std::get_if<Array>(&port.defaultValue))
        return array->clone();
    return port.defaultValue;
}

PortId nextFreePortId(const ToolDefinition &def, PortStage stage)
{
    std::set<PortId> used;
    for (const auto &port : stagePorts(def, stage)) used.insert(port.id);
    for (std::uint64_t candidate = floorId(stage); candidate <= std::numeric_limits<PortId>::max(); ++candidate)
        if (!used.contains(static_cast<PortId>(candidate)))
            return static_cast<PortId>(candidate);
    throw ContractError("端口编号已耗尽：" + std::string(stageName(stage)));
}

namespace {

std::vector<PortDefinition> &mutableStagePorts(ToolDefinition &def, PortStage stage)
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
bool editorFitsType(EditorKind editor, const TypeSpec &type)
{
    if (editor == EditorKind::Default)
        return true;
    const auto choices = editorChoices(type);
    return std::find(choices.begin(), choices.end(), editor) != choices.end();
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
    const auto found
        = std::find_if(ports.begin(), ports.end(), [id](const PortDefinition &port) { return port.id == id; });
    if (found == ports.end())
        throw ContractError("找不到要改型的参数：" + portLabel(id));

    PortDefinition &port = *found;
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
    const auto               note = [&cleared](std::string text)
    {
        cleared.push_back(std::move(text));
    };

    port.type = newType;

    // A value the new type cannot hold falls back to the type's own empty value; a default that
    // survives is left exactly as authored.
    const bool explicitDefault
        = !isMissing(port.defaultValue) && !equalValues(port.defaultValue, defaultValue(oldType));
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
        if (port.optionsProvider != OptionsProvider::None)
        {
            port.optionsProvider = OptionsProvider::None;
            note("候选来源");
        }
    }

    if (!editorFitsType(port.editor, newType))
    {
        port.editor = EditorKind::Default;
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
    if (port.sameGeometryAs && newType.type != Type::Image)
    {
        port.sameGeometryAs.reset();
        note("同几何关系");
    }
    if (port.colorOrder != ColorOrder::Automatic && newType.type != Type::Image)
    {
        port.colorOrder = ColorOrder::Automatic;
        note("颜色次序");
    }
    if (port.imageVisible && newType.type != Type::Image)
    {
        port.imageVisible = false;
        note("图像显示");
    }

    // References other ports make to this one stay valid only while it still satisfies them.
    for (auto *list : {&def.initParams, &def.inputs, &def.outputs})
    {
        for (auto &other : *list)
        {
            if (&other == &port)
                continue;
            const auto pointsHere = [&](const std::optional<PortRef> &ref)
            {
                return ref && sameReference(*ref, stage, id);
            };
            const auto owner = "端口 " + std::to_string(other.id) + " 的";
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
        }
    }

    // Smart selection may only propose into a writable, non-image input.

    // The declaration-driven scope names one picture and writes into typed targets. A picture that
    // stops being an Image takes the whole point-selection capability with it; an image target that
    // can no longer hold a value is dropped from the write list.

    return cleared;
}

std::vector<std::string> erasePort(ToolDefinition &def, PortStage stage, PortId id)
{
    if (isReservedPort(stage, id))
        throw ContractError("宿主公共端口不能删除：" + portLabel(id));

    auto      &ports = mutableStagePorts(def, stage);
    const auto found
        = std::find_if(ports.begin(), ports.end(), [id](const PortDefinition &port) { return port.id == id; });
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
    const auto               note = [&cleared](std::string text)
    {
        cleared.push_back(std::move(text));
    };

    // References other ports make to this one would dangle; each is dropped and named, so the editor
    // reports the exact loss instead of failing validation with a dangling reference later.
    for (auto *list : {&def.initParams, &def.inputs, &def.outputs})
    {
        for (auto &other : *list)
        {
            if (&other == &*found)
                continue;
            const auto pointsHere = [&](const std::optional<PortRef> &ref)
            {
                return ref && sameReference(*ref, stage, id);
            };
            const auto owner = "端口 " + std::to_string(other.id) + " 的";
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
    for (const PortId id : {kStatusCodePort, kStatusMessagePort}) outputs.push_back(hostPort(PortStage::Outputs, id));
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
            if (!port.options.empty() || port.optionsProvider != OptionsProvider::None)
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
        std::erase_if(def.inputs,
                      [](const PortDefinition &port) { return isReservedPort(PortStage::Inputs, port.id); });
    }

    // The one place a port without an explicit default receives the type's empty value. A status
    // port keeps Missing: the run's status, not a 0, says whether the tool succeeded.
    for (auto *list : {&def.initParams, &def.inputs, &def.outputs})
        for (auto &port : *list)
            if (!port.typeFrom
                && !isReservedPort(list == &def.outputs  ? PortStage::Outputs
                                   : list == &def.inputs ? PortStage::Inputs
                                                         : PortStage::Init,
                                   port.id)
                && isMissing(port.defaultValue))
                port.defaultValue = defaultValue(port.type);
}

void validateToolDefinition(const ToolDefinition &def)
{
    validateIdentifier(def.nameEn, false);
    if (def.nameZh.empty())
    {
        throw ContractError("Tool name_zh must not be empty: " + def.nameEn);
    }
    if (def.version.empty())
    {
        throw ContractError("Tool version must not be empty: " + def.nameEn);
    }

    const int kindInt = static_cast<int>(def.kind);
    if (kindInt < 0 || kindInt > 3)
    {
        throw ContractError("Unknown tool kind on: " + def.nameEn);
    }

    if (def.kind == ToolKind::PythonFunction || def.kind == ToolKind::PythonClass)
    {
        if (def.entry.package.empty() || def.entry.file.empty() || def.entry.symbol.empty())
        {
            throw ContractError(
                "Python tool requires implementation.package, implementation.file and implementation.symbol: "
                + def.nameEn);
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
    if (def.kind == ToolKind::PythonFunction)
    {
        if (!def.initParams.empty())
        {
            throw ContractError("Function tool cannot have init params: " + def.nameEn);
        }
    }
    validatePorts(def.inputs, PortStage::Inputs);
    validatePorts(def.outputs, PortStage::Outputs);

    if (def.training)
    {
        const auto *frame = findPort(def.inputs, def.training->image.id);
        if (def.training->image.stage != PortStage::Inputs || !frame || frame->type.type != Type::Image)
            throw ContractError("training.image must reference an Image input: " + def.nameEn);
        if (def.training->records.fields.empty())
            throw ContractError("training.records.fields cannot be empty: " + def.nameEn);
        if (def.training->records.maxCount < 0)
            throw ContractError("training.records.max cannot be negative: " + def.nameEn);
        int roiFields = 0;
        for (const auto &field : def.training->records.fields)
        {
            if (field.id == 0)
                throw ContractError("training field ids must be positive: " + def.nameEn);
            if (field.nameZh.empty())
                throw ContractError("training fields need a name: " + def.nameEn);
            if (field.type.type == Type::Image || field.type.type == Type::TrainingSet)
                throw ContractError("training fields cannot hold an image or a training set: " + def.nameEn);
            if (field.roi)
            {
                ++roiFields;
                if (field.type.type != Type::ROI)
                    throw ContractError("the canvas training field must be a ROI: " + def.nameEn);
            }
            if (field.minRegions < 0 || field.maxRegions < 0
                || (field.maxRegions != 0 && field.minRegions > field.maxRegions))
                throw ContractError("training field region bounds are invalid: " + def.nameEn);
        }
        if (roiFields > 1)
            throw ContractError("a training tool declares at most one canvas field: " + def.nameEn);
        // Training parameters are typed values with the same limits and controls as a value port;
        // one validation path keeps their defaults, steps and options consistent with parameters.
        std::vector<PortDefinition> parameters;
        parameters.reserve(def.training->parameters.size());
        for (const auto &declared : def.training->parameters)
        {
            if (declared.type.type == Type::Image || declared.type.type == Type::TrainingSet)
                throw ContractError("training parameters cannot hold an image or a training set: " + def.nameEn);
            PortDefinition parameter;
            parameter.id           = declared.id;
            parameter.nameZh       = declared.nameZh;
            parameter.type         = declared.type;
            parameter.defaultValue = declared.defaultValue;
            parameter.editor       = declared.editor;
            parameter.step         = declared.step;
            parameter.decimals     = declared.decimals;
            parameter.minimum      = declared.minimum;
            parameter.maximum      = declared.maximum;
            parameter.options      = declared.options;
            parameters.push_back(std::move(parameter));
        }
        validatePorts(parameters, PortStage::Init);
        if (def.training->call.tool.empty())
            throw ContractError("training.call 需要训练器：" + def.nameEn);
        {
            if (def.training->writes.empty() && def.training->artifacts.empty())
                throw ContractError("training.call needs at least one write or artifact: " + def.nameEn);
            for (const auto &input : def.training->call.inputs)
            {
                if (input.port == 0)
                    throw ContractError("training.call.inputs needs a port: " + def.nameEn);
                if (const auto *role = std::get_if<TrainingSetSource>(&input.source))
                {
                    continue;
                }
                if (const auto *field = std::get_if<TrainingFieldRef>(&input.source))
                {
                    const bool declared
                        = std::any_of(def.training->records.fields.begin(), def.training->records.fields.end(),
                                      [&](const TrainingFieldSpec &entry) { return entry.id == field->id; });
                    if (!declared)
                        throw ContractError("training.call 引用了未声明的训练字段：" + std::to_string(field->id) + "（"
                                            + def.nameEn + "）");
                    continue;
                }
                if (const auto *parameter = std::get_if<TrainingParameterRef>(&input.source))
                {
                    const bool declared
                        = std::any_of(def.training->parameters.begin(), def.training->parameters.end(),
                                      [&](const TrainingParameterSpec &entry) { return entry.id == parameter->id; });
                    if (!declared)
                        throw ContractError("training.call 引用了未声明的训练参数：" + std::to_string(parameter->id)
                                            + "（" + def.nameEn + "）");
                    continue;
                }
                if (const auto *literal = std::get_if<TypedLiteral>(&input.source))
                {
                    if (isMissing(literal->value))
                        throw ContractError("training.call 的字面量必须有值：" + def.nameEn);
                    validateValue(literal->type, literal->value);
                    continue;
                }
                const auto *reference = std::get_if<PortRef>(&input.source);
                if (!reference || reference->stage == PortStage::Outputs || !findPort(def, *reference))
                    throw ContractError("training.call 输入引用了不存在的端口：" + def.nameEn);
            }
            for (const auto &write : def.training->writes)
            {
                if (write.sourcePort == 0)
                    throw ContractError("training.writes 的来源端口必须大于 0：" + def.nameEn);
                if (write.target.stage == PortStage::Outputs || isReservedPort(write.target.stage, write.target.id))
                    throw ContractError("training.writes 引用了不可写端口：" + formatPortRef(write.target) + "（"
                                        + def.nameEn + "）");
                const auto *port = findPort(def, write.target);
                if (!port || port->type.type == Type::Image)
                    throw ContractError("training.writes 引用了不可写入的端口：" + formatPortRef(write.target) + "（"
                                        + def.nameEn + "）");
            }
            for (const auto &artifact : def.training->artifacts)
            {
                if (artifact.id.empty() || artifact.format.empty() || artifact.sourcePort == 0)
                    throw ContractError("training.artifacts 需要 id、format 和来源端口：" + def.nameEn);
                if (artifact.target.stage != PortStage::Init || isReservedPort(PortStage::Init, artifact.target.id))
                    throw ContractError("training.artifacts 必须写入初始化参数：" + formatPortRef(artifact.target)
                                        + "（" + def.nameEn + "）");
                const auto *port = findPort(def.initParams, artifact.target.id);
                if (!port || port->type != TypeSpec{Type::String})
                    throw ContractError("training.artifacts 目标必须是 String 初始化参数："
                                        + formatPortRef(artifact.target) + "（" + def.nameEn + "）");
            }
        }
    }

    const auto requireImage = [&](const PortRef &reference, const char *context)
    {
        const auto *target = findPort(def, reference);
        if (!target || target->type.type != Type::Image)
            throw ContractError(std::string(context) + " must reference an Image port: " + formatPortRef(reference));
    };

    for (const auto *list : {&def.initParams, &def.inputs, &def.outputs})
        for (const auto &port : *list)
        {
            if (port.image)
            {
                if (list == &def.outputs && port.image->stage == PortStage::Outputs && port.image->id == port.id)
                    throw ContractError("relations.image must not reference itself: " + port.nameZh);
                requireImage(*port.image, "relations.image");
            }
            if (port.sameGeometryAs)
                requireImage(*port.sameGeometryAs, "relations.same_geometry_as");
        }

    for (const auto &port : def.outputs)
    {
        if (port.typeFrom)
        {
            if (def.kind != ToolKind::Host)
                throw ContractError("type_from 仅适用于宿主工具：" + def.nameEn);
            for (const auto &reference : {port.typeFrom->flowParameter, port.typeFrom->nodeParameter})
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
            throw ContractError("relations.alignment 必须引用另一个 Array(float64, rank=3) 输入：" + port.nameZh);
    }

    const auto requireParameter = [&](PortId id, const char *label)
    {
        const auto *parameter = findPort(def.initParams, id);
        if (!parameter || parameter->type != TypeSpec{Type::Array, DType::String, 2})
            throw ContractError(std::string("节点模型需要二维字符串数组初始化参数：") + label + "（" + portLabel(id)
                                + "）");
    };
    switch (def.nodeModel)
    {
    case NodeModel::Fixed:
        break;
    case NodeModel::Expression:
    case NodeModel::Condition:
        if (def.kind != ToolKind::PythonClass)
            throw ContractError("表达式与条件节点必须使用 Python 类：" + def.nameEn);
        requireParameter(kVariablesInitPort, "variables");
        requireParameter(kProgramInitPort, "program");
        if (def.nodeModel == NodeModel::Condition)
        {
            const auto *mode = findPort(def.initParams, kConditionModePort);
            const bool  validMode
                = mode && mode->type == TypeSpec{Type::Int} && mode->options.size() == 2
               && std::any_of(
                      mode->options.begin(), mode->options.end(), [](const auto &value)
                      { return equalValues(value, PortValue{std::int64_t{static_cast<int>(BindingMode::Literal)}}); })
               && std::any_of(
                      mode->options.begin(), mode->options.end(), [](const auto &value)
                      { return equalValues(value, PortValue{std::int64_t{static_cast<int>(BindingMode::Upstream)}}); })
               && equalValues(mode->defaultValue, PortValue{std::int64_t{static_cast<int>(BindingMode::Literal)}});
            if (!validMode)
                throw ContractError("条件节点必须声明整数条件来源，选项 Literal/Upstream，默认 Literal：" + def.nameEn);
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

    if (def.visibility == ToolVisibility::Internal)
    {
        if (def.nodeModel != NodeModel::Fixed)
            throw ContractError("辅助工具必须是固定节点且不能声明交互或能力映射：" + def.nameEn);
        if (def.kind == ToolKind::Host)
            throw ContractError("辅助工具不能是宿主工具：" + def.nameEn);
    }
}

} // namespace visionrt
