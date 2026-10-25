#include "ui/AppController_p.h"

#include "core/ToolValueValidation.h"

#include <algorithm>
#include <utility>

namespace visionrt {
namespace {
using Matrices = NestedVector<double, 3>;

const PortDefinition *firstRoiInput(const ToolDefinition &tool)
{
    for (const auto &port : tool.inputs)
        if (port.type.type == Type::ROI)
            return &port;
    return nullptr;
}

// Only the seed the user is pointing at may steer the candidate. Passing the whole group would let
// a distant ROI hijack a click on empty space, which is exactly what point-and-click must avoid.
bool nearCursor(const ROI::value_type::value_type &record, const QPointF &cursor, double margin)
{
    if (record.size() < 3)
        return false;
    double minX = record[1], maxX = record[1], minY = record[2], maxY = record[2];
    const auto accumulate = [&](double x, double y)
    {
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    };
    switch (roiKind(std::span<const double>(record)))
    {
    case RoiKind::Circle:
        accumulate(record[1] - record[3], record[2] - record[3]);
        accumulate(record[1] + record[3], record[2] + record[3]);
        break;
    case RoiKind::Rectangle:
        for (const auto &corner : rectangleCorners(record))
            accumulate(corner.x, corner.y);
        break;
    default:
        for (std::size_t index = 1; index + 1 < record.size(); index += 2)
            accumulate(record[index], record[index + 1]);
        break;
    }
    return cursor.x() >= minX - margin && cursor.x() <= maxX + margin && cursor.y() >= minY - margin
        && cursor.y() <= maxY + margin;
}

const PortDefinition *mappedTarget(const ToolDefinition &tool, const InteractionMapping &mapping,
                                   std::string_view helperOutput)
{
    const auto found = mapping.proposalMap.find(helperOutput);
    return found == mapping.proposalMap.end() ? nullptr : workbench::port(tool.inputs, found->second);
}

const std::string *stringLiteral(const NodeDefinition &node, std::string_view input)
{
    const auto found = node.inputs.find(input);
    if (found == node.inputs.end() || found->second.mode != BindingMode::Literal)
        return nullptr;
    return std::get_if<std::string>(&found->second.literal);
}

std::optional<double> numberLiteral(const NodeDefinition &node, std::string_view input)
{
    const auto found = node.inputs.find(input);
    if (found == node.inputs.end() || found->second.mode != BindingMode::Literal)
        return std::nullopt;
    if (const auto *value = std::get_if<double>(&found->second.literal))
        return *value;
    if (const auto *value = std::get_if<std::int64_t>(&found->second.literal))
        return static_cast<double>(*value);
    return std::nullopt;
}

const std::string *stringOutput(const Values &outputs, std::string_view port)
{
    const auto found = outputs.find(port);
    return found == outputs.end() ? nullptr : std::get_if<std::string>(&found->second);
}

std::optional<double> numberOutput(const Values &outputs, std::string_view port)
{
    const auto found = outputs.find(port);
    if (found == outputs.end())
        return std::nullopt;
    if (const auto *value = std::get_if<double>(&found->second))
        return *value;
    return std::nullopt;
}

bool allowedKind(const InteractionMapping &mapping, RoiKind kind)
{
    return std::find(mapping.roiKinds.begin(), mapping.roiKinds.end(), kind) != mapping.roiKinds.end();
}

// Ctrl+click keeps the current global parameters, so a candidate that contradicts them must be
// refused rather than silently rewriting the ROIs already placed.
void requireCompatible(const ToolDefinition &tool, const InteractionMapping &mapping, const NodeDefinition &node,
                       const Values &outputs)
{
    if (const auto *suggested = stringOutput(outputs, "suggested_polarity"))
        if (const auto *target = mappedTarget(tool, mapping, "suggested_polarity"))
            if (const auto *current = stringLiteral(node, target->id))
                if (*current != "any" && *suggested != "any" && *current != *suggested)
                    throw ContractError("候选极性与当前参数不兼容");
    const auto minPort = mappedTarget(tool, mapping, "suggested_width_min");
    const auto maxPort = mappedTarget(tool, mapping, "suggested_width_max");
    const auto low     = numberOutput(outputs, "suggested_width_min");
    const auto high    = numberOutput(outputs, "suggested_width_max");
    const auto currentMin = minPort ? numberLiteral(node, minPort->id) : std::nullopt;
    const auto currentMax = maxPort ? numberLiteral(node, maxPort->id) : std::nullopt;
    if (low && high && currentMin && currentMax && (*currentMin > *high || *currentMax < *low))
        throw ContractError("候选宽度范围与当前参数不兼容");
}
} // namespace

std::optional<AssistContext> AppController::Impl::assistContext(QString *reason) const
{
    const auto reject = [reason](const QString &text) -> std::optional<AssistContext>
    {
        if (reason)
            *reason = text;
        return {};
    };
    if (reason)
        reason->clear();
    const auto *node = currentNode();
    const auto *tool = currentTool();
    if (!node || !tool)
        return reject(QStringLiteral("请先选择工具节点"));
    if (!tool->interaction)
        return reject({});
    if (state != RuntimeState::Idle || !readsAllowed())
        return reject(QStringLiteral("运行或排空期间不可用智能点选"));
    if (roi)
        return reject(QStringLiteral("ROI 编辑期间不可用智能点选"));
    const auto *roiPort = firstRoiInput(*tool);
    if (!roiPort)
        return reject(QStringLiteral("该工具没有可点选的 ROI 输入"));
    if (roiPort->imageInput.empty())
        return reject(QStringLiteral("ROI 输入未声明关联图像：") + workbench::q(roiPort->id));
    const auto binding = node->inputs.find(roiPort->imageInput);
    if (binding == node->inputs.end() || binding->second.mode != BindingMode::Upstream || !binding->second.source)
        return reject(QStringLiteral("请先把图像输入连接到上游"));
    const auto found = snapshots.find(binding->second.source->node);
    if (found == snapshots.end() || !found->second)
        return reject(QStringLiteral("请先运行上游图像节点"));
    const auto value = found->second->outputs.find(binding->second.source->port);
    const auto *batch = value == found->second->outputs.end() ? nullptr : std::get_if<DataRef<Image>>(&value->second);
    if (!batch || batch->get().empty())
        return reject(QStringLiteral("当前图像尚无可点选缓存，请先运行"));
    if (index < 0 || static_cast<std::size_t>(index) >= batch->get().size())
        return reject(QStringLiteral("图像序号超出当前批次"));
    return AssistContext{projectEpoch, generation, flow.toStdString(), node->id, node->executionRevision, index,
                         found->second};
}

bool AppController::Impl::assistContextCurrent(const AssistContext &context) const
{
    QString    reason;
    const auto current = assistContext(&reason);
    return current && *current == context;
}

std::optional<AssistRequest> AppController::Impl::assistRequest(bool learn, bool append, const QPointF &cursor,
                                                               QString *reason) const
{
    const auto reject = [reason](const QString &text) -> std::optional<AssistRequest>
    {
        if (reason)
            *reason = text;
        return {};
    };
    const auto context = assistContext(reason);
    if (!context)
        return {};
    const auto *node    = currentNode();
    const auto *tool    = currentTool();
    const auto *roiPort = tool ? firstRoiInput(*tool) : nullptr;
    if (!node || !tool || !roiPort || !tool->interaction)
        return reject(QStringLiteral("当前节点不支持智能点选"));
    const auto &mapping = *tool->interaction;
    const auto *helper  = registry.find(mapping.helperToolId);
    if (!helper)
        return reject(QStringLiteral("辅助工具缺失：") + workbench::q(mapping.helperToolId));

    const auto binding = node->inputs.find(roiPort->imageInput);
    const auto source  = snapshots.find(binding->second.source->node);
    if (source == snapshots.end())
        return reject(QStringLiteral("上游结果已更新，请先运行"));
    const auto value = source->second->outputs.find(binding->second.source->port);
    const auto *batch = value == source->second->outputs.end() ? nullptr : std::get_if<DataRef<Image>>(&value->second);
    if (!batch || index < 0 || static_cast<std::size_t>(index) >= batch->get().size())
        return reject(QStringLiteral("上游结果已更新，请先运行"));

    if (learn)
    {
        const auto roiBinding = node->inputs.find(roiPort->id);
        if (roiBinding == node->inputs.end() || roiBinding->second.mode == BindingMode::Upstream
            || effectiveSource(roiBinding->second))
            return reject(QStringLiteral("该参数由上游提供"));
    }

    Values inputs;
    for (const auto &[helperPort, nodeInput] : mapping.inputMap)
    {
        if (helperPort == "images")
            inputs.emplace(helperPort, DataRef<Image>(Image{batch->get()[static_cast<std::size_t>(index)]},
                                                      {batch->owner()}));
        else if (helperPort == "seed_rois")
        {
            ROI::value_type seeds;
            const auto      seedValue = resolvedInput(*node, nodeInput);
            if (const auto *groups = std::get_if<DataRef<ROI>>(&seedValue))
                if (index < static_cast<int>(groups->get().size()))
                    for (const auto &record : groups->get()[static_cast<std::size_t>(index)])
                        if (nearCursor(record, cursor, 8.0))
                            seeds.push_back(record);
            inputs.emplace(helperPort, DataRef<ROI>(ROI{std::move(seeds)}));
        }
        else if (helperPort == "alignment")
        {
            const auto alignment = resolvedInput(*node, nodeInput);
            if (isMissing(alignment))
                inputs.emplace(helperPort, Array(DataRef<Matrices>(Matrices{})));
            else
            {
                const auto *array = std::get_if<Array>(&alignment);
                if (!array || array->type() != TypeSpec{Type::Array, DType::Float64, 3})
                    return reject(QStringLiteral("对齐输入必须是 Array(float64, rank=3)"));
                const auto &matrices = array->get<Matrices>();
                if (matrices.empty() || matrices.size() == 1)
                    inputs.emplace(helperPort, *array);
                else if (index < static_cast<int>(matrices.size()))
                    inputs.emplace(helperPort,
                                   Array(DataRef<Matrices>(Matrices{matrices[static_cast<std::size_t>(index)]})));
                else
                    return reject(QStringLiteral("对齐矩阵数量与图像数量不一致"));
            }
        }
        else
            inputs.emplace(helperPort, resolvedInput(*node, nodeInput));
    }

    inputs.emplace("cursor",
                   Array(DataRef<NestedVector<double, 1>>(NestedVector<double, 1>{cursor.x(), cursor.y()})));
    inputs.emplace("phase", std::string(learn ? "learn" : "hover"));
    std::int64_t extent = 256;
    if (const auto *port = workbench::port(helper->inputs, "search_extent"))
        if (const auto *declared = std::get_if<std::int64_t>(&port->defaultValue))
            extent = *declared;
    inputs.emplace("search_extent", extent);

    AssistRequest request;
    request.context = *context;
    request.inputs  = std::move(inputs);
    request.learn   = learn;
    request.append  = append;
    if (reason)
        reason->clear();
    return request;
}

void AppController::Impl::applyAssistOutcome(const AssistOutcome &outcome)
{
    if (!assistContextCurrent(outcome.context))
    {
        clearAssistPreview();
        setAssistHint(QStringLiteral("结果已更新，请重新选择"));
        return;
    }
    const auto *node    = currentNode();
    const auto *tool    = currentTool();
    const auto *roiPort = tool ? firstRoiInput(*tool) : nullptr;
    if (!node || !tool || !roiPort || !tool->interaction)
        return;
    if (!outcome.valid || outcome.proposal.empty() || outcome.proposal.front().empty())
    {
        clearAssistPreview();
        setAssistHint(outcome.quality.isEmpty() ? QStringLiteral("未找到有效目标")
                                                : QStringLiteral("未找到有效目标：") + outcome.quality);
        return;
    }
    const auto binding = node->inputs.find(roiPort->id);
    if (binding == node->inputs.end())
        return;
    if (binding->second.mode == BindingMode::Upstream || effectiveSource(binding->second))
    {
        clearAssistPreview();
        setAssistHint(QStringLiteral("该参数由上游提供"));
        return;
    }
    const auto &mapping = *tool->interaction;
    for (const auto &record : outcome.proposal.front())
        if (!allowedKind(mapping, roiKind(std::span<const double>(record))))
        {
            clearAssistPreview();
            setAssistHint(QStringLiteral("候选类型与工具声明不一致"));
            return;
        }
    try
    {
        ROI groups;
        if (const auto *current = std::get_if<DataRef<ROI>>(&binding->second.literal))
            groups = current->get();
        groups.resize(std::max(groups.size(), static_cast<std::size_t>(index) + 1));
        auto &group = groups[static_cast<std::size_t>(index)];

        std::vector<std::pair<std::string, PortValue>> edits;
        if (outcome.append)
        {
            requireCompatible(*tool, mapping, *node, outcome.outputs);
            group.insert(group.end(), outcome.proposal.front().begin(), outcome.proposal.front().end());
        }
        else
        {
            group = outcome.proposal.front();
            for (const auto &[helperOutput, nodeInput] : mapping.proposalMap)
            {
                if (nodeInput == roiPort->id)
                    continue;
                const auto *target     = workbench::port(tool->inputs, nodeInput);
                const auto  suggestion = outcome.outputs.find(helperOutput);
                if (!target || suggestion == outcome.outputs.end() || isMissing(suggestion->second))
                    continue;
                const auto current = node->inputs.find(nodeInput);
                if (current == node->inputs.end() || current->second.mode == BindingMode::Upstream
                    || effectiveSource(current->second))
                    continue;
                validateValue(target->type, suggestion->second);
                if (equalValues(current->second.literal, suggestion->second))
                    continue;
                edits.emplace_back(nodeInput, suggestion->second);
            }
        }
        edits.insert(edits.begin(), {roiPort->id, DataRef<ROI>(std::move(groups))});
        clearAssistPreview();
        if (!editor.setLiteralBindings(flow.toStdString(), node->id, edits))
            return;
        changed();
        setAssistHint(QStringLiteral("已应用智能点选结果，运行后确认"));
    }
    catch (const std::exception &error)
    {
        clearAssistPreview();
        report(error, QStringLiteral("智能点选提交失败"));
        setAssistHint(QStringLiteral("智能点选结果未应用：") + QString::fromUtf8(error.what()));
    }
}

void AppController::Impl::setAssistHint(const QString &text)
{
    if (assistHint == text)
        return;
    assistHint = text;
    emit q->assistChanged();
}

void AppController::Impl::clearAssistPreview()
{
    assistPreviewContext.reset();
    if (assistPreviewRois.empty())
        return;
    assistPreviewRois.clear();
    if (viewport)
        viewport->setAssistPreview({});
    emit q->assistChanged();
}

void AppController::Impl::refreshAssist()
{
    QString    hint;
    bool       enabled  = false;
    const auto *node    = currentNode();
    const auto *tool    = currentTool();
    const auto *roiPort = tool ? firstRoiInput(*tool) : nullptr;
    const bool  candidate = node && tool && tool->interaction && roiPort && state == RuntimeState::Idle
                         && !isClosing() && !roi && readsAllowed();
    if (candidate)
    {
        const auto &mapping = *tool->interaction;
        const auto *helper  = registry.find(mapping.helperToolId);
        if (!helper)
            hint = QStringLiteral("辅助工具缺失：") + workbench::q(mapping.helperToolId);
        else
        {
            QString missing;
            if (mapping.inputMap.contains("cursor") || mapping.inputMap.contains("phase")
                || mapping.inputMap.contains("search_extent"))
                missing = QStringLiteral("协议端口 cursor/phase/search_extent");
            for (const auto &port : helper->inputs)
                if (port.id != "cursor" && port.id != "phase" && port.id != "search_extent"
                    && !mapping.inputMap.contains(port.id))
                    missing = workbench::q(port.id);
            for (const auto &[helperOutput, target] : mapping.proposalMap)
                if (!workbench::port(helper->outputs, helperOutput))
                    missing = workbench::q(helperOutput);
            if (!missing.isEmpty())
                hint = QStringLiteral("辅助工具映射缺少端口：") + missing;
            else
            {
                if (assistToolId != tool->id || assist->helperId() != helper->id)
                {
                    assist->configure(std::make_shared<const ToolRegistry>(registry), *helper,
                                      editor.project().path.parent_path());
                    assistToolId = tool->id;
                }
                enabled = true;
                hint    = QStringLiteral("智能点选：移动鼠标预选，单击替换，Ctrl+单击追加，Esc 取消");
            }
        }
    }
    else if (node && tool && tool->interaction && roiPort && state == RuntimeState::Idle && !isClosing())
        hint = QStringLiteral("智能点选需要已运行的图像输入");
    if (!enabled && assist)
        assist->invalidate();
    if (assistPreviewContext && !assistContextCurrent(*assistPreviewContext))
        clearAssistPreview();
    assistEnabled = enabled;
    if (viewport)
        viewport->setAssistActive(enabled);
    setAssistHint(hint);
}

bool AppController::assistActive() const
{
    return impl_->assistEnabled;
}

QString AppController::assistHint() const
{
    return impl_->assistHint;
}

QVariantList AppController::assistPreview() const
{
    QVariantList rows;
    for (const auto &group : impl_->assistPreviewRois)
        for (const auto &record : group)
        {
            QVariantList values;
            for (const auto value : record)
                values.push_back(value);
            rows.push_back(values);
        }
    return rows;
}

bool AppController::assistHover(double x, double y)
{
    if (!impl_->assistEnabled || impl_->isClosing())
        return false;
    QString reason;
    auto    request = impl_->assistRequest(false, false, QPointF(x, y), &reason);
    if (!request)
    {
        if (!reason.isEmpty())
            impl_->setAssistHint(reason);
        return false;
    }
    impl_->assist->request(std::move(*request));
    return true;
}

bool AppController::assistClick(double x, double y, bool append)
{
    if (!impl_->assistEnabled || impl_->isClosing())
    {
        impl_->setAssistHint(QStringLiteral("当前状态不可用智能点选"));
        return false;
    }
    QString reason;
    auto    request = impl_->assistRequest(true, append, QPointF(x, y), &reason);
    if (!request)
    {
        if (!reason.isEmpty())
            impl_->setAssistHint(reason);
        return false;
    }
    impl_->clearAssistPreview();
    if (!impl_->assist->request(std::move(*request)))
    {
        impl_->setAssistHint(QStringLiteral("正在识别上一个候选，请稍候"));
        return false;
    }
    return true;
}

void AppController::assistCancel()
{
    if (impl_->assist)
        impl_->assist->cancel();
    impl_->clearAssistPreview();
}
} // namespace visionrt
