// Training is a host-side session over one asset file. The tool reads that file and nothing else,
// so the contract is: what the window edits is exactly what the file says after a save, and a
// reload sees the same records with their rectangles intact.
#include "ui/TrainingSession.h"

#include <QFile>
#include <QGuiApplication>
#include <QImage>
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
        session.begin({1, TrainingKind::ColorSamples, std::filesystem::path(asset.toStdWString())});
        require(session.active() && session.records().empty(), "a new session starts empty");
        require(!session.savable(), "an empty set is not applicable");

        QImage picture(16, 16, QImage::Format_RGB888);
        picture.fill(QColor(200, 30, 40));
        const auto added = session.addImage(picture);
        require(added.has_value() && session.selected() == 0, "adding an image selects its record");
        const auto &record = session.records().front();
        require(!record.label.isEmpty() && record.regions.empty(), "a new record has a name and no rectangles yet");
        require(std::filesystem::is_regular_file(record.image), "the picture is written beside the asset");
        require(session.dirty(), "adding marks the session dirty");

        // A record without a rectangle or without a class name cannot be applied, because the tool
        // rejects exactly those files.
        require(!session.savable(), "a record without a rectangle is not applicable");
        session.setRegions(0, {rectangle(8, 8)});
        require(session.savable(), "named records with rectangles are applicable");
        session.setLabel(0, "  ");
        require(!session.savable(), "a blank class name is refused");
        session.setLabel(0, "red");
        require(session.save(), session.error().toStdString().c_str());
        require(!session.dirty(), "saving clears the dirty mark");

        const auto stored = readAsset(asset);
        require(stored.value("schema_version").toInt() == 2, "asset carries its schema version");
        require(stored.value("kind").toInt() == static_cast<int>(TrainingKind::ColorSamples), "asset names its kind");
        const auto records = stored.value("records").toArray();
        require(records.size() == 1, "one record is written");
        const auto written = records[0].toObject();
        require(written.value("label").toString() == QStringLiteral("red"), "the class name is written");
        const auto regions = written.value("regions").toArray();
        require(regions.size() == 1 && regions[0].toArray().size() == 5, "regions are centre based");
        require(std::abs(regions[0].toArray()[0].toDouble() - 8.0) < 1e-9, "region centre X is written");

        // A fresh session over the same asset sees the applied records, so reopening the tool
        // continues from what the file says rather than from a second copy.
        TrainingSession reopened;
        reopened.begin({1, TrainingKind::ColorSamples, std::filesystem::path(asset.toStdWString())});
        require(reopened.records().size() == 1, "reload reads the saved record");
        require(reopened.records().front().label == QStringLiteral("red"), "reload reads the class name");
        require(reopened.records().front().regions.size() == 1, "reload reads the rectangles");
        require(std::filesystem::is_regular_file(reopened.records().front().image),
                "reload resolves the stored picture");
        require(reopened.savable(), "reloaded records are applicable");

        // A malformed asset is reported, never silently replaced with an empty set on save.
        const auto broken = directory.path() + QStringLiteral("/broken.json");
        QFile      brokenFile(broken);
        require(brokenFile.open(QIODevice::WriteOnly), "broken fixture must be writable");
        brokenFile.write("{ \"schema_version\": 1, \"kind\": \"color_samples\", \"records\": [ {} ] }");
        brokenFile.close();
        TrainingSession damaged;
        damaged.begin({1, TrainingKind::ColorSamples, std::filesystem::path(broken.toStdWString())});
        require(damaged.records().empty() && !damaged.error().isEmpty(),
                "a malformed asset is reported instead of becoming an empty set");

        // A straight-edge model is one reference picture plus exactly two rectangles: the long side
        // is the reference edge and the short side is the band the tool searches across.
        const auto edgesAsset = directory.path() + QStringLiteral("/edges.json");
        TrainingSession edges;
        edges.begin({2, TrainingKind::StraightEdges, std::filesystem::path(edgesAsset.toStdWString())});
        require(edges.regionBudget() == 2 && !edges.colorSamples(), "edge training holds two rectangles");
        require(edges.addImage(picture).has_value(), "the reference picture is written");
        require(edges.records().size() == 1, "edge training keeps one reference record");
        require(!edges.savable(), "one edge is not a pose");
        edges.setRegions(0, {makeRectangle(10 - 15, 10 - 2.5, 30, 5, 0),
                             makeRectangle(20 - 2.5, 20 - 15, 5, 30, 0)});
        require(edges.savable(), "two rectangles describe two edges");
        require(edges.save(), edges.error().toStdString().c_str());

        const auto edgeDocument = readAsset(edgesAsset);
        require(edgeDocument.value("kind").toInt() == static_cast<int>(TrainingKind::StraightEdges),
                "edge asset names its kind");
        require(!edgeDocument.value("reference_image").toString().isEmpty(), "edge asset names its picture");
        const auto segments = edgeDocument.value("segments").toArray();
        require(segments.size() == 2, "both edges are written");
        const auto horizontal = segments[0].toObject();
        const auto from       = horizontal.value("from").toArray();
        const auto to         = horizontal.value("to").toArray();
        // A wide rectangle is a horizontal edge: the endpoints sit on its long axis, not its corners.
        require(std::abs(from[0].toDouble() - (-5.0)) < 1e-6 && std::abs(from[1].toDouble() - 10.0) < 1e-6,
                "the horizontal edge starts at the left end of its long axis");
        require(std::abs(to[0].toDouble() - 25.0) < 1e-6 && std::abs(to[1].toDouble() - 10.0) < 1e-6,
                "the horizontal edge ends at the right end of its long axis");
        const auto band = horizontal.value("search").toArray();
        require(std::abs(band[2].toDouble() - 30.0) < 1e-6 && std::abs(band[3].toDouble() - 5.0) < 1e-6,
                "the search band follows the rectangle");
        // A tall rectangle is a vertical edge, so the written direction is the perpendicular one.
        const auto vertical = segments[1].toObject();
        require(std::abs(vertical.value("search").toArray()[4].toDouble() - 90.0) < 1e-6,
                "a tall rectangle becomes an edge along its long axis");

        TrainingSession reloadedEdges;
        reloadedEdges.begin({2, TrainingKind::StraightEdges, std::filesystem::path(edgesAsset.toStdWString())});
        require(reloadedEdges.records().size() == 1 && reloadedEdges.records().front().regions.size() == 2,
                "reload reads both reference edges");
        require(reloadedEdges.savable(), "reloaded edges are applicable");
        const auto firstEdge = RegionView(reloadedEdges.records().front().regions.front()).rectangle();
        require(std::abs(firstEdge.center().x - 10.0) < 1e-6 && std::abs(firstEdge.center().y - 10.0) < 1e-6
                    && std::abs(firstEdge.width() - 30.0) < 1e-6,
                "reload restores the edge geometry");

        // Replacing the reference picture keeps the drawn edges only while the geometry is the same.
        edges.setRegions(0, {makeRectangle(10 - 15, 10 - 2.5, 30, 5, 0),
                             makeRectangle(20 - 2.5, 20 - 15, 5, 30, 0)});
        QImage wider(24, 16, QImage::Format_RGB888);
        wider.fill(QColor(10, 20, 30));
        require(edges.addImage(wider).has_value(), "a new reference picture replaces the old one");
        require(edges.records().size() == 1 && edges.records().front().regions.empty(),
                "a differently sized picture drops edges that no longer describe it");
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "Training assets round-trip colour records and straight-edge segments\n";
    return 0;
}
