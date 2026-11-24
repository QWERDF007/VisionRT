#include "core/ToolRegistry.h"
// A training draft lives in memory until the user commits it. The contract is: "加入训练" copies the
// published frame exactly (depth, channels and colour order), records share one image entry, fields
// are read and written by the declaration's number, and nothing reaches the disk while editing.
#include "ui/TrainingAssets.h"
#include "ui/TrainingSession.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

EncodedRegion rectangle(double centerX, double centerY)
{
    return makeRectangle(centerX - 5.0, centerY - 4.0, 10.0, 8.0, 0.0);
}

QJsonObject readAsset(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "asset must be readable");
    return QJsonDocument::fromJson(file.readAll()).object();
}

PortDefinition recordParameter(PortId id, std::string name, TypeSpec type, bool roi, int minimum, int maximum)
{
    PortDefinition port;
    port.id = id; port.nameZh = std::move(name); port.type = type;
    port.scope = ParameterScope::Record; port.roi = roi; port.minRegions = minimum; port.maxRegions = maximum;
    return port;
}

TrainingSession::Begin colourBegin(const QString &asset)
{
    TrainingSession::Begin begin;
    begin.node                = 1;
    begin.spec.image          = PortRef{PortStage::Inputs, 1};
    begin.trainParams = {
        recordParameter(1, "颜色名称", TypeSpec{Type::String}, false, 0, 0),
        recordParameter(2, "采样区", TypeSpec{Type::ROI}, true, 1, 0),
    };
    begin.asset = std::filesystem::path(asset.toStdWString());
    return begin;
}

TrainingSession::Begin edgeBegin(const QString &asset)
{
    TrainingSession::Begin begin;
    begin.node                  = 2;
    begin.spec.image            = PortRef{PortStage::Inputs, 1};
    begin.spec.maxRecords = 1;
    begin.trainParams   = {
        recordParameter(1, "参考边", TypeSpec{Type::ROI}, true, 2, 2),
    };
    begin.asset = std::filesystem::path(asset.toStdWString());
    return begin;
}

