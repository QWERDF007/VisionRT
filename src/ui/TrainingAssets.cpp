#include "ui/TrainingAssets.h"

#include "ui/TrainingSession.h"
#include "core/ToolRegistry.h"
#include "core/ToolPaths.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QScopeGuard>
#include <QUuid>
#include <algorithm>
#include <system_error>

namespace visionrt::training {
namespace {
// The next revision number that does not collide with a directory this target already holds. An
// existing revision is never overwritten: it may belong to a different project saved earlier into
// the same directory.
int nextRevision(const std::filesystem::path &base)
{
    int             next = 1;
    std::error_code error;
    if (!std::filesystem::is_directory(base, error))
        return next;
    for (const auto &entry : std::filesystem::directory_iterator(base, error))
    {
        const auto name = entry.path().filename().string();
        if (entry.is_directory() && !name.empty() && name.find_first_not_of("0123456789") == std::string::npos)
            next = std::max(next, std::stoi(name) + 1);
    }
    return next;
}
} // namespace

Revision::Revision(const std::filesystem::path &base, const TrainingSession &draft,
                   const std::map<std::string, ModelArtifact> &models,
                   const std::map<std::string, std::filesystem::path> &destinations)
{
    const auto revision    = nextRevision(base);
    const auto destination = base / std::to_wstring(revision);
    directory_             = base / (L".staging-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdWString());
    auto discard           = qScopeGuard(
        [this]
        {
            std::error_code ignored;
            std::filesystem::remove_all(directory_, ignored);
        });
    std::filesystem::create_directories(directory_);
    if (!models.empty()) std::filesystem::create_directories(directory_ / L"models");
    std::map<std::string, QString> artifacts;
    for (const auto &[id, model] : models)
    {
        const auto relative = QString::fromStdString("models/" + id + ".bin");
        QFile file(QString::fromStdWString((directory_ / std::filesystem::path(relative.toStdWString())).wstring()));
        const auto &bytes = model.bytes;
        if (!file.open(QIODevice::WriteOnly)
            || file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<qint64>(bytes.size()))
                   != static_cast<qint64>(bytes.size()))
            throw ContractError("无法写入训练模型：" + file.errorString().toStdString());
        file.close();
        artifacts.emplace(id, relative);
    }
    QSaveFile file(QString::fromStdWString((directory_ / L"manifest.json").wstring()));
    file.setDirectWriteFallback(false);
    const auto content = QJsonDocument(draft.manifest(artifacts)).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size() || !file.commit())
        throw ContractError("无法提交训练清单：" + file.errorString().toStdString());
    std::filesystem::rename(directory_, destination);
    directory_ = destination;
    for (const auto &[id, path] : destinations)
    {
        if (path.empty()) continue;
        std::filesystem::create_directories(path.parent_path());
        QSaveFile modelFile(QString::fromStdWString(path.wstring()));
        modelFile.setDirectWriteFallback(false);
        const auto &bytes = models.at(id).bytes;
        if (!modelFile.open(QIODevice::WriteOnly)
            || modelFile.write(reinterpret_cast<const char *>(bytes.data()), static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size())
            || !modelFile.commit())
            throw ContractError("无法保存训练模型：" + modelFile.errorString().toStdString());
    }
    discard.dismiss();
}

Revision::~Revision()
{
    if (!accepted_)
    {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }
}

std::string assetName(const std::string &projectName)
{
    QString stem = QString::fromStdString(projectName).trimmed();
    if (stem.isEmpty())
        stem = QStringLiteral("project");
    for (auto &character : stem)
        if (QStringLiteral("<>:\"/\\|?*").contains(character) || character.unicode() < 0x20)
            character = QChar(u'_');
    return stem.toStdString();
}

std::filesystem::path projectBase(const std::filesystem::path &projectFile, const std::string &projectName, NodeId node)
{
    return projectFile.parent_path()
         / std::filesystem::path(QString::fromStdString(assetName(projectName) + ".assets").toStdWString())
         / L"training" / std::to_wstring(node);
}

Revision::Revision(const std::filesystem::path &base, const std::filesystem::path &source)
{
    if (!std::filesystem::is_directory(source))
        throw ContractError("训练资产缺失：" + source.string());
    const auto destination = base / std::to_wstring(nextRevision(base));
    directory_             = base / (L".staging-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdWString());
    auto discard           = qScopeGuard(
        [this]
        {
            std::error_code ignored;
            std::filesystem::remove_all(directory_, ignored);
        });
    std::filesystem::create_directories(base);
    std::filesystem::copy(source, directory_, std::filesystem::copy_options::recursive);
    std::filesystem::rename(directory_, destination);
    directory_ = destination;
    discard.dismiss();
}

SaveAssets::SaveAssets(ProjectDefinition &project, const std::filesystem::path &destination, const ToolRegistry &registry)
{
    for (auto &flow : project.flows)
        for (auto &node : flow.nodes)
        {
            if (node.trainingAsset.empty())
                continue;
            const auto base = projectBase(destination, project.name, node.id);
            if (node.trainingAsset.parent_path() == base)
                continue;
            auto revision      = std::make_unique<Revision>(base, node.trainingAsset);
            if (const auto *tool = registry.find(node.toolId); tool && tool->training)
                for (const auto &artifact : tool->training->artifacts)
                {
                    auto &value = node.trainParams.at(artifact.target.id);
                    const auto configured = pathFromUtf8(std::get<std::string>(value));
                    if (configured.empty()) continue;
                    const auto source = configured.is_absolute() ? configured : project.path.parent_path() / configured;
                    const auto relative = source.lexically_normal().lexically_relative(node.trainingAsset.lexically_normal());
                    if (!relative.empty() && *relative.begin() != "..")
                    {
                        const auto newPath = (revision->directory() / relative).lexically_relative(destination.parent_path());
                        const auto text = newPath.generic_u8string();
                        value = std::string(reinterpret_cast<const char *>(text.data()), text.size());
                    }
                    else if (configured.is_relative() && source != destination.parent_path() / configured)
                    {
                        const auto target = destination.parent_path() / configured;
                        std::filesystem::create_directories(target.parent_path());
                        QFile input(QString::fromStdWString(source.wstring()));
                        QSaveFile output(QString::fromStdWString(target.wstring()));
                        output.setDirectWriteFallback(false);
                        if (!input.open(QIODevice::ReadOnly)) throw ContractError("模型文件无法读取");
                        const auto bytes = input.readAll();
                        if (input.error() != QFileDevice::NoError || !output.open(QIODevice::WriteOnly)
                            || output.write(bytes) != bytes.size() || !output.commit())
                            throw ContractError("模型文件无法复制");
                    }
                }
            node.trainingAsset = revision->directory();
            revisions_.push_back(std::move(revision));
        }
}

void SaveAssets::accept() noexcept
{
    for (auto &revision : revisions_) revision->accept();
}

} // namespace visionrt::training
