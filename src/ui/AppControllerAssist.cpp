#include "ui/AppController_p.h"
#include "ui/UiPreferences.h"

#include "core/InputNormalizer.h"
#include "core/ToolValueValidation.h"
#include "log/LogService.h"

#include <algorithm>
#include <atomic>
#include <utility>

namespace visionrt {
namespace {

// Ports of the bundled point-and-click assistants. The interaction mapping already names them by
// number; these constants only let the host recognise the three inputs and three suggestions it has
// dedicated presentation for.
namespace assist_ports {
inline constexpr PortId Images            = 1;
inline constexpr PortId Cursor            = 2;
inline constexpr PortId Phase             = 3;
inline constexpr PortId SeedRois          = 4;
inline constexpr PortId Alignment         = 5;
inline constexpr PortId SearchExtent      = 6;
inline constexpr PortId SuggestedPolarity = 6;
inline constexpr PortId SuggestedWidthMin = 7;
inline constexpr PortId SuggestedWidthMax = 8;
} // namespace assist_ports

const PortDefinition *firstRoiInput(const ToolDefinition &tool)
{
    for (const auto &port : tool.inputs)
        if (port.type.type == Type::ROI)
            return &port;
    return nullptr;
}

// Only the seed the user is pointing at may steer the candidate. Passing the whole group would let
// a distant ROI hijack a click on empty space, which is exactly what point-and-click must avoid.
// Seeds are stored in reference coordinates while the cursor lives in the current image, so the
// comparison happens in one frame: the record is mapped through the per-image alignment first.
bool nearCursor(std::span<const double> record, const std::array<double, 9> &referenceToImage,
                const QPointF &cursor, double margin)
{
    if (record.size() < 3)
        return false;
    std::array<cv::Point2d, 2> bounds{};
    try
    {
        bounds = transformedRoiBounds(record, referenceToImage);
    }
    catch (const ContractError &error)
    {
        // A singular or non-finite alignment has no image for the seed, so "not under the cursor"
        // is the only honest answer. Report it once: a project whose alignment collapses would
        // otherwise look like point-and-click silently ignoring the click.
        static std::atomic_bool reported{false};
        if (!reported.exchange(true))
            LogService::write(LogLevel::Warn, "智能点选", std::string("对齐矩阵不可用：") + error.what());
        return false;
    }
    return cursor.x() >= bounds[0].x - margin && cursor.x() <= bounds[1].x + margin
        && cursor.y() >= bounds[0].y - margin && cursor.y() <= bounds[1].y + margin;
}

// Identity in the current frame: a reference ROI already lives in the image the user sees.
constexpr std::array<double, 9> kNoAlignment{1, 0, 0, 0, 1, 0, 0, 0, 1};

std::array<double, 9> alignmentFor(std::span<const double> matrices, int index)
{
    if (matrices.empty() || matrices.size() % 9 != 0)
        return kNoAlignment;
    const auto count  = matrices.size() / 9;
    const auto offset = static_cast<std::size_t>(index >= 0 && static_cast<std::size_t>(index) < count ? index : 0) * 9;
    return {matrices[offset],     matrices[offset + 1], matrices[offset + 2],
            matrices[offset + 3], matrices[offset + 4], matrices[offset + 5],
            matrices[offset + 6], matrices[offset + 7], matrices[offset + 8]};
}

const PortDefinition *mappedTarget(const ToolDefinition &tool, const InteractionMapping &mapping, PortId helperOutput)
{
    const auto found = mapping.proposals.find(helperOutput);
    return found == mapping.proposals.end() ? nullptr : workbench::port(tool.inputs, found->second.id);
}

const std::string *stringLiteral(const NodeDefinition &node, PortId input)
{
    const auto found = node.inputs.find(input);
    if (found == node.inputs.end() || found->second.mode != BindingMode::Literal)
        return nullptr;
    return std::get_if<std::string>(&found->second.literal);
}

std::optional<double> numberLiteral(const NodeDefinition &node, PortId input)
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

const std::string *stringOutput(const Values &outputs, PortId port)
{
    const auto found = outputs.find(port);
    return found == outputs.end() ? nullptr : std::get_if<std::string>(&found->second);
}

std::optional<double> numberOutput(const Values &outputs, PortId port)
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
    if (const auto *suggested = stringOutput(outputs, assist_ports::SuggestedPolarity))
        if (const auto *target = mappedTarget(tool, mapping, assist_ports::SuggestedPolarity))
            if (const auto *current = stringLiteral(node, target->id))
                if (*current != "any" && *suggested != "any" && *current != *suggested)
                    throw ContractError("候选极性与当前参数不兼容");
    const auto minPort = mappedTarget(tool, mapping, assist_ports::SuggestedWidthMin);
    const auto maxPort = mappedTarget(tool, mapping, assist_ports::SuggestedWidthMax);
    const auto low     = numberOutput(outputs, assist_ports::SuggestedWidthMin);
    const auto high    = numberOutput(outputs, assist_ports::SuggestedWidthMax);
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
        return reject(QStringLiteral("该工具不支持智能点选"));
    if (!easyTouchEnabled)
        return reject(QStringLiteral("智能点选已关闭"));
    if (!assistRequestsAllowed())
        return reject(state != RuntimeState::Idle || presentation.draining() ? QStringLiteral("运行或排空期间不可用智能点选")
                                                              : QStringLiteral("当前管理操作尚未完成，暂不可用智能点选"));
    const auto *roiPort = firstRoiInput(*tool);
    if (!roiPort)
        return reject(QStringLiteral("该工具没有可点选的 ROI 输入"));
    if (!roiPort->image)
        return reject(QStringLiteral("ROI 输入未声明关联图像：") + workbench::q(portLabel(roiPort->id)));
    const auto binding = node->inputs.find(roiPort->image->id);
    if (binding == node->inputs.end() || binding->second.mode != BindingMode::Upstream || !binding->second.source)
        return reject(QStringLiteral("请先把图像输入连接到上游"));
    const auto snapshot = presentation.sharedSnapshot(binding->second.source->node);
    if (!snapshot)
        return reject(QStringLiteral("请先运行上游图像节点"));
    const auto value = snapshot->outputs.find(binding->second.source->port);
    const auto *batch = value == snapshot->outputs.end() ? nullptr : std::get_if<DataRef<Image>>(&value->second);
    if (!batch || batch->get().empty())
        return reject(QStringLiteral("当前图像尚无可点选缓存，请先运行"));
    const int index = presentation.imageIndex();
    if (index < 0 || static_cast<std::size_t>(index) >= batch->get().size())
        return reject(QStringLiteral("图像序号超出当前批次"));
    return AssistContext{projectEpoch, generation, flow.toStdString(), node->id, node->executionRevision, index,
                         snapshot};
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
    const auto *helper  = registry.find(mapping.helperNameEn);
    if (!helper)
        return reject(QStringLiteral("辅助工具缺失：") + workbench::q(mapping.helperNameEn));

