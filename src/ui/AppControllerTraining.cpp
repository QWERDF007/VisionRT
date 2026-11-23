#include "AppController_p.h"
#include "core/ExampleProject.h"
#include "core/InputNormalizer.h"
#include "core/NodeContracts.h"
#include "core/ToolPaths.h"
#include "core/ToolValueValidation.h"
#include "core/ValueJson.h"
#include "log/LogService.h"
#include "ui/DisplayConverter.h"
#include "ui/TrainingAssets.h"

#include <QImage>
#include <algorithm>
#include <string>

namespace visionrt {
namespace {

// The manifest of the revision this node published, the single source a declared trainer reads.
std::filesystem::path publishedAsset(const NodeDefinition &node)
{
    return node.trainingAsset.empty() ? std::filesystem::path{} : node.trainingAsset / L"manifest.json";
}

// A node's published revisions. A named project owns them beside its file, so copying the project
// copies what it runs; an unnamed project or an installed example has no such home and publishes
// into the workspace until the user saves a copy of its own.
std::filesystem::path trainingBase(const ProjectDefinition &project, const NodeDefinition &node,
                                   const ToolDefinition &tool, const std::filesystem::path &applicationDirectory)
{
    if (!project.path.empty() && !ExampleProject::owns(applicationDirectory, project.path))
        return training::projectBase(project.path, project.name, node.id);
    const auto name = QStringLiteral("training/%1-%2-%3")
                          .arg(QString::fromStdString(training::assetName(project.name)))
                          .arg(node.id)
                          .arg(QString::fromStdString(tool.nameEn));
    return softwareDataDir() / std::filesystem::path(name.toStdWString());
}

// Every cross-file fact a training call cannot prove on its own: the trainer exists, its ports keep
// the declared types, and every write target still agrees with the output that feeds it.
QString trainingProblem(const ToolDefinition &tool, const ToolDefinition &trainer, const TrainingSpec &spec)
{
    for (const auto &input : spec.call.inputs)
    {
        const auto *helperPort = workbench::port(trainer.inputs, input.port);
        if (!helperPort)
            return QStringLiteral("训练器缺少端口：%1 inputs.%2").arg(workbench::q(trainer.nameEn)).arg(input.port);
        TypeSpec expected{Type::Int};
        if (const auto *role = std::get_if<TrainingSetSource>(&input.source))
        {
            expected = TypeSpec{Type::TrainingSet};
        }
        else if (const auto *literal = std::get_if<TypedLiteral>(&input.source))
            expected = literal->type;
        else if (const auto *reference = std::get_if<TrainingParameterRef>(&input.source))
        {
            const auto parameter = std::ranges::find(spec.parameters, reference->id, &TrainingParameterSpec::id);
            if (parameter == spec.parameters.end())
                return QStringLiteral("训练参数不存在：%1").arg(reference->id);
            expected = parameter->type;
        }
        else if (const auto *reference = std::get_if<PortRef>(&input.source))
        {
            const auto *nodePort = reference->stage == PortStage::Init ? workbench::port(tool.initParams, reference->id)
                                 : reference->stage == PortStage::Inputs ? workbench::port(tool.inputs, reference->id)
                                                                         : nullptr;
            if (!nodePort)
                return QStringLiteral("训练调用引用了不存在的端口：%1").arg(static_cast<int>(reference->id));
            expected = nodePort->type;
        }
        if (helperPort->type != expected)
            return QStringLiteral("训练器端口类型不匹配：%1 inputs.%2")
                .arg(workbench::q(trainer.nameEn))
                .arg(input.port);
    }
    for (const auto &write : spec.writes)
    {
        const auto *source = workbench::port(trainer.outputs, write.sourcePort);
        if (!source)
            return QStringLiteral("训练器缺少输出端口：%1 outputs.%2")
                .arg(workbench::q(trainer.nameEn))
                .arg(write.sourcePort);
        const auto *target = write.target.stage == PortStage::Init ? workbench::port(tool.initParams, write.target.id)
                                                                   : workbench::port(tool.inputs, write.target.id);
        if (!target)
            return QStringLiteral("训练写入目标不存在：%1").arg(static_cast<int>(write.target.id));
        if (target->type != source->type)
            return QStringLiteral("训练写入类型不匹配：%1 outputs.%2")
                .arg(workbench::q(trainer.nameEn))
                .arg(write.sourcePort);
    }
    for (const auto &artifact : spec.artifacts)
    {
        const auto *source = workbench::port(trainer.outputs, artifact.sourcePort);
        if (!source)
            return QStringLiteral("训练器缺少模型端口：%1 outputs.%2")
                .arg(workbench::q(trainer.nameEn))
                .arg(artifact.sourcePort);
        if (source->type.type != Type::Array || source->type.dtype != DType::UInt8 || source->type.rank != 1)
            return QStringLiteral("训练模型端口必须是一维 uint8 Array：%1 outputs.%2")
                .arg(workbench::q(trainer.nameEn))
                .arg(artifact.sourcePort);
    }
    if (spec.overlayOutput != 0)
    {
        const auto *overlay = workbench::port(trainer.outputs, spec.overlayOutput);
        if (!overlay)
            return QStringLiteral("训练器缺少叠加端口：%1 outputs.%2")
                .arg(workbench::q(trainer.nameEn))
                .arg(spec.overlayOutput);
        if (overlay->type.type != Type::ROI)
            return QStringLiteral("训练叠加端口必须是 ROI：%1 outputs.%2")
                .arg(workbench::q(trainer.nameEn))
                .arg(spec.overlayOutput);
    }
    return {};
}

std::optional<TrainingFieldValue> trainingValue(const TypeSpec &type, const QVariant &value)
{
    if (type.type == Type::String)
        return value.toString().toStdString();
    try
    {
        return trainingFieldValueOf(ValueJson::parse(value.toString().toUtf8(), type));
    }
    catch (const ContractError &)
    {
        return std::nullopt;
    }
}
} // namespace

const TrainingSpec *AppController::Impl::trainingSpec() const
{
    const auto *tool = effectiveTool();
    return tool && tool->training ? &*tool->training : nullptr;
}

bool AppController::Impl::ensureTrainingSession()
{
    const auto *spec = trainingSpec();
    const auto *node = currentNode();
    if (!spec || !node)
        return false;
    auto      &view  = trainingViews[node->id];
    const auto asset = publishedAsset(*node);
    // An unsaved draft may already name the asset it will be written to; only a project that points
    // somewhere else forces a rebind.
    const bool load
        = !view.session.active() || view.session.spec() != *spec || (!asset.empty() && view.session.asset() != asset);
    if (training != &view.session || load)
        ++trainingSerial;
    training = &view.session;
    if (load)
    {
        // A project saved under another name moves its asset reference with it. The records the user
        // has not applied yet are still theirs and are in no file to read back, so the session
        // follows the new path instead of starting over.
        if (!asset.empty() && view.session.active() && view.session.spec() == *spec && view.session.dirty())
            view.session.setAsset(asset);
        else
            training->begin({node->id, *spec, asset});
        if (!training->error().isEmpty())
        {
            LogService::write(LogLevel::Error, LogSource::Ui, QStringLiteral("训练数据：") + training->error());
            return false;
        }
    }
    return true;
}

bool AppController::Impl::requestTraining(const ToolDefinition &tool, const TrainingSpec &spec)
{
    const auto *node = currentNode();
    if (!node)
        return false;
    const auto *trainer = registry.find(spec.call.tool);
    if (!trainer)
    {
        LogService::write(LogLevel::Error, LogSource::Ui,
                          QStringLiteral("训练器缺失：") + workbench::q(spec.call.tool));
        return false;
    }
    const auto problem = trainingProblem(tool, *trainer, spec);
    if (!problem.isEmpty())
    {
        LogService::write(LogLevel::Error, LogSource::Ui, QStringLiteral("训练数据：") + problem);
        return false;
    }
    if (!training->validate())
    {
        LogService::write(LogLevel::Error, LogSource::Ui, QStringLiteral("训练数据：") + training->error());
        return false;
    }

    try
    {
        Values inputs;
        for (const auto &entry : spec.call.inputs)
        {
            if (const auto *role = std::get_if<TrainingSetSource>(&entry.source))
            {
                inputs.emplace(entry.port, training->trainingSet());
                continue;
            }
            if (const auto *field = std::get_if<TrainingFieldRef>(&entry.source))
            {
                inputs.emplace(entry.port, static_cast<std::int64_t>(field->id));
                continue;
            }
            if (const auto *parameter = std::get_if<TrainingParameterRef>(&entry.source))
            {
                const auto value = training->parameter(parameter->id);
                if (!value)
                {
                    LogService::write(LogLevel::Error, LogSource::Ui,
                                      QStringLiteral("训练数据：训练器引用了未设置的训练参数"));
                    return false;
                }
                inputs.emplace(entry.port, portValueOf(*value));
                continue;
            }
            if (const auto *literal = std::get_if<TypedLiteral>(&entry.source))
            {
                inputs.emplace(entry.port, literal->value);
                continue;
            }
            const auto &reference = std::get<PortRef>(entry.source);
            if (reference.stage == PortStage::Init)
            {
                const auto found = node->initParams.find(reference.id);
                inputs.emplace(entry.port, found == node->initParams.end() ? PortValue{Missing{}} : found->second);
            }
            else
            {
                const auto *value = presentation.resolvedInput(*node, reference.id);
                inputs.emplace(entry.port, value ? *value : PortValue{Missing{}});
            }
        }
        AssistRequest request;
        request.context = {projectEpoch, generation, flow.toStdString(), node->id, node->executionRevision};
        request.context.trainingSerial = trainingSerial;
        request.learn                  = true;
        request.inputs                 = std::move(inputs);
        if (!assist->request(request, std::make_shared<const ToolRegistry>(registry), *trainer))
            throw ContractError("已有计算尚未结束，请稍后重试");
        pendingTraining = request.context;
        editor.setBusy(true);
        assistEnabled = false;
        if (auto *viewport = presentation.viewport())
            viewport->setAssistActive(false);
        emit q->runtimeChanged();
        emit q->assistChanged();
        return true;
    }
    catch (const std::exception &error)
    {
        LogService::write(LogLevel::Error, LogSource::Ui,
                          QStringLiteral("训练失败：") + QString::fromUtf8(error.what()));
        return false;
    }
}

void AppController::Impl::completeTraining(const AssistOutcome &outcome)
{
    const auto expected = *pendingTraining;
    pendingTraining.reset();
    editor.setBusy(state != RuntimeState::Idle);
    const auto *node    = currentNode();
    const auto *tool    = effectiveTool();
    const bool  current = outcome.context == expected && projectEpoch == expected.projectEpoch && node
                      && node->id == expected.node && node->executionRevision == expected.executionRevision
                      && trainingSerial == expected.trainingSerial && training && tool && tool->training;
    bool applied = false;
    if (current && outcome.status)
        applied = publishTraining(*tool, *tool->training, outcome.outputs);
    else if (current)
        LogService::write(LogLevel::Error, LogSource::Ui,
                          QStringLiteral("训练失败：") + QString::fromStdString(outcome.status.message));
    if (!applied)
        clearTrainingFit();
    assistToolId.clear();
    assist->releaseSession();
    emit q->trainingChanged();
    emit q->runtimeChanged();
    if (applied && !trainingFitRois.empty())
        showTrainingFit(trainingFitRois);
    refreshAssist();
}

bool AppController::Impl::publishTraining(const ToolDefinition &tool, const TrainingSpec &spec, const Values &outputs)
{
    const auto *node = currentNode();
    try
    {
        std::vector<LiteralWrite> writes;
        for (const auto &write : spec.writes)
        {
            const auto *target = workbench::port(write.target.stage == PortStage::Init ? tool.initParams : tool.inputs,
                                                 write.target.id);
            const auto &value = outputs.at(write.sourcePort);
            validateToolValue(*target, value);
            writes.push_back({write.target.stage, write.target.id, value});
        }
        std::map<std::string, Array> models;
        for (const auto &artifact : spec.artifacts)
        {
            const auto &value = outputs.at(artifact.sourcePort);
            const auto *model = std::get_if<Array>(&value);
            if (!model || model->type() != TypeSpec{Type::Array, DType::UInt8, 1})
                throw ContractError("训练模型必须是一维 uint8 Array");
            if (model->elementCount() == 0)
                throw ContractError("训练模型不能为空：" + artifact.id);
            models.emplace(artifact.id, *model);
        }
        std::optional<training::Revision> publication;
        try
        {
            // The revision reference is the single training fact the project stores; artifact
            // paths are derived from it when the tool is initialized.
            if (!editor.setLiteralWrites(flow.toStdString(), node->id, writes,
                                         [&]
                                         {
                                             publication.emplace(
                                                 trainingBase(editor.project(), *node, tool, applicationDirectory),
                                                 *training, models);
                                             return publication->directory();
                                         }))
                throw ContractError("训练结果与节点现状相同");
        }
        catch (const std::exception &failure)
        {
            // The revision was never referenced, so the project keeps its previous asset and the
            // next apply starts from a clean directory instead of a half-published one.
            LogService::write(LogLevel::Error, LogSource::Ui,
                              QStringLiteral("训练数据已算出，但无法写入节点：") + QString::fromUtf8(failure.what()));
            return false;
        }
        publication->accept();
        const auto &final = publication->directory();
        training->adoptAsset(final / L"manifest.json");
        // What the trainer measured, in the reference picture's coordinates. A declaration that
        // names no overlay output leaves the canvas alone.
        trainingFitRois.clear();
        if (spec.overlayOutput != 0)
        {
            const auto  found = outputs.find(spec.overlayOutput);
            const auto *fit   = found == outputs.end() ? nullptr : std::get_if<DataRef<ROI>>(&found->second);
            if (fit)
                trainingFitRois = fit->get();
        }
        LogService::write(LogLevel::Info, LogSource::Ui, QStringLiteral("训练数据已发布，节点按新结果运行"));
        emit q->projectChanged();
        emit q->parametersChanged();
        return true;
    }
    catch (const std::exception &failure)
    {
        LogService::write(LogLevel::Error, LogSource::Ui,
                          QStringLiteral("训练失败：") + QString::fromUtf8(failure.what()));
        return false;
    }
}

void AppController::Impl::showTrainingFit(const ROI &fit)
{
    trainingFitRois = fit;
    if (!trainingMode)
        return;
    if (auto *viewport = presentation.viewport())
        viewport->setAssistPreview(trainingFitRois);
}

void AppController::Impl::clearTrainingFit()
{
    const bool shown = !trainingFitRois.empty();
    trainingFitRois.clear();
    if (!shown || !trainingMode)
        return;
    if (auto *viewport = presentation.viewport())
        viewport->setAssistPreview({});
}

void AppController::Impl::showTrainingDraftRecord()
{
    if (pendingTraining)
        return;
    ++trainingSerial;
    clearAssistPreview();
    assist->invalidate();
    auto *viewport = presentation.viewport();
    if (!viewport)
        return;
    // The picture on the canvas is about to change: a line measured from the previous record would
    // now describe an input the user is no longer looking at.
    clearTrainingFit();
    const auto &records = training->records();
    const int   index   = training->selected();
    if (index < 0 || index >= static_cast<int>(records.size()))
    {
        viewport->endRoiEdit();
        viewport->setHostFrame({});
        trainingAssistImage = {};
        return;
    }
    const auto &record = records[index];
    const auto  stored = std::find_if(training->images().begin(), training->images().end(),
                                      [&](const TrainingDraftImage &image) { return image.id == record.image; });
    const auto  frame  = stored == training->images().end()
                           ? DisplayFrame{}
                           : DisplayConverter::convert(stored->pixels, stored->colorOrder);
    if (frame.image.isNull())
    {
        LogService::write(LogLevel::Error, LogSource::Ui,
                          QStringLiteral("无法显示训练图：%1")
                              .arg(frame.error.isEmpty() ? QStringLiteral("草稿中没有这张原图") : frame.error));
        viewport->endRoiEdit();
        viewport->setHostFrame({});
        trainingAssistImage = {};
        return;
    }
    // The assistant reads the same picture the user is pointing at; the stored frame stays the
    // trainer's source and this conversion only feeds the canvas.
    trainingAssistImage = stored->pixels;
    viewport->setHostFrame(frame);
    viewport->fitContain();
    viewport->setDrawShape(0);
    // The record's rectangles are edited through the same ROI mechanism the processing view uses;
    // only the owner in the target differs.
    seatTrainingRegions();
    refreshAssist();
}

void AppController::Impl::seatTrainingRegions()
{
    auto     *viewport = presentation.viewport();
    const int index    = training->selected();
    if (!viewport || index < 0 || index >= static_cast<int>(training->records().size()))
        return;
    if (const auto input = training->spec().regionInput; input != 0)
    {
        const auto *node        = currentNode();
        const auto *declaration = parameter(input, false);
        if (!node || !declaration)
            return;
        auto value = resolvedInput(*node, input);
        if (isMissing(value))
            value = DataRef<ROI>(ROI{});
        RoiEditSession::Start start;
        start.node       = node->id;
        start.input      = input;
        start.flow       = flow;
        start.type       = declaration->type;
        start.original   = node->inputs.at(input);
        start.background = DataRef<Image>(Image{trainingAssistImage});
        start.draft      = value;
        start.view       = viewport->viewState();
        roiSession.begin(std::move(start));
        const auto &regions = std::get<DataRef<ROI>>(roiSession.draft()).get();
        viewport->beginRoiEdit({node->id, input, 0, -1, roiSession.id()},
                               regions.empty() ? RegionGroup{} : regions.front(),
                               node->inputs.at(input).mode == BindingMode::Upstream);
        return;
    }
    RoiEditSession::Start start;
    start.node       = training->node();
    start.kind       = RoiTargetKind::Training;
    start.input      = training->spec().records.roiField();
    start.flow       = flow;
    start.type       = TypeSpec{Type::ROI};
    start.background = DataRef<Image>(Image{trainingAssistImage});
    start.draft      = DataRef<ROI>(ROI{training->regions(index)});
    start.original   = {BindingMode::Literal, start.draft, {}};
    start.view       = viewport->viewState();
    roiSession.begin(std::move(start));
    viewport->beginRoiEdit(roiSession.target(), training->regions(index), false, false);
}

void AppController::Impl::applyTrainingAssist(const AssistOutcome &outcome)
{
    if (!trainingMode || outcome.context.trainingSerial != trainingSerial || !training->active())
    {
        clearAssistPreview();
        LogService::write(LogLevel::Warn, LogSource::Ui, QStringLiteral("智能点选：训练数据已变化，请重新点选"));
        return;
    }
    // A preview started on one record must not land on another after the user selects a different
    // picture; the serial still matches because the session did not change, the record did.
    if (outcome.context.trainingRecord >= 0 && outcome.context.trainingRecord != training->selected())
    {
        clearAssistPreview();
        LogService::write(LogLevel::Warn, LogSource::Ui, QStringLiteral("智能点选：训练记录已切换，请重新点选"));
        return;
    }
    const int index = training->selected();
    if (index < 0 || index >= static_cast<int>(training->records().size()))
        return;
    if (!outcome.valid || outcome.proposal.empty() || outcome.proposal.front().empty())
    {
        clearAssistPreview();
        LogService::write(LogLevel::Warn, LogSource::Ui,
                          QStringLiteral("智能点选失败：")
                              + (outcome.quality.isEmpty() ? QStringLiteral("未找到有效目标") : outcome.quality));
        return;
    }
    const auto sharedInput = training->spec().regionInput;
    auto       regions     = training->regions(index);
    if (sharedInput != 0)
    {
        const auto value = resolvedInput(*currentNode(), sharedInput);
        if (const auto *roi = std::get_if<DataRef<ROI>>(&value); roi && !roi->get().empty())
            regions = roi->get().front();
        if (currentNode()->inputs.at(sharedInput).mode == BindingMode::Upstream)
        {
            report(QStringLiteral("搜索区域由上游提供，请编辑上游工具"));
            return;
        }
    }
    if (outcome.append)
    {
        // A declaration may cap the canvas field, so a click that would exceed the budget is
        // refused with the same rule the manual draw obeys; the user deletes one explicitly.
        const int budget = training->regionBudget();
        if (budget > 0 && static_cast<int>(regions.size() + outcome.proposal.front().size()) > budget)
        {
            clearAssistPreview();
            LogService::write(LogLevel::Warn, LogSource::Ui,
                              QStringLiteral("智能点选：训练记录最多 %1 个矩形，请先删除多余的矩形").arg(budget));
            return;
        }
        regions.insert(regions.end(), outcome.proposal.front().begin(), outcome.proposal.front().end());
    }
    else
        regions = outcome.proposal.front();
    auto draft = *training;
    if (sharedInput == 0)
        draft.setRegions(index, regions);
    std::vector<LiteralWrite> writes;
    if (sharedInput != 0)
        writes.push_back({PortStage::Inputs, sharedInput, DataRef<ROI>(ROI{std::move(regions)})});
    for (const auto &mapping : parameterLocator()->parameters)
    {
        const auto &value  = outcome.outputs.at(mapping.sourcePort);
        const auto *target = parameter(mapping.target.id, mapping.target.stage == PortStage::Init);
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
        writes.push_back({mapping.target.stage, mapping.target.id, value});
    }
    try
    {
        const bool edited = editor.setLiteralWrites(flow.toStdString(), currentNode()->id, writes);
        *training         = std::move(draft);
        if (edited)
            changed();
        else
            trainingEdited();
    }
    catch (const ContractError &error)
    {
        clearAssistPreview();
        LogService::write(LogLevel::Warn, LogSource::Ui, QString::fromUtf8(error.what()));
    }
}

void AppController::Impl::trainingEdited()
{
    ++trainingSerial;
    clearTrainingFit();
    clearAssistPreview();
    assist->invalidate();
    seatTrainingRegions();
    emit q->trainingChanged();
}

void AppController::Impl::commitTrainingRoi(const RoiEditTarget &target, const EncodedRegion *value)
{
    if (!trainingMode || target.kind != RoiTargetKind::Training || target.session != roiSession.id()
        || target.nodeSeq != training->node())
        return;
    roiSession.cancelPreview();
    const int index = training->selected();
    if (index < 0 || index >= static_cast<int>(training->records().size()))
        return;
    auto regions = training->regions(index);
    if (!value)
    {
        if (target.roiIndex < 0 || target.roiIndex >= static_cast<int>(regions.size()))
            return;
        regions.erase(regions.begin() + target.roiIndex);
    }
    else if (target.roiIndex < 0)
    {
        const int budget = training->regionBudget();
        if (budget > 0 && static_cast<int>(regions.size()) >= budget)
        {
            LogService::write(LogLevel::Warn, LogSource::Ui,
                              QStringLiteral("训练数据：这条记录最多 %1 个矩形，请先删除或调整已有矩形").arg(budget));
            return;
        }
        regions.push_back(*value);
    }
    else if (target.roiIndex < static_cast<int>(regions.size()))
        regions[target.roiIndex] = *value;
    else
        return;
    training->setRegions(index, std::move(regions));
    trainingEdited();
    // A committed rectangle ends the create gesture; drawing another is one explicit click away.
    if (auto *viewport = presentation.viewport())
        viewport->setDrawShape(0);
    emit q->trainingChanged();
}

void AppController::Impl::leaveTraining()
{
    if (!trainingMode)
        return;
    clearTrainingFit();
    roiSession.end();
    if (auto *viewport = presentation.viewport())
        viewport->endRoiEdit();
    presentation.setSuspended(false);
    trainingMode        = false;
    trainingAssistImage = {};
    if (assist)
        assist->invalidate();
    assistEnabled = false;
    if (auto *viewport = presentation.viewport())
        viewport->setAssistActive(false);
    emit q->trainingChanged();
    updateDisplay();
    emit q->resultsChanged();
}

bool AppController::trainingAvailable() const
{
    return impl_->trainingSpec() != nullptr;
}

bool AppController::trainingMode() const
{
    return impl_->trainingMode;
}

void AppController::setTrainingMode(bool value)
{
    if (value == impl_->trainingMode || !impl_->idle())
        return;
    if (!value)
    {
        impl_->trainingViews.at(impl_->training->node()).visible = false;
        impl_->leaveTraining();
        return;
    }
    if (!trainingAvailable())
        return;
    if (!impl_->ensureTrainingSession())
        return;
    impl_->presentation.setSuspended(true);
    impl_->trainingMode                                      = true;
    impl_->trainingViews.at(impl_->training->node()).visible = true;
    ++impl_->trainingSerial;
    impl_->showTrainingDraftRecord();
    impl_->refreshAssist();
    emit trainingChanged();
    emit resultsChanged();
}

QVariantList AppController::trainingRecords() const
{
    return impl_->training ? impl_->training->rows() : QVariantList{};
}

QString AppController::selectedTrainingDraftRecord() const
{
    if (!impl_->training)
        return {};
    const int   index   = impl_->training->selected();
    const auto &records = impl_->training->records();
    if (index < 0 || index >= static_cast<int>(records.size()))
        return {};
    return records[index].id;
}

bool AppController::trainingDirty() const
{
    return impl_->training && impl_->training->dirty();
}

bool AppController::trainingSavable() const
{
    return impl_->training && impl_->training->savable();
}

bool AppController::addTrainingImage()
{
    if (!canAddTrainingImage())
        return false;
    const auto *spec = impl_->trainingSpec();
    const auto *node = impl_->currentNode();
    if (!spec || !node)
        return false;
    const auto *value = impl_->presentation.resolvedInput(*node, spec->image.id);
    const auto *batch = value ? std::get_if<DataRef<Image>>(value) : nullptr;
    const int   index = impl_->presentation.imageIndex();
    if (!batch || batch->get().empty() || index < 0 || static_cast<std::size_t>(index) >= batch->get().size())
    {
        LogService::write(LogLevel::Warn, LogSource::Ui, QStringLiteral("训练数据：声明的图像端口还没有发布可用的帧"));
        emit trainingChanged();
        return false;
    }
    if (!impl_->ensureTrainingSession())
    {
        emit trainingChanged();
        return false;
    }
    const auto &binding = node->inputs.at(spec->image.id);
    const auto snapshot = effectiveSource(binding) ? impl_->presentation.sharedSnapshot(binding.source->node) : nullptr;
    if (!snapshot)
    {
        LogService::write(LogLevel::Warn, LogSource::Ui, QStringLiteral("训练原图缺少发布身份，请运行图像来源"));
        return false;
    }
    const auto source = QStringLiteral("%1:%2:%3:%4:%5:%6:%7")
                            .arg(snapshot->generation)
                            .arg(snapshot->session)
                            .arg(snapshot->round)
                            .arg(snapshot->publication)
                            .arg(binding.source->node)
                            .arg(binding.source->port)
                            .arg(index);
    const auto *producerNode = impl_->findNode(binding.source->node);
    const auto *producer     = producerNode ? impl_->registry.find(producerNode->toolId) : nullptr;
    if (!producer)
    {
        LogService::write(LogLevel::Warn, LogSource::Ui, QStringLiteral("训练原图的工具声明缺失"));
        return false;
    }
    const auto  producerContract = nodeContract(*producerNode, *producer);
    const auto *sourcePort       = workbench::port(producerContract.outputs, binding.source->port);
    if (!sourcePort)
    {
        LogService::write(LogLevel::Warn, LogSource::Ui, QStringLiteral("训练原图的输出声明缺失"));
        return false;
    }
    const auto order = sourcePort->colorOrder;
    const auto added = impl_->training->addImage(batch->get()[static_cast<std::size_t>(index)], order, source);
    if (!added)
    {
        LogService::write(LogLevel::Error, LogSource::Ui, QStringLiteral("训练数据：") + impl_->training->error());
        emit trainingChanged();
        return false;
    }
    impl_->presentation.setSuspended(true);
    impl_->trainingMode                                      = true;
    impl_->trainingViews.at(impl_->training->node()).visible = true;
    ++impl_->trainingSerial;
    impl_->showTrainingDraftRecord();
    emit trainingChanged();
    emit resultsChanged();
    return true;
}

bool AppController::canAddTrainingImage() const
{
    const auto *viewport = impl_->presentation.viewport();
    const auto *spec     = impl_->trainingSpec();
    if (!trainingAvailable() || !spec || impl_->trainingMode || !impl_->editingAllowed() || !viewport
        || !viewport->hasImage())
        return false;
    // "加入训练" copies what the panel is showing, so the selector must be on the declared frame
    // port; another output would train on pixels the declaration never named.
    return impl_->presentation.selectedImageOutput() == QStringLiteral("inputs:%1").arg(spec->image.id);
}

void AppController::selectTrainingDraftRecord(const QString &id)
{
    if (!impl_->trainingMode || !impl_->idle())
        return;
    const auto &records = impl_->training->records();
    for (std::size_t index = 0; index < records.size(); ++index)
        if (records[index].id == id)
        {
            if (!impl_->training->select(static_cast<int>(index)))
                return;
            impl_->showTrainingDraftRecord();
            emit trainingChanged();
            return;
        }
}

void AppController::removeTrainingDraftRecord(const QString &id)
{
    if (!impl_->trainingMode || !impl_->idle())
        return;
    const auto &records = impl_->training->records();
    for (std::size_t index = 0; index < records.size(); ++index)
        if (records[index].id == id)
        {
            if (!impl_->training->remove(static_cast<int>(index)))
                return;
            impl_->showTrainingDraftRecord();
            emit trainingChanged();
            return;
        }
}

bool AppController::setTrainingField(const QString &id, int field, const QVariant &value)
{
    if (!impl_->trainingMode || !impl_->idle() || !impl_->training)
        return false;
    const TrainingFieldSpec *declared = nullptr;
    for (const auto &entry : impl_->training->spec().records.fields)
        if (entry.id == static_cast<PortId>(field))
            declared = &entry;
    if (!declared || declared->roi)
        return false;
    const auto converted = trainingValue(declared->type, value);
    if (!converted)
        return false;
    const auto &records = impl_->training->records();
    for (std::size_t index = 0; index < records.size(); ++index)
        if (records[index].id == id)
        {
            if (impl_->training->setField(static_cast<int>(index), declared->id, *converted))
            {
                impl_->trainingEdited();
                return true;
            }
            return false;
        }
    return false;
}

QVariantList AppController::trainingParameters() const
{
    return impl_->training ? impl_->training->parameterRows() : QVariantList{};
}

bool AppController::setTrainingParameter(int id, const QVariant &value)
{
    if (!impl_->trainingMode || !impl_->idle() || !impl_->training)
        return false;
    const TrainingParameterSpec *declared = nullptr;
    for (const auto &entry : impl_->training->spec().parameters)
        if (entry.id == static_cast<PortId>(id))
            declared = &entry;
    if (!declared)
        return false;
    const auto converted = trainingValue(declared->type, value);
    if (!converted || !impl_->training->setParameter(declared->id, *converted))
        return false;
    impl_->trainingEdited();
    return true;
}

void AppController::applyTraining()
{
    if (!impl_->trainingMode || !impl_->idle())
        return;
    if (!impl_->ensureTrainingSession())
    {
        emit trainingChanged();
        return;
    }
    const auto *tool = impl_->effectiveTool();
    const auto *spec = impl_->trainingSpec();
    if (!tool || !spec)
    {
        emit trainingChanged();
        return;
    }
    const bool applied = impl_->requestTraining(*tool, *spec);
    if (!applied)
    {
        // A draft that changed the search bands no longer has a measurement to show.
        impl_->clearTrainingFit();
        emit trainingChanged();
        return;
    }

    emit trainingChanged();
}

} // namespace visionrt
