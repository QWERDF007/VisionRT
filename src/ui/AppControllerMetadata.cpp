#include "ui/AppController_p.h"
#include <visionrt/sdk/ValueYaml.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <algorithm>

namespace visionrt {
namespace workbench {
namespace {
QString kindName(ToolKind kind) {
    switch (kind) {
    case ToolKind::CppFunction: return "cpp_function";
    case ToolKind::CppClass: return "cpp_class";
    case ToolKind::PythonFunction: return "python_function";
    case ToolKind::PythonClass: return "python_class";
    }
    return {};
}
QVariantMap portMetadata(const PortDefinition& port) {
    QVariantMap row{{"id", q(port.id)}, {"name", q(port.name)}, {"type", typeName(port.type.type)},
        {"rank", port.type.rank}, {"layout", port.type.layout == Layout::Dense ? "dense" : "batch_ragged"},
        {"dtype", QString{}}, {"default", valueText(port.defaultValue, port.type)}, {"editor", q(port.editor)},
        {"tab", q(port.tab)}, {"group", q(port.group)}, {"minimum", QString{}}, {"maximum", QString{}},
        {"step", QString{}}, {"decimals", port.decimals ? *port.decimals : -1}, {"optionsProvider", q(port.optionsProvider)},
        {"imageInput", q(port.imageInput)}, {"imagePort", q(port.imagePort)}, {"colorOrder", q(port.colorOrder)},
        {"sameGeometryAsInput", q(port.sameGeometryAsInput)}, {"system", port.id.starts_with("__")}};
    if (port.type.dtype) {
        static const char* names[] = {"uint8", "int32", "int64", "float32", "float64", "bool"};
        row["dtype"] = QString::fromLatin1(names[static_cast<int>(*port.type.dtype)]);
    }
    if (port.minimum) row["minimum"] = valueText(*port.minimum, port.type);
    if (port.maximum) row["maximum"] = valueText(*port.maximum, port.type);
    if (port.step) row["step"] = valueText(*port.step, port.type);
    QVariantList options; for (const auto& option : port.options) options.push_back(valueText(option, port.type));
    row["options"] = options; return row;
}
}
QVariantMap metadata(const ToolDefinition& definition) {
    QVariantMap result{{"id", q(definition.id)}, {"displayName", q(definition.displayName)}, {"version", q(definition.version)},
        {"kind", kindName(definition.kind)}, {"category", q(definition.category)}, {"subcategory", q(definition.subcategory)},
        {"builtin", definition.builtin}, {"definitionPath", pathText(definition.definitionPath)},
        {"registryName", q(definition.registryName)}, {"symbol", q(definition.symbol)}, {"script", QString{}}};
    if (!definition.script.empty()) result["script"] = pathText(definition.script.lexically_relative(definition.definitionPath.parent_path()));
    auto ports = [](const auto& values) { QVariantList rows; for (const auto& port : values) rows.push_back(portMetadata(port)); return rows; };
    result["inputs"] = ports(definition.inputs); result["initParams"] = ports(definition.initParams); result["outputs"] = ports(definition.outputs);
    return result;
}
YAML::Node metadataYaml(const QVariantMap& map) {
    YAML::Node document(YAML::NodeType::Map);
    document["schema_version"] = 1;
    for (const auto& [field, key] : {std::pair{"id", "tool_id"}, {"displayName", "display_name"}, {"version", "version"}, {"kind", "kind"}})
        document[key] = map.value(field).toString().toStdString();
    const bool cpp = map.value("kind").toString().startsWith("cpp_");
    if (cpp) document["entry"]["registry_name"] = map.value("registryName").toString().toStdString();
    else {
        document["entry"]["file"] = map.value("script").toString().toStdString();
        document["entry"]["symbol"] = map.value("symbol").toString().toStdString();
    }
    for (const auto& [field, key] : {std::pair{"inputs", "inputs"}, {"initParams", "init_params"}, {"outputs", "outputs"}}) {
        YAML::Node rows(YAML::NodeType::Sequence);
        const bool output = std::string_view(key) == "outputs";
        for (const auto& item : map.value(field).toList()) {
            const auto values = item.toMap();
            const auto id = values.value("id").toString();
            if (output && (id == "__status_code" || id == "__status_message")) continue;
            YAML::Node row(YAML::NodeType::Map);
            for (const auto* name : {"id", "name", "type"}) row[name] = values.value(name).toString().toStdString();
            const auto type = values.value("type").toString();
            if (type == "NumericArray" || type == "StringArray") {
                row["rank"] = values.value("rank").toInt();
                row["layout"] = values.value("layout", "dense").toString().toStdString();
                if (type == "NumericArray") row["dtype"] = values.value("dtype").toString().toStdString();
            }
            if (!output) {
                const auto defaultText = values.value("default").toString();
                if (!defaultText.isEmpty()) row["default"] = YAML::Load(defaultText.toStdString());
                for (const auto& [source, destination] : {std::pair{"editor", "editor"}, {"tab", "tab"}, {"group", "group"},
                        {"optionsProvider", "options_provider"}, {"imageInput", "image_input"}}) {
                    const auto text = values.value(source).toString(); if (!text.isEmpty()) row[destination] = text.toStdString();
                }
                for (const auto& [source, destination] : {std::pair{"minimum", "min"}, {"maximum", "max"}, {"step", "step"}}) {
                    const auto text = values.value(source).toString(); if (!text.isEmpty()) row[destination] = YAML::Load(text.toStdString());
                }
                if (values.value("decimals", -1).toInt() >= 0) row["decimals"] = values.value("decimals").toInt();
                if (!values.value("options").toList().isEmpty()) {
                    YAML::Node options(YAML::NodeType::Sequence);
                    for (const auto& option : values.value("options").toList()) options.push_back(YAML::Load(option.toString().toStdString()));
                    row["options"] = options;
                }
            } else {
                if (!values.value("imagePort").toString().isEmpty()) row["image_port"] = values.value("imagePort").toString().toStdString();
                for (const auto& [source, destination] : {std::pair{"colorOrder", "color_order"}, {"sameGeometryAsInput", "same_geometry_as_input"}})
                    if (!values.value(source).toString().isEmpty()) row["display"][destination] = values.value(source).toString().toStdString();
            }
            rows.push_back(row);
        }
        document[key] = rows;
    }
    return document;
}
}
namespace {
std::filesystem::path component(const QString& value) {
    if (value.trimmed().isEmpty() || value == "." || value == ".." || value.contains('/') || value.contains('\\') || value.contains(':'))
        throw ContractError("Category and subcategory must be nonempty directory names");
    return workbench::localPath(value);
}
struct Candidate { ToolDefinition definition; YAML::Node yaml; std::filesystem::path previous; };
Candidate candidate(const QVariantMap& metadata, const ToolRegistry& registry, const std::filesystem::path& application) {
    if (metadata.value("builtin").toBool()) throw ContractError("Builtin metadata is read-only; save a copy with a new tool ID");
    const auto id = metadata.value("id").toString();
    if (id.isEmpty() || id.contains('/') || id.contains('\\') || id.contains(':')) throw ContractError("Invalid tool ID");
    const auto category = component(metadata.value("category").toString());
    const auto subcategory = component(metadata.value("subcategory").toString());
    const auto target = application / "tools" / "custom" / category / subcategory / workbench::localPath(id + ".yaml");
    auto yaml = workbench::metadataYaml(metadata);
    auto definition = parseDefinitionNode(yaml, target, false);
    registry.validateContract(definition);
    std::filesystem::path previous;
    const auto source = metadata.value("definitionPath").toString();
    if (const auto* existing = registry.find(definition.id)) {
        if (existing->builtin) throw ContractError("Builtin tool ID cannot be overwritten; choose a new ID");
        if (source.isEmpty() || workbench::localPath(source) != existing->definitionPath.lexically_normal()) throw ContractError("Duplicate tool ID");
        previous = existing->definitionPath;
        if (existing->kind != definition.kind) throw ContractError("Existing tool kind cannot be changed");
    } else if (!source.isEmpty()) {
        for (const auto& existing : registry.definitions()) if (existing.definitionPath == workbench::localPath(source))
            throw ContractError("Existing tool ID cannot be renamed; save a copy with an empty definitionPath");
    }
    if (target != previous && QFileInfo::exists(workbench::pathText(target))) throw ContractError("Destination YAML already exists");
    return {std::move(definition), std::move(yaml), std::move(previous)};
}
}
QVariantList AppController::toolDefinitions() const {
    QVariantList result;
    for (const auto& definition : impl_->registry.definitions()) {
        auto item = workbench::metadata(definition);
        const auto availability = impl_->registry.availability(definition.id);
        item["available"] = availability.state == ToolAvailability::Available; item["message"] = workbench::q(availability.message);
        result.push_back(item);
    }
    return result;
}
QVariantMap AppController::toolMetadata(const QString& toolId) const {
    const auto* definition = impl_->registry.find(toolId.toStdString()); return definition ? workbench::metadata(*definition) : QVariantMap{};
}
QVariantList AppController::cppImplementations() const {
    QVariantList result;
    for (const auto* definition : impl_->registry.cppContracts()) result.push_back(workbench::metadata(*definition));
    return result;
}
QVariantMap AppController::previewToolImpact(const QVariantMap& metadata) const {
    try {
        const auto proposed = candidate(metadata, impl_->registry, impl_->applicationDirectory);
        QVariantList nodes; std::size_t references = 0;
        for (const auto& flow : impl_->editor.project().flows) for (const auto& node : flow.nodes) if (node.toolId == proposed.definition.id) {
            nodes.push_back(QVariantMap{{"flow", workbench::q(flow.id)}, {"node", QString::number(node.id)}, {"title", workbench::q(node.title())}});
            for (const auto& [port, type] : node.signatures.outputs) references += impl_->editor.referenceCount(flow.id, {node.id, port});
        }
        return {{"valid", true}, {"affectedNodes", nodes.size()}, {"affectedBindings", static_cast<qulonglong>(references)}, {"nodes", nodes},
            {"message", QStringLiteral("影响 %1 个节点和 %2 个输出引用；不兼容的端口/绑定将清理。保存后重建运行时资源。")
                .arg(nodes.size()).arg(references)}};
    } catch (const std::exception& error) { return {{"valid", false}, {"affectedNodes", 0}, {"affectedBindings", 0}, {"message", workbench::q(error.what())}}; }
}
bool AppController::saveToolMetadata(const QVariantMap& metadata) {
    if (!impl_->idle()) return false;
    try {
        auto proposed = candidate(metadata, impl_->registry, impl_->applicationDirectory);
        const auto destination = workbench::pathText(proposed.definition.definitionPath);
        if (!QDir().mkpath(QFileInfo(destination).absolutePath())) throw ContractError("Cannot create custom tool directory; deploy in a writable portable directory");
        QSaveFile file(destination);
        if (!file.open(QIODevice::WriteOnly)) throw ContractError(file.errorString().toStdString());
        const auto encoded = YAML::Dump(proposed.yaml);
        if (file.write(encoded.data(), static_cast<qint64>(encoded.size())) != static_cast<qint64>(encoded.size()) || !file.commit())
            throw ContractError(file.errorString().toStdString());
        if (!proposed.previous.empty() && proposed.previous != proposed.definition.definitionPath && !QFile::remove(workbench::pathText(proposed.previous))) {
            QFile::remove(destination);
            throw ContractError("Cannot remove previous metadata location; original definition remains active");
        }
        const bool python = proposed.definition.kind == ToolKind::PythonFunction || proposed.definition.kind == ToolKind::PythonClass;
        impl_->registry.replaceDefinition(std::move(proposed.definition));
        if (python) { impl_->refresh(false); impl_->refreshReconcile = true; }
        else {
            auto reconciled = ProjectStorage(impl_->registry).reconcile(impl_->editor.project());
            impl_->storageDiagnostics(reconciled.diagnostics); impl_->editor.replaceProject(std::move(reconciled.project));
            impl_->tools.setRegistry(impl_->registry); impl_->changed(true); impl_->request(Impl::Pending::Reset);
        }
        return true;
    } catch (const std::exception& error) { impl_->report(workbench::q(error.what())); return false; }
}
}