    if (!roiPort->image)
        return reject(QStringLiteral("ROI 输入未声明关联图像：") + workbench::q(portLabel(roiPort->id)));
    // The context already resolved the ROI's declared image and checked that it came from an
    // upstream result; reading it again could only disagree with what the proposal is stamped with.
    const auto binding = node->inputs.find(roiPort->image->id);
    if (binding == node->inputs.end() || binding->second.mode != BindingMode::Upstream || !binding->second.source)
        return reject(QStringLiteral("请先把图像输入连接到上游"));
    const auto value = context->source->outputs.find(binding->second.source->port);
    const auto *batch = value == context->source->outputs.end() ? nullptr : std::get_if<DataRef<Image>>(&value->second);
    const int   index = context->imageIndex;
    if (!batch || index < 0 || static_cast<std::size_t>(index) >= batch->get().size())
        return reject(QStringLiteral("上游结果已更新，请先运行"));

    if (learn)
    {
        const auto roiBinding = node->inputs.find(roiPort->id);
        if (roiBinding == node->inputs.end() || roiBinding->second.mode == BindingMode::Upstream
            || effectiveSource(roiBinding->second))
            return reject(QStringLiteral("该参数由上游提供"));
    }

    // The seed the user points at is stored in reference coordinates while the cursor is measured in
    // the current image, so the frame mapping is resolved once here and the hit test uses it.
    const auto seedAlignment = [&]
    {
        if (!roiPort->alignment)
            return kNoAlignment;
        const auto alignment = resolvedInput(*node, roiPort->alignment->id);
        const auto *array    = std::get_if<Array>(&alignment);
        if (!array || array->type() != TypeSpec{Type::Array, DType::Float64, 3})
            return kNoAlignment;
        if (array->elementCount() == 0)
            return kNoAlignment;
        if (array->shape().size() != 3 || array->shape()[1] != 3 || array->shape()[2] != 3)
            return kNoAlignment;
        return alignmentFor(array->elements<double>(), index);
    }();

