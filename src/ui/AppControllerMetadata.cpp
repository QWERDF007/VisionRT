#include "ui/AppController_p.h"

#include <visionrt/sdk/ValueYaml.h>

namespace visionrt {
namespace workbench {
namespace {
QString kindName(ToolKind kind)
{
    switch (kind)
    {
    case ToolKind::CppFunction:
        return "cpp_function";
    case ToolKind::CppClass:
        return "cpp_class";
    case ToolKind::PythonFunction:
        return "python_function";
    case ToolKind::PythonClass:
        return "python_class";
    }
    return {};
}

QVariantMap portMetadata(const PortDefinition &port)
{
    QVariantMap row{
        {"id", q(port.id)},
        {"name", q(port.name)},
        {"type", typeName(port.type.type)},
        {"rank", port.type.rank},
        {"layout", port.type.layout == Layout::Dense ? "dense" : "batch_ragged"},
        {"dtype", QString{}},
        {"default", valueText(port.defaultValue, port.type)},
        {"editor", q(port.editor)},
        {"tab", q(port.tab)},
        {"group", q(port.group)},
        {"minimum", QString{}},
        {"maximum", QString{}},
        {"step", QString{}},
        {"decimals", port.decimals ? *port.decimals : -1},
        {"optionsProvider", q(port.optionsProvider)},
        {"imageInput", q(port.imageInput)},
        {"imagePort", q(port.imagePort)},
        {"colorOrder", q(port.colorOrder)},
        {"sameGeometryAsInput", q(port.sameGeometryAsInput)},
        {"system", port.id.starts_with("__")}
    };
    if (port.type.dtype)
    {
        static const char *names[] = {"uint8", "int32", "int64", "float32", "float64", "bool"};
        row["dtype"]               = QString::fromLatin1(names[static_cast<int>(*port.type.dtype)]);
    }
    if (port.minimum)
        row["minimum"] = valueText(*port.minimum, port.type);
    if (port.maximum)
        row["maximum"] = valueText(*port.maximum, port.type);
    if (port.step)
        row["step"] = valueText(*port.step, port.type);
    QVariantList options;
    for (const auto &option : port.options) options.push_back(valueText(option, port.type));
    row["options"] = options;
    return row;
}
} // namespace

QVariantMap metadata(const ToolDefinition &definition)
{
    QVariantMap result{
        {            "id",                    q(definition.id)},
        {   "displayName",           q(definition.displayName)},
        {       "version",               q(definition.version)},
        {          "kind",           kindName(definition.kind)},
        {      "category",              q(definition.category)},
        {   "subcategory",           q(definition.subcategory)},
        {       "builtin",                  definition.builtin},
        {"definitionPath", pathText(definition.definitionPath)},
        {  "registryName",          q(definition.registryName)},
        {        "symbol",                q(definition.symbol)},
        {        "script",                           QString{}}
    };
    if (!definition.script.empty())
        result["script"] = pathText(definition.script.lexically_relative(definition.definitionPath.parent_path()));
    auto ports = [](const auto &values)
    {
        QVariantList rows;
        for (const auto &port : values) rows.push_back(portMetadata(port));
        return rows;
    };
    result["inputs"]     = ports(definition.inputs);
    result["initParams"] = ports(definition.initParams);
    result["outputs"]    = ports(definition.outputs);
    return result;
}
} // namespace workbench

QVariantList AppController::toolDefinitions() const
{
    QVariantList result;
    for (const auto &definition : impl_->registry.definitions())
    {
        auto       item         = workbench::metadata(definition);
        const auto availability = impl_->registry.availability(definition.id);
        item["available"]       = availability.state == ToolAvailability::Available;
        item["message"]         = workbench::q(availability.message);
        result.push_back(item);
    }
    return result;
}

QVariantMap AppController::toolMetadata(const QString &toolId) const
{
    const auto *definition = impl_->registry.find(toolId.toStdString());
    return definition ? workbench::metadata(*definition) : QVariantMap{};
}
} // namespace visionrt
