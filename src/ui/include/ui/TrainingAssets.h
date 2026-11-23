#pragma once

#include "core/Project.h"
#include "ui/Export.h"

#include <QString>
#include <filesystem>
#include <map>
#include <memory>
#include <vector>

namespace visionrt {
class TrainingSession;
}

namespace visionrt::training {

VISIONRT_UI_API void writeFrame(const cv::Mat &pixels, ColorOrder order, const QString &path);
VISIONRT_UI_API void readFrame(const QString &path, cv::Mat &pixels, ColorOrder &order);

// Owns one complete unpublished revision until the project transaction accepts its directory.
// A refusal or exception removes only this revision, including its staging files.
class VISIONRT_UI_API Revision final
{
public:
    Revision(const std::filesystem::path &base, const TrainingSession &draft,
             const std::map<std::string, Array> &models);
    Revision(const std::filesystem::path &base, const std::filesystem::path &source);
    ~Revision();
    Revision(const Revision &)            = delete;
    Revision &operator=(const Revision &) = delete;

    const std::filesystem::path &directory() const noexcept
    {
        return directory_;
    }

    void accept() noexcept
    {
        accepted_ = true;
    }

private:
    std::filesystem::path directory_;
    bool                  accepted_ = false;
};

// One project name reduced to a directory component that is valid on every platform the product
// ships on. The workspace layout and the beside-project tree both start from it.
VISIONRT_UI_API std::string assetName(const std::string &projectName);

// The asset tree a named project owns: <project>.assets/training/<node>/<revision>. Both the
// publisher and the save-time copier derive the location here, so a saved reference always names
// the place the next publish or copy writes to.
VISIONRT_UI_API std::filesystem::path projectBase(const std::filesystem::path &projectFile,
                                                  const std::string &projectName, NodeId node);

// Owns copied revisions until the project file has committed all references to them.
class VISIONRT_UI_API SaveAssets final
{
public:
    SaveAssets(ProjectDefinition &project, const std::filesystem::path &destination);
    SaveAssets(const SaveAssets &)            = delete;
    SaveAssets &operator=(const SaveAssets &) = delete;
    void        accept() noexcept;

private:
    std::vector<std::unique_ptr<Revision>> revisions_;
};

} // namespace visionrt::training