    Values inputs;
    for (const auto &[helperPort, nodeInput] : mapping.inputs)
    {
        if (helperPort == assist_ports::Images)
            inputs.emplace(helperPort, DataRef<Image>(Image{batch->get()[static_cast<std::size_t>(index)]},
                                                      {batch->owner()}));
        else if (helperPort == assist_ports::SeedRois)
        {
            RegionGroup seeds;
            const auto      seedValue = resolvedInput(*node, nodeInput.id);
            if (const auto *groups = std::get_if<DataRef<ROI>>(&seedValue))
                if (index < static_cast<int>(groups->get().size()))
                    for (const auto &record : groups->get()[static_cast<std::size_t>(index)])
                        if (nearCursor(record, seedAlignment, cursor, 8.0))
                            seeds.push_back(record);
            inputs.emplace(helperPort, DataRef<ROI>(ROI{std::move(seeds)}));
        }
        else if (helperPort == assist_ports::Alignment)
        {
            const auto alignment = resolvedInput(*node, nodeInput.id);
            if (isMissing(alignment))
                inputs.emplace(helperPort, Array::empty(DType::Float64, 3));
            else
            {
                const auto *array = std::get_if<Array>(&alignment);
                if (!array || array->type() != TypeSpec{Type::Array, DType::Float64, 3})
                    return reject(QStringLiteral("对齐输入必须是 Array(float64, rank=3)"));
                const auto matrices = array->elementCount();
                if (matrices == 0 || matrices == 9)
                    inputs.emplace(helperPort, *array);
                else if (index >= 0 && static_cast<std::size_t>(index) * 9 + 9 <= matrices)
                {
                    const auto values = array->elements<double>();
                    std::vector<double> slice(values.begin() + static_cast<std::ptrdiff_t>(index) * 9,
                                              values.begin() + static_cast<std::ptrdiff_t>(index) * 9 + 9);
                    inputs.emplace(helperPort,
                                   Array::dense(DType::Float64, {1, 3, 3}, std::move(slice)));
                }
                else
                    return reject(QStringLiteral("对齐矩阵数量与图像数量不一致"));
            }
        }
        else
            inputs.emplace(helperPort, resolvedInput(*node, nodeInput.id));
    }

    inputs.emplace(assist_ports::Cursor,
                   Array::dense(DType::Float64, {2}, std::vector<double>{cursor.x(), cursor.y()}));
    inputs.emplace(assist_ports::Phase, std::string(learn ? "learn" : "hover"));
    std::int64_t extent = 256;
    if (const auto *port = workbench::port(helper->inputs, assist_ports::SearchExtent))
        if (const auto *declared = std::get_if<std::int64_t>(&port->defaultValue))
            extent = *declared;
    inputs.emplace(assist_ports::SearchExtent, extent);
    // System inputs with a legal default (currently upstream_status=0) are never required in
    // each tool's interaction map, but the helper must still receive a legal value.
    inputs = normalizeInputs(helper->inputs, std::move(inputs));

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
        const int index = presentation.imageIndex();
        groups.resize(std::max(groups.size(), static_cast<std::size_t>(index) + 1));
        auto &group = groups[static_cast<std::size_t>(index)];

