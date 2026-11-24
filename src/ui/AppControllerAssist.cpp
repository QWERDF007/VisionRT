#include "AppController_p.h"
#include "core/InputNormalizer.h"
#include "core/ToolValueValidation.h"
#include "core/ValueJson.h"
#include "log/LogService.h"
#include "ui/UiPreferences.h"

#include <QImage>
#include <algorithm>
#include <atomic>
#include <utility>

namespace visionrt {
namespace {

// Only the seed the user is pointing at may steer the candidate. Passing the whole group would let
// a distant ROI hijack a click on empty space, which is exactly what point-and-click must avoid.
// Seeds are stored in reference coordinates while the cursor lives in the current image, so the
// comparison happens in one frame: the record is mapped through the per-image alignment first.
bool nearCursor(std::span<const double> record, const std::array<double, 9> &referenceToImage, const QPointF &cursor,
                double margin)
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
            LogService::write(LogLevel::Warn, LogSource::Ui, std::string("智能点选：对齐矩阵不可用：") + error.what());
        return false;
    }
    if (RegionView(record).kind() == RoiKind::Circle)
    {
        const auto        circle      = RegionView(record).circle();
        const auto       &m           = referenceToImage;
        const double      determinant = m[0] * m[4] - m[1] * m[3];
        const double      dx = cursor.x() - m[2], dy = cursor.y() - m[5];
        const cv::Point2d reference{(m[4] * dx - m[1] * dy) / determinant, (-m[3] * dx + m[0] * dy) / determinant};
        return cv::norm(reference - circle.center()) <= circle.radius() + margin / std::sqrt(std::abs(determinant));
    }
    return cursor.x() >= bounds[0].x - margin && cursor.x() <= bounds[1].x + margin
        && cursor.y() >= bounds[0].y - margin && cursor.y() <= bounds[1].y + margin;
}

// Identity in the current frame: a reference ROI already lives in the image the user sees.
constexpr std::array<double, 9> kNoAlignment{1, 0, 0, 0, 1, 0, 0, 0, 1};

std::array<double, 9> alignmentFor(std::span<const double> matrix)
{
    if (matrix.empty())
        return kNoAlignment;
    std::array<double, 9> result;
    std::copy_n(matrix.begin(), result.size(), result.begin());
    return result;
}

// An empty alignment is the declared identity; nonempty batches are one broadcast matrix or one
// matrix per source image. Missing and malformed bindings are rejected before a locator is called.
std::optional<PortValue> alignmentSlice(const PortValue &value, int index, size_t imageCount)
{
    const auto *array = std::get_if<Array>(&value);
    if (!array || array->type() != TypeSpec{Type::Array, DType::Float64, 3})
        return {};
    if (array->elementCount() == 0)
        return value;
    const auto shape = array->shape();
    if (shape[1] != 3 || shape[2] != 3 || (shape[0] != 1 && shape[0] != imageCount))
        return {};
    const auto          selected = shape[0] == 1 ? 0 : index;
    const auto          values   = array->elements<double>();
    std::vector<double> slice(values.begin() + static_cast<std::ptrdiff_t>(selected) * 9,
                              values.begin() + static_cast<std::ptrdiff_t>(selected) * 9 + 9);
    return Array::dense(DType::Float64, {1, 3, 3}, std::move(slice));
}

} // namespace

const ParameterLocator *AppController::Impl::parameterLocator() const
{
    const auto *tool = currentTool();
    if (!tool)
        return nullptr;
    const auto &registration = trainingMode ? tool->integrations.training : tool->integrations.processing;
    return registration ? &*registration : nullptr;
}