int entries(const QString &directory)
{
    return QDir(directory).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size();
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    QTemporaryDir    directory;
    require(directory.isValid(), "temporary directory must exist");
    const auto asset = directory.path() + QStringLiteral("/colors.json");

    try
    {
        TrainingSession session;
        session.begin(colourBegin(asset));
        require(session.active() && session.records().empty(), "a new session starts empty");
        require(session.images().empty(), "a new session holds no frames");
        require(!session.savable(), "an empty set is not applicable");

        // A 16-bit frame stands in for every picture a display conversion would change: the draft
        // must keep the stored pixels and the declared order, not a normalised QImage.
        cv::Mat frame(6, 7, CV_16UC3, cv::Scalar(1000, 2000, 3000));
        frame.at<cv::Vec3w>(0, 0) = cv::Vec3w{10, 20, 30};
        frame.at<cv::Vec3w>(1, 2) = cv::Vec3w{4000, 5000, 6000};
        const auto added          = session.addImage(frame, ColorOrder::BGR, QStringLiteral("frame-1"));
        require(added.has_value() && session.selected() == 0, "adding a frame selects its record");
        require(session.images().size() == 1 && session.records().size() == 1, "one frame, one record");
        const auto &stored = session.images().front();
        require(stored.pixels.type() == CV_16UC3, "the draft kept the stored depth and channels");
        require(stored.colorOrder == ColorOrder::BGR, "the draft kept the declared colour order");
        require(stored.pixels.at<cv::Vec3w>(0, 0)[0] == 10 && stored.pixels.at<cv::Vec3w>(0, 0)[2] == 30,
                "the draft kept the stored pixel values");
        require(!stored.thumbnail.isEmpty() && stored.thumbnail.startsWith(QStringLiteral("data:image/png")),
                "the list thumbnail comes from the draft");

        // The draft owns its pixels: changing the published frame afterwards cannot change training.
        frame.at<cv::Vec3w>(0, 0) = cv::Vec3w{0, 0, 0};
        require(stored.pixels.at<cv::Vec3w>(0, 0)[0] == 10, "the draft copied the frame");

        // The same published frame added twice is one image with two records, so a trainer sees the
        // original picture once even when two labels were painted on it.
        const auto second = session.addImage(frame, ColorOrder::BGR, QStringLiteral("frame-1"));
        require(second.has_value() && session.images().size() == 1 && session.records().size() == 2,
                "two records share the frame they came from");
        require(session.records()[0].image == session.records()[1].image, "records name one image");
        require(session.records()[0].id != session.records()[1].id, "each record keeps its own identity");

        // Editing is purely in memory. The observation point is the whole temporary directory: a
        // picture beside the asset or a second file would show up here.
        require(!std::filesystem::exists(session.asset()), "adding a frame wrote the asset");
        require(entries(directory.path()) == 0, "adding a frame created a file");
        require(!session.savable(), "a record without a name or rectangle is not applicable");
        require(session.setField(0, 1, std::string("red")), "the name field is writable by number");
        require(!session.setField(0, 2, DataRef<ROI>(ROI{})), "the canvas field is not a single value");
        require(session.setRegions(0, {rectangle(3.0, 4.0)}), "the canvas field takes a group");
        require(session.dirty(), "editing marks the draft dirty");
        // An incomplete sibling record is editable but keeps the whole draft inapplicable; the
        // apply qualification is separate from being allowed to edit the first edge.
        require(!session.savable(), "an incomplete sibling record must keep the draft inapplicable");
        require(session.setField(1, 1, std::string("blue")), "the second record takes a name");
        require(session.setRegions(1, {rectangle(5.0, 6.0)}), "the second record takes a group");
        require(session.savable(), "named records with rectangles are applicable");
        require(entries(directory.path()) == 0, "editing created a file");

        // Fields are keyed by the declaration's number: a renamed field keeps its value, and the
        // training set carries the declared types a trainer reads.
        const auto set = session.trainingSet();
        require(set.get().images.size() == 1 && set.get().records.size() == 2, "the call value mirrors the draft");
        const auto &firstImage = set.get().images.begin()->second;
        require(firstImage.pixels.type() == CV_16UC3 && firstImage.colorOrder == ColorOrder::BGR,
                "the call value carries the stored frame");
        require(firstImage.pixels.at<cv::Vec3w>(0, 0)[1] == 20, "the call value carries the stored pixels");
        const auto &firstRecord = set.get().records.begin()->second;
        require(std::get<std::string>(firstRecord.fields.at(1)) == "red", "the name keeps its type");
        const auto &regions = std::get<DataRef<ROI>>(firstRecord.fields.at(2)).get();
        require(regions.size() == 1 && regions.front().size() == 1, "the canvas field carries one group");
        const auto region = RegionView(regions.front().front()).rectangle();
        require(std::abs(region.center().x - 3.0) < 1e-6 && std::abs(region.center().y - 4.0) < 1e-6,
                "the call value carries the drawn geometry");

        // A declaration that numbers its fields differently is the same draft: the ids are the
        // contract, not the names or their order.
        const auto renamedAsset     = directory.path() + QStringLiteral("/renamed.json");
        auto       renamed          = colourBegin(renamedAsset);
        renamed.trainParams = {
            recordParameter(5, "类别", TypeSpec{Type::String}, false, 0, 0),
            recordParameter(9, "区域", TypeSpec{Type::ROI}, true, 1, 0),
        };
        TrainingSession renamedSession;
        renamedSession.begin(renamed);
        require(renamedSession.addImage(frame, ColorOrder::BGR, QStringLiteral("frame-1")).has_value(),
                "a renamed declaration still accepts a frame");
        require(renamedSession.setField(0, 5, std::string("purple")), "the renamed name field is writable");
        require(renamedSession.setRegions(0, {rectangle(5.0, 6.0)}), "the renamed canvas field is writable");
        const auto renamedSet = renamedSession.trainingSet();
        require(std::get<std::string>(renamedSet.get().records.begin()->second.fields.at(5)) == "purple",
                "the renamed field reaches its own number");
        require(std::get<DataRef<ROI>>(renamedSet.get().records.begin()->second.fields.at(9)).get().size() == 1,
                "the renamed canvas field reaches its own number");

        training::Revision publication(std::filesystem::path(directory.path().toStdWString()) / L"published", session,
                                       {});
        publication.accept();
        const auto manifest = publication.directory() / L"manifest.json";
        session.adoptAsset(manifest);
        require(!session.dirty(), "accepted publication is the persisted version");
        const auto storedAsset = readAsset(QString::fromStdWString(manifest.wstring()));
        require(storedAsset.value("records").toArray().size() == 2, "published records");
        TrainingSession reopened;
        reopened.begin(colourBegin(QString::fromStdWString(manifest.wstring())));
        require(reopened.records().size() == 2, "reload reads the applied records");
        require(std::get<std::string>(*reopened.value(0, 1)) == "red", "reload reads the typed name");
        require(reopened.regions(0).size() == 1, "reload reads the canvas group");
        require(reopened.images().size() == 1, "reload rebuilds one image for the shared file");
        require(reopened.savable(), "reloaded records are applicable");

        TrainingSession sparse;
        sparse.begin(colourBegin({}));
        sparse.addImage(frame, ColorOrder::BGR, QStringLiteral("first"));
        sparse.addImage(frame, ColorOrder::BGR, QStringLiteral("second"));
        sparse.setField(1, 1, std::string("red"));
        sparse.setRegions(1, {rectangle(5, 6)});
        sparse.remove(0);
        const auto         survivingImage = sparse.images().front().id;
        training::Revision sparsePublication(std::filesystem::path(directory.path().toStdWString()) / L"sparse", sparse,
                                             {});
        sparsePublication.accept();
        TrainingSession sparseReloaded;
        sparseReloaded.begin(
            colourBegin(QString::fromStdWString((sparsePublication.directory() / L"manifest.json").wstring())));
        require(sparseReloaded.images().size() == 1 && sparseReloaded.records().size() == 1
                    && sparseReloaded.images().front().id == survivingImage,
                "reopening must preserve image identities after deleting an earlier image");

        ProjectDefinition bundle;
        bundle.name = "bundle";
        bundle.flows.emplace_back();
        auto &bundleNodes = bundle.flows.back().nodes;
        bundleNodes.emplace_back();
        bundleNodes.back().id            = 1;
        bundleNodes.back().trainingAsset = publication.directory();
        bundleNodes.emplace_back();
        bundleNodes.back().id            = 2;
        bundleNodes.back().trainingAsset = publication.directory() / L"missing";
        const auto copyRoot              = std::filesystem::path(directory.path().toStdWString()) / L"failed-copy";
        bool       copyFailed            = false;
        try
        {
            ToolRegistry registry;
            training::SaveAssets assets(bundle, copyRoot / L"project.json", registry);
        }
        catch (const std::exception &)
        {
            copyFailed = true;
        }
        require(copyFailed, "a missing later revision must refuse the save");
        for (const auto &entry : std::filesystem::recursive_directory_iterator(copyRoot))
            require(!entry.is_regular_file(), "a later copy failure must remove earlier unreferenced copies");

        // A malformed asset is reported, never silently replaced with an empty set on save.
        const auto broken = directory.path() + QStringLiteral("/broken.json");
        QFile      brokenFile(broken);
        require(brokenFile.open(QIODevice::WriteOnly), "broken fixture must be writable");
        brokenFile.write("{ \"schema_version\": 2, \"kind\": 0, \"records\": [ {} ] }");
        brokenFile.close();
        TrainingSession damaged;
        damaged.begin(colourBegin(broken));
        require(damaged.records().empty() && !damaged.error().isEmpty(),
                "a malformed asset is reported instead of becoming an empty set");

        // A one-record tool declares its budget: exactly two rectangles on one reference frame, and
        // replacing the frame keeps edges only while the geometry still describes the part.
        const auto edgesAsset = directory.path() + QStringLiteral("/edges.json");
        TrainingSession edges;
        edges.begin(edgeBegin(edgesAsset));
        require(edges.regionBudget() == 2, "the declared canvas budget is two rectangles");
        require(edges.addImage(frame, ColorOrder::BGR, QStringLiteral("frame-1")).has_value(),
                "the reference frame is accepted");
        require(edges.records().size() == 1, "edge training keeps one record");
        require(!edges.savable(), "one edge is not a pose");
        edges.setRegions(0, {makeRectangle(10 - 15, 10 - 2.5, 30, 5, 0)});
        require(!edges.savable(), "a single edge is below the declared minimum");
        edges.setRegions(0, {makeRectangle(10 - 15, 10 - 2.5, 30, 5, 0),
                             makeRectangle(20 - 2.5, 20 - 15, 5, 30, 0)});
        require(edges.savable(), "two rectangles describe two edges");
        training::Revision edgePublication(std::filesystem::path(directory.path().toStdWString()) / L"edge-published",
                                           edges, {});
        edgePublication.accept();
        require(edges.addImage(frame, ColorOrder::BGR, QStringLiteral("frame-1")).has_value(),
                "replacing with the same geometry is allowed");
        require(edges.records().size() == 1 && edges.regions(0).size() == 2,
                "a same-sized reference keeps the drawn edges");
        cv::Mat wider(8, 7, CV_16UC3, cv::Scalar(1, 2, 3));
        require(edges.addImage(wider, ColorOrder::BGR, QStringLiteral("frame-2")).has_value(),
                "a new reference frame is accepted");
        require(edges.records().size() == 1 && edges.regions(0).empty(),
                "a differently sized reference drops edges that no longer describe it");

        // Deleting a record releases the frame only when nothing else points at it.
        TrainingSession release;
        release.begin(colourBegin(directory.path() + QStringLiteral("/release.json")));
        release.addImage(frame, ColorOrder::BGR, QStringLiteral("frame-1"));
        release.addImage(frame, ColorOrder::BGR, QStringLiteral("frame-1"));
        require(release.images().size() == 1, "two records still share one frame");
        release.remove(1);
        require(release.images().size() == 1, "the frame stays while a record names it");
        release.remove(0);
        require(release.images().empty() && release.records().empty(), "the last removal releases the frame");

        auto noDefaults            = colourBegin({});
        noDefaults.trainParams.insert(noDefaults.trainParams.end(), {
            {5, "数量",    TypeSpec{Type::Int}},
            {8, "说明", TypeSpec{Type::String}}
        });
        TrainingSession explicitParameters;
        explicitParameters.begin(noDefaults);
        require(!explicitParameters.parameter(5) && !explicitParameters.parameter(8),
                "an unspecified training parameter must remain unset");
        explicitParameters.addImage(frame, ColorOrder::BGR, QStringLiteral("frame-1"));
        explicitParameters.setField(0, 1, std::string("red"));
        explicitParameters.setRegions(0, {rectangle(3, 4)});
        require(!explicitParameters.savable(), "unset parameters must prevent publication");
        require(explicitParameters.setParameter(5, std::int64_t{0}), "an explicit zero parameter is valid");
        require(!explicitParameters.savable(), "an unset string must stay distinct from an empty string");
        require(explicitParameters.setParameter(8, std::string{}), "an explicit empty string is valid");
        require(explicitParameters.savable(), "explicit typed values make the draft applicable");
        training::Revision explicitPublication(std::filesystem::path(directory.path().toStdWString()) / L"explicit",
                                               explicitParameters, {});
        explicitPublication.accept();
        const auto explicitAsset = explicitPublication.directory() / L"manifest.json";
        require(!readAsset(QString::fromStdWString(explicitAsset.wstring())).contains("parameters"),
                "training manifests must not duplicate project parameter values");
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "Training drafts keep raw frames, typed fields and in-memory records\n";
    return 0;
}