        std::vector<std::pair<PortId, PortValue>> edits;
        if (outcome.append)
        {
            requireCompatible(*tool, mapping, *node, outcome.outputs);
            group.insert(group.end(), outcome.proposal.front().begin(), outcome.proposal.front().end());
        }
        else
        {
            group = outcome.proposal.front();
            for (const auto &[helperOutput, nodeInput] : mapping.proposals)
            {
                if (nodeInput.id == roiPort->id)
                    continue;
                const auto *target     = workbench::port(tool->inputs, nodeInput.id);
                const auto  suggestion = outcome.outputs.find(helperOutput);
                if (!target || suggestion == outcome.outputs.end() || isMissing(suggestion->second))
                    continue;
                const auto current = node->inputs.find(nodeInput.id);
                if (current == node->inputs.end() || current->second.mode == BindingMode::Upstream
                    || effectiveSource(current->second))
                    continue;
                // A recommendation is a manual commit like any other: declared ranges clamp it and
                // a value outside the declared options is reported, never replaced by the default.
                const auto normalized = normalizeInput(*target, suggestion->second);
                if (equalValues(current->second.literal, normalized.value))
                    continue;
                edits.emplace_back(nodeInput.id, normalized.value);
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
    if (auto *viewport = presentation.viewport())
        viewport->setAssistPreview({});
    emit q->assistChanged();
}

bool AppController::Impl::assistRequestsAllowed() const
{
    return easyTouchEnabled && !operations.isClosing() && state == RuntimeState::Idle && !presentation.draining()
        && !operations.active() && !roiSession.active() && readsAllowed();
}

void AppController::Impl::refreshAssist()
{
    QString     hint;
    bool        enabled  = false;
    const auto *node    = currentNode();
    const auto *tool    = currentTool();
    const auto *roiPort = tool ? firstRoiInput(*tool) : nullptr;
    const bool  candidate = node && tool && tool->interaction && roiPort && state == RuntimeState::Idle
                         && !operations.isClosing() && !roiSession.active() && readsAllowed();
    if (!easyTouchEnabled)
        hint = QStringLiteral("智能点选已关闭");
    else if (candidate)
    {
        const auto &mapping = *tool->interaction;
        const auto *helper  = registry.find(mapping.helperNameEn);
        if (!helper)
            hint = QStringLiteral("辅助工具缺失：") + workbench::q(mapping.helperNameEn);
        else
        {
            QString missing;
            if (mapping.inputs.contains(assist_ports::Cursor) || mapping.inputs.contains(assist_ports::Phase)
                || mapping.inputs.contains(assist_ports::SearchExtent))
                missing = QStringLiteral("协议端口 cursor/phase/search_extent");
            for (const auto &port : helper->inputs)
                if (port.id != assist_ports::Cursor && port.id != assist_ports::Phase
                    && port.id != assist_ports::SearchExtent && !mapping.inputs.contains(port.id)
                    && isMissing(port.defaultValue))
                    missing = workbench::q(portLabel(port.id));
            for (const auto &[helperOutput, target] : mapping.proposals)
                if (!workbench::port(helper->outputs, helperOutput))
                    missing = workbench::q(portLabel(helperOutput));
            if (!missing.isEmpty())
                hint = QStringLiteral("辅助工具映射缺少端口：") + missing;
            else
            {
                if (assistToolId != tool->nameEn || assist->helperId() != helper->nameEn)
                {
                    assist->configure(std::make_shared<const ToolRegistry>(registry), *helper,
                                      editor.project().path.parent_path());
                    assistToolId = tool->nameEn;
                }
                enabled = true;
                hint    = QStringLiteral("智能点选：移动鼠标预选，单击替换，Ctrl+单击追加，Esc 取消");
            }
        }
    }
    else if (node && tool && tool->interaction && roiPort && state == RuntimeState::Idle && !operations.isClosing())
        hint = QStringLiteral("智能点选需要已运行的图像输入");
    else if (node && tool && tool->interaction && (presentation.draining() || operations.active() || operations.isClosing()))
        hint = QStringLiteral("运行或排空期间不可用智能点选");
    if (!enabled && assist)
        assist->invalidate();
    assistDisabledReason = hint;
    if (assistPreviewContext && !assistContextCurrent(*assistPreviewContext))
        clearAssistPreview();
    assistEnabled = enabled;
    if (auto *viewport = presentation.viewport())
        viewport->setAssistActive(enabled);
    setAssistHint(hint);
}

bool AppController::assistActive() const
{
    return impl_->assistEnabled && impl_->assistRequestsAllowed();
}

bool AppController::easyTouchEnabled() const
{
    return impl_->easyTouchEnabled;
}

void AppController::setEasyTouchEnabled(bool enabled)
{
    if (impl_->easyTouchEnabled == enabled || impl_->operations.isClosing())
        return;
    impl_->easyTouchEnabled = enabled;
    if (!UiPreferences::setEasyTouchEnabled(enabled))
        impl_->report(QStringLiteral("无法写入界面设置，智能点选开关只在本次运行生效：")
                      + UiPreferences::path());
    if (!enabled)
    {
        // Drop the preview and every queued/in-flight request, then release the persistent
        // helper off the GUI thread. Committed ROI and recommended parameters stay untouched.
        impl_->clearAssistPreview();
        impl_->setAssistHint(QStringLiteral("智能点选已关闭"));
        if (impl_->assist)
        {
            impl_->assist->cancel();
            impl_->assist->releaseSession();
        }
    }
    impl_->refreshAssist();
    emit assistChanged();
}

bool AppController::assistAvailable() const
{
    return impl_->assistEnabled && impl_->easyTouchEnabled;
}

QString AppController::assistDisabledReason() const
{
    return impl_->assistDisabledReason;
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
    if (!impl_->assistRequestsAllowed() || !impl_->assistEnabled)
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
    if (!impl_->assistRequestsAllowed() || !impl_->assistEnabled)
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