std::optional<AssistContext> AppController::Impl::assistContext(QString *reason) const
{
    const auto reject = [reason](QString text) -> std::optional<AssistContext>
    {
        if (reason)
            *reason = std::move(text);
        return {};
    };
    const auto *node    = currentNode();
    const auto *tool    = currentTool();
    const auto *locator = parameterLocator();
    if (!node || !tool || !locator)
        return reject(QStringLiteral("当前参数未配置智能点选"));
    if (!easyTouchEnabled)
        return reject(QStringLiteral("智能点选已关闭"));
    if (!assistRequestsAllowed())
        return reject(QStringLiteral("运行或管理操作期间暂不可用智能点选"));
    const auto *algorithm = registry.find(locator->algorithm);
    if (!algorithm)
        return reject(QStringLiteral("定位算法缺失：") + workbench::q(locator->algorithm));
    const auto verify = [&](const std::vector<PortDefinition> &ports, PortId id, Type type)
    {
        const auto *p = workbench::port(ports, id);
        return p && p->type.type == type;
    };
    for (auto [id, type] : {
             std::pair{ locator->frameInput, Type::Image},
             {locator->searchInput,   Type::ROI}
    })
        if (!verify(algorithm->inputs, id, type))
            return reject(
                QStringLiteral("算法 %1 inputs.%2 缺失或类型不匹配").arg(workbench::q(locator->algorithm)).arg(id));
    for (auto [id, type] : {
             std::pair{  locator->previewOutput,  Type::ROI},
             {locator->candidateOutput,  Type::ROI},
             {    locator->validOutput, Type::Bool}
    })
        if (!verify(algorithm->outputs, id, type))
            return reject(
                QStringLiteral("算法 %1 outputs.%2 缺失或类型不匹配").arg(workbench::q(locator->algorithm)).arg(id));
    if (locator->seedInput && !verify(algorithm->inputs, *locator->seedInput, Type::ROI))
        return reject(QStringLiteral("算法 %1 inputs.%2 缺失或类型不匹配")
                          .arg(workbench::q(locator->algorithm))
                          .arg(*locator->seedInput));
    if (locator->alignmentInput)
    {
        const auto *port = workbench::port(algorithm->inputs, *locator->alignmentInput);
        if (!port || port->type != TypeSpec{Type::Array, DType::Float64, 3})
            return reject(QStringLiteral("算法 %1 inputs.%2 对齐类型不匹配")
                              .arg(workbench::q(locator->algorithm))
                              .arg(*locator->alignmentInput));
    }
    if (locator->messageOutput && !verify(algorithm->outputs, *locator->messageOutput, Type::String))
        return reject(QStringLiteral("算法 %1 outputs.%2 诊断类型不匹配")
                          .arg(workbench::q(locator->algorithm))
                          .arg(*locator->messageOutput));
    if (!verify(tool->inputs, locator->imageSource, Type::Image))
        return reject(QStringLiteral("图像来源 inputs.%1 无效").arg(locator->imageSource));
    for (const auto &write : locator->parameters)
    {
        const auto *source = workbench::port(algorithm->outputs, write.sourcePort);
        if (!source)
            return reject(QStringLiteral("算法 %1 outputs.%2 不存在")
                              .arg(workbench::q(locator->algorithm))
                              .arg(write.sourcePort));
        if (write.limit != SuggestedLimit::Exact && source->type.type != Type::Int && source->type.type != Type::Float)
            return reject(QStringLiteral("算法 %1 outputs.%2 的建议约束只适用于数值参数")
                              .arg(workbench::q(locator->algorithm))
                              .arg(write.sourcePort));
        const auto *target
            = workbench::port(write.target.stage == PortStage::Train ? tool->trainParams : write.target.stage == PortStage::Init ? tool->initParams : tool->inputs, write.target.id);
        if (!target || target->type != source->type)
            return reject(QStringLiteral("参数 %1 缺失或类型不匹配").arg(write.target.id));
    }
    AssistContext context{projectEpoch, generation, flow.toStdString(), node->id, node->executionRevision};
    if (trainingMode)
    {
        const auto *spec = trainingSpec();
        if (!spec || (training ? training->roiParameter() : 0) != locator->regionTarget)
            return reject(QStringLiteral("训练画布字段不存在"));
        if (!training || !training->active() || training->selected() < 0 || trainingAssistImage.empty())
            return reject(QStringLiteral("请先加入训练参考图"));
        context.trainingSerial = trainingSerial;
        context.trainingRecord = training->selected();
    }
    else
    {
        if (!verify(tool->inputs, locator->regionTarget, Type::ROI))
            return reject(QStringLiteral("ROI 目标 inputs.%1 无效").arg(locator->regionTarget));
        const auto binding = node->inputs.find(locator->imageSource);
        if (binding == node->inputs.end() || !effectiveSource(binding->second))
            return reject(QStringLiteral("请先连接上游图像"));
        context.source     = presentation.sharedSnapshot(binding->second.source->node);
        context.imageIndex = presentation.imageIndex();
        if (!context.source)
            return reject(QStringLiteral("请先运行上游图像节点"));
        const auto  it    = context.source->outputs.find(binding->second.source->port);
        const auto *batch = it == context.source->outputs.end() ? nullptr : std::get_if<DataRef<Image>>(&it->second);
        if (!batch || context.imageIndex < 0 || static_cast<size_t>(context.imageIndex) >= batch->get().size())
            return reject(QStringLiteral("当前图像尚未运行"));
    }
    return context;
}

bool AppController::Impl::assistContextCurrent(const AssistContext &context) const
{
    const auto current = assistContext(nullptr);
    return current && *current == context;
}

std::optional<AssistRequest> AppController::Impl::assistRequest(bool learn, bool append, const QPointF &point,
                                                                QString *reason) const
{
    const auto context = assistContext(reason);
    if (!context)
        return {};
    const auto   &locator = *parameterLocator();
    const auto   &node    = *currentNode();
    AssistRequest request;
    request.context = *context;
    request.learn   = learn;
    request.append  = append;
    request.inputs.emplace(locator.searchInput,
                           DataRef<ROI>(ROI{{makeCircle({point.x(), point.y()}, locator.searchRadius)}}));
    PortValue   alignment = Array::empty(DType::Float64, 3);
    RegionGroup seeds;
    if (trainingMode)
    {
        request.inputs.emplace(locator.frameInput, DataRef<Image>(Image{trainingAssistImage}));
        if (append)
            seeds = training->regions(training->selected());
    }
    else
    {
        const auto &input = node.inputs.at(locator.imageSource);
        const auto &batch = std::get<DataRef<Image>>(context->source->outputs.at(input.source->port));
        request.inputs.emplace(locator.frameInput,
                               DataRef<Image>(Image{batch.get().at(context->imageIndex)}, {batch.owner()}));
        const auto *field = workbench::port(currentTool()->inputs, locator.regionTarget);
        if (field->alignment)
        {
            const auto raw   = resolvedInput(node, field->alignment->id);
            const auto slice = alignmentSlice(raw, context->imageIndex, batch.get().size());
            if (!slice)
            {
                if (reason)
                    *reason = QStringLiteral("对齐参数缺失或矩阵批次无效");
                return {};
            }
            alignment = *slice;
        }
        const auto writable = [&](PortId id)
        {
            const auto it = node.inputs.find(id);
            return it != node.inputs.end() && it->second.mode != BindingMode::Upstream;
        };
        if (learn
            && (!writable(locator.regionTarget)
                || std::ranges::any_of(locator.parameters, [&](const auto &w)
                                       { return w.target.stage == PortStage::Inputs && !writable(w.target.id); })))
        {
            if (reason)
                *reason = QStringLiteral("点选目标由上游提供");
            return {};
        }
        if (!append && locator.seedInput)
        {
            const auto  value = resolvedInput(node, locator.regionTarget);
            const auto *all   = std::get_if<DataRef<ROI>>(&value);
            if (all && !all->get().empty()
                && (all->get().size() == 1 || static_cast<size_t>(context->imageIndex) < all->get().size()))
            {
                const auto &matrix    = std::get<Array>(alignment);
                const auto  transform = alignmentFor(matrix.elements<double>());
                const auto  index     = all->get().size() == 1 ? 0 : static_cast<size_t>(context->imageIndex);
                for (const auto &region : all->get().at(index))
                    if (nearCursor(region, transform, point, 8.0))
                        seeds.push_back(region);
            }
        }
    }
    if (locator.alignmentInput)
        request.inputs.emplace(*locator.alignmentInput, std::move(alignment));
    if (locator.seedInput)
        request.inputs.emplace(*locator.seedInput, DataRef<ROI>(ROI{std::move(seeds)}));
    return request;
}

void AppController::Impl::applyAssistOutcome(const AssistOutcome &outcome)
{
    if (!assistContextCurrent(outcome.context))
    {
        clearAssistPreview();
        return;
    }
    if (trainingMode)
    {
        applyTrainingAssist(outcome);
        return;
    }
    clearAssistPreview();
    if (!outcome.valid || outcome.proposal.empty() || outcome.proposal.front().empty())
    {
        LogService::write(LogLevel::Warn, LogSource::Ui, QStringLiteral("未找到有效目标：") + outcome.quality);
        return;
    }
    const auto &node    = *currentNode();
    const auto &locator = *parameterLocator();
    const auto  index   = static_cast<size_t>(outcome.context.imageIndex);
    const auto &old     = node.inputs.at(locator.regionTarget).literal;
    ROI         placed;
    if (const auto *existing = std::get_if<DataRef<ROI>>(&old))
        placed = existing->get();
    if (placed.size() == 1 && index > 0)
    {
        const auto &input = node.inputs.at(locator.imageSource);
        const auto &batch = std::get<DataRef<Image>>(outcome.context.source->outputs.at(input.source->port));
        placed.resize(batch.get().size(), placed.front());
    }
    const bool hasPrior = index < placed.size() && !placed[index].empty();
    if (index >= placed.size())
        placed.resize(index + 1);
    if (!outcome.append)
        placed[index].clear();
    const auto &newRegions = outcome.proposal.front();
    placed[index].insert(placed[index].end(), newRegions.begin(), newRegions.end());
    std::vector<LiteralWrite> writes{
        {PortStage::Inputs, locator.regionTarget, DataRef<ROI>(std::move(placed))}
    };
    for (const auto &mapping : locator.parameters)
    {
        const auto &value  = outcome.outputs.at(mapping.sourcePort);
        const auto *target = workbench::port(
            mapping.target.stage == PortStage::Init ? currentTool()->initParams : currentTool()->inputs,
            mapping.target.id);
        try
        {
            if (isMissing(value))
                throw ContractError("定位算法未提供参数：" + target->nameZh);
            validateToolValue(*target, value);
        }
        catch (const ContractError &error)
        {
            LogService::write(LogLevel::Warn, LogSource::Ui, QString::fromUtf8(error.what()));
            return;
        }
        if (outcome.append && hasPrior)
        {
            const auto *current    = mapping.target.stage == PortStage::Init ? &node.initParams.at(mapping.target.id)
                                                                             : &node.inputs.at(mapping.target.id).literal;
            bool        compatible = equalValues(*current, value);
            if (mapping.limit != SuggestedLimit::Exact)
            {
                const auto compare = [&](auto currentNumber, auto suggestedNumber)
                {
                    return mapping.limit == SuggestedLimit::Minimum ? currentNumber >= suggestedNumber
                                                                    : currentNumber <= suggestedNumber;
                };
                compatible = false;
                if (const auto *number = std::get_if<std::int64_t>(current))
                {
                    if (const auto *suggested = std::get_if<std::int64_t>(&value))
                        compatible = compare(*number, *suggested);
                }
                else if (const auto *number = std::get_if<double>(current))
                {
                    if (const auto *suggested = std::get_if<double>(&value))
                        compatible = compare(*number, *suggested);
                }
            }
            if (!compatible)
            {
                LogService::write(
                    LogLevel::Warn, LogSource::Ui,
                    QStringLiteral("候选与共享参数 %1 冲突：当前 %2，建议 %3")
                        .arg(QString::fromStdString(target->nameZh), ValueJson::text(*current, target->type),
                             ValueJson::text(value, target->type)));
                return;
            }
            continue;
        }
        writes.push_back({mapping.target.stage, mapping.target.id, value});
    }
    try
    {
        if (editor.setLiteralWrites(flow.toStdString(), node.id, writes))
            changed();
    }
    catch (const ContractError &e)
    {
        LogService::write(LogLevel::Warn, LogSource::Ui, QString::fromUtf8(e.what()));
    }
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
    return easyTouchEnabled && editingAllowed() && (!roiSession.active() || trainingMode) && readsAllowed();
}

void AppController::Impl::publishAssistState(bool enabled, const QString &reason)
{
    const bool changed   = assistEnabled != enabled || assistDisabledReason != reason;
    assistEnabled        = enabled;
    assistDisabledReason = reason;
    if (!enabled && assist)
        assist->invalidate();
    if (assistPreviewContext && !assistContextCurrent(*assistPreviewContext))
        clearAssistPreview();
    if (auto *viewport = presentation.viewport())
        viewport->setAssistActive(enabled);
    if (!changed)
        return;
    if (enabled)
        LogService::write(LogLevel::Info, LogSource::Ui,
                          trainingMode ? QStringLiteral("智能点选：单击替换训练区域，Ctrl+单击追加")
                                       : QStringLiteral("智能点选：移动鼠标预选，单击替换，Ctrl+单击追加，Esc 取消"));
    else if (easyTouchEnabled && !reason.isEmpty())
        LogService::write(LogLevel::Info, LogSource::Ui, reason);
    emit q->assistChanged();
}

void AppController::Impl::refreshAssist()
{
    if (pendingTraining)
        return;
    QString    reason;
    const auto context = assistContext(&reason);
    if (context)
    {
        const auto         &locator = *parameterLocator();
        const AssistOutputs roles{locator.previewOutput, locator.candidateOutput, locator.validOutput,
                                  locator.messageOutput.value_or(0)};
        if (assistToolId != currentTool()->nameEn || assist->helperId() != locator.algorithm || !(assistRoles == roles))
        {
            assist->configure(std::make_shared<const ToolRegistry>(registry), *registry.find(locator.algorithm), roles);
            assistToolId = currentTool()->nameEn;
            assistRoles  = roles;
        }
    }
    publishAssistState(context.has_value(), reason);
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
        impl_->report(QStringLiteral("无法写入界面设置，智能点选开关只在本次运行生效：") + UiPreferences::path());
    if (!enabled && !impl_->pendingTraining)
    {
        // Drop the preview and every queued/in-flight request, then release the persistent
        // helper off the GUI thread. Committed ROI and recommended parameters stay untouched.
        impl_->clearAssistPreview();
        if (impl_->assist)
        {
            impl_->assist->cancel();
            impl_->assist->releaseSession();
        }
    }
    LogService::write(LogLevel::Info, LogSource::Ui,
                      enabled ? QStringLiteral("智能点选已开启") : QStringLiteral("智能点选已关闭"));
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

QVariantList AppController::assistPreview() const
{
    QVariantList rows;
    for (const auto &group : impl_->assistPreviewRois)
        for (const auto &record : group)
        {
            QVariantList values;
            for (const auto value : record) values.push_back(value);
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
        impl_->clearAssistPreview();
        return false;
    }
    impl_->assist->request(std::move(*request));
    return true;
}

bool AppController::assistClick(double x, double y, bool append)
{
    if (!impl_->assistRequestsAllowed() || !impl_->assistEnabled)
    {
        LogService::write(LogLevel::Warn, LogSource::Ui, QStringLiteral("当前状态不可用智能点选"));
        return false;
    }
    QString reason;
    auto    request = impl_->assistRequest(true, append, QPointF(x, y), &reason);
    if (!request)
    {
        if (!reason.isEmpty())
            LogService::write(LogLevel::Warn, LogSource::Ui, reason);
        return false;
    }
    impl_->clearAssistPreview();
    if (!impl_->assist->request(std::move(*request)))
    {
        LogService::write(LogLevel::Warn, LogSource::Ui, QStringLiteral("正在识别上一个候选，请稍候"));
        return false;
    }
    return true;
}

void AppController::assistCancel()
{
    if (impl_->pendingTraining)
        return;
    if (impl_->assist)
        impl_->assist->cancel();
    impl_->clearAssistPreview();
}
} // namespace visionrt
