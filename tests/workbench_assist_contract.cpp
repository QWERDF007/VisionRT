// Contract for smart point selection (T-R32-06).
// The real tools package, a real circle helper and real images run through the same channels a user
// drives: hover previews a candidate, click commits ROI plus recommended parameters in one
// transaction, Ctrl+click appends, the selected image index is honoured, non-identity alignment is
// forwarded and inverse-transformed, an upstream ROI refuses the click, and editing a parameter
// makes the pending preview stale.
#include "WorkbenchFixtures.h"
#include "ui/AppController.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSignalSpy>
#include <QTest>
#include <iostream>
#include <stdexcept>

using namespace visionrt;

namespace {
void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

QVariantMap rowOf(const QVariantList &rows, const QString &id)
{
    for (const auto &value : rows)
        if (value.toMap().value("id").toString() == id)
            return value.toMap();
    return {};
}

double numericValue(const QString &text)
{
    bool         ok    = false;
    const double value = text.toDouble(&ok);
    if (!ok)
        throw std::runtime_error("assist parameter is not numeric: " + text.toStdString());
    return value;
}

QImage dots(const QList<QPointF> &centers, const QList<double> &radii)
{
    QImage image(320, 240, QImage::Format_Grayscale8);
    image.fill(20);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(220, 220, 220));
    for (int index = 0; index < centers.size(); ++index)
        painter.drawEllipse(centers[index], radii[index], radii[index]);
    return image;
}

// ROI literal of one node, as stored in the saved project: groups[imageIndex][recordIndex][values].
QJsonArray roiGroups(const QString &project, const QString &node)
{
    QFile file(project);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("saved assist project is unreadable");
    const auto document = QJsonDocument::fromJson(file.readAll());
    for (const auto &flow : document.object().value("flows").toArray())
        for (const auto &candidate : flow.toObject().value("nodes").toArray())
        {
            const auto object = candidate.toObject();
            if (object.value("node_seq").toString() != node)
                continue;
            return object.value("inputs").toObject().value("rois").toObject().value("literal").toArray();
        }
    throw std::runtime_error("assist node is missing from the saved project");
}

QPointF recordCenter(const QJsonArray &record)
{
    return {record.at(1).toDouble(), record.at(2).toDouble()};
}
} // namespace

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    testing::WorkbenchInstallation installation;
    const auto                     root = installation.directory();

    // Image 0 carries the replace and append circles, image 1 proves the selected index is honoured.
    const auto images = root + "/assist-images";
    require(QDir().mkpath(images), "assist image directory could not be created");
    require(dots({{160.0, 120.0}, {60.0, 60.0}}, {40.0, 30.0}).save(images + "/0.png"),
            "assist first image could not be written");
    require(dots({{200.0, 80.0}}, {32.0}).save(images + "/1.png"), "assist second image could not be written");
    QImage stripes(320, 240, QImage::Format_Grayscale8);
    stripes.fill(20);
    {
        QPainter painter(&stripes);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(220, 220, 220));
        for (int x = 40; x < 280; x += 24)
            painter.drawRect(x, 20, 10, 200);
    }
    require(stripes.save(images + "/2.png"), "assist stripe image could not be written");

    AppController app(2, root);
    const auto    settle = [&]
    { require(QTest::qWaitFor([&] { return !app.busy(); }, 20000), "assist operation did not settle"); };
    // A management synchronize starts on the runtime thread shortly after a project edit. Let it
    // finish before the next pointer interaction, exactly as a user's next mouse move would.
    const auto stable = [&]
    {
        settle();
        QTest::qWait(200);
        settle();
    };
    require(QTest::qWaitFor([&] { return !app.busy(); }, 10000), "assist catalog did not become ready");
    app.rescanTools();
    settle();

    app.addNode("visionrt.image.read", 0, 0);
    settle();
    const auto reader = app.selectedNode();
    if (reader.isEmpty())
        throw std::runtime_error("assist reader node is missing: " + app.diagnostic().toStdString());
    require(app.setParameter("count", false, "3"), "assist count was rejected");
    require(app.setParameterPath("path", false, QUrl::fromLocalFile(images)), "assist image path was rejected");
    settle();

    app.addNode("visiontools.measure.circle", 0, 4);
    settle();
    const auto circle = app.selectedNode();
    require(!circle.isEmpty(), "assist circle node is missing");
    require(app.bindInput(app.portContext("images"), reader, "images"), "circle image input could not be bound");
    settle();
    if (!app.save(root + "/assist.json"))
        throw std::runtime_error("assist project could not be saved: " + app.diagnostic().toStdString());
    stable();
    app.runOnce();
    settle();
    require(!app.dirty(), "running the assist baseline must not dirty the saved project");
    require(app.imageCount() == 3, "assist batch did not expose all images");

    require(QTest::qWaitFor([&] { return app.assistActive(); }, 5000),
            "smart point selection did not become available after the image was cached: "
                + app.assistHint().toStdString());
    require(app.assistHint().contains(QStringLiteral("单击")), "assist hint is missing its click guidance");

    // Hover only previews: candidate visible, project untouched.
    const auto parametersBefore = app.parameters();
    require(app.assistHover(160.0, 120.0), "hover over the circle was rejected: " + app.assistHint().toStdString());
    require(QTest::qWaitFor([&] { return !app.assistPreview().isEmpty(); }, 5000),
            "hover produced no candidate: " + app.assistHint().toStdString());
    require(!app.dirty(), "a hover preview must not dirty the project");
    const auto preview = app.assistPreview().front().toList();
    require(preview.size() == 4 && static_cast<int>(preview.at(0).toDouble()) == 3,
            "assist candidate must be a circle record");
    require(std::abs(preview.at(1).toDouble() - 160.0) <= 1.5 && std::abs(preview.at(2).toDouble() - 120.0) <= 1.5
                && std::abs(preview.at(3).toDouble() - 40.0) <= 1.5,
            "hover candidate geometry is not the drawn circle");

    // Click learns and commits the proposal in one transaction.
    QSignalSpy projectChanges(&app, &AppController::projectChanged);
    require(app.assistClick(160.0, 120.0, false), "circle click was rejected");
    require(QTest::qWaitFor([&] { return app.dirty(); }, 10000),
            "the click did not commit a ROI: " + app.assistHint().toStdString());
    stable();
    require(projectChanges.count() == 1, "a proposal must raise exactly one project transaction");
    auto       rows = app.parameters();
    const auto rois = rowOf(rows, "rois");
    require(rois.value("hasRois").toBool() && rois.value("roiCount").toInt() == 1,
            "the committed ROI was not written back");
    const auto polarity = rowOf(rows, "polarity").value("valueText").toString();
    require(polarity == "dark_to_light" || polarity == "light_to_dark",
            "the recommended polarity was not applied");
    const double halfWidth = numericValue(rowOf(rows, "search_half_width").value("literal").toString());
    require(std::abs(halfWidth - 8.0) > 1e-9, "the recommended search half width was not applied");
    require(rowOf(rows, "num_calipers").value("literal").toString()
                == rowOf(parametersBefore, "num_calipers").value("literal").toString(),
            "an unmapped parameter changed during the proposal");
    require(QTest::qWaitFor(
                [&]
                {
                    for (const auto &value : app.nodes())
                        if (value.toMap().value("id").toString() == circle)
                            return value.toMap().value("stale").toBool();
                    return false;
                },
                5000),
            "a committed proposal must mark the previous formal result as stale");

    // Ctrl+click at another circle appends instead of replacing, and keeps the global parameters.
    const auto beforeAppend = app.parameters();
    stable();
    require(app.assistHover(60.0, 60.0), "hover over the second circle was rejected");
    require(QTest::qWaitFor([&] { return !app.assistPreview().isEmpty(); }, 5000),
            "hover produced no candidate for the second circle: " + app.assistHint().toStdString());
    require(app.assistClick(60.0, 60.0, true), "append click was rejected: " + app.assistHint().toStdString());
    require(QTest::qWaitFor([&] { return rowOf(app.parameters(), "rois").value("roiCount").toInt() == 2; }, 10000),
            "Ctrl+click did not append the second candidate");
    stable();
    require(rowOf(app.parameters(), "search_half_width").value("literal").toString()
                == rowOf(beforeAppend, "search_half_width").value("literal").toString(),
            "Ctrl+click must keep the current global parameters");

    // The selected image index owns the proposal: image 1 keeps image 0's group untouched.
    app.setImageIndex(1);
    stable();
    require(app.assistHover(200.0, 80.0), "hover over the second image was rejected");
    require(QTest::qWaitFor([&] { return !app.assistPreview().isEmpty(); }, 5000),
            "hover produced no candidate on the second image: " + app.assistHint().toStdString());
    require(app.assistClick(200.0, 80.0, false), "second image click was rejected: " + app.assistHint().toStdString());
    require(QTest::qWaitFor([&] { return rowOf(app.parameters(), "rois").value("roiCount").toInt() == 3; }, 10000),
            "the second image proposal was not committed");
    stable();
    require(app.save(root + "/assist.json"), "assist project could not be saved after the batch commit");
    settle();
    const auto batchGroups = roiGroups(root + "/assist.json", circle);
    require(batchGroups.size() == 2 && batchGroups.at(0).toArray().size() == 2
                && batchGroups.at(1).toArray().size() == 1,
            "the proposal was not written to the selected image group");
    require(std::abs(recordCenter(batchGroups.at(1).toArray().at(0).toArray()).x() - 200.0) <= 1.5
                && std::abs(recordCenter(batchGroups.at(1).toArray().at(0).toArray()).y() - 80.0) <= 1.5,
            "the second image candidate landed on the wrong geometry");

    // A non-identity alignment is forwarded to the helper and its inverse is what gets committed:
    // the preview stays in current-image coordinates, the literal stays in reference coordinates.
    app.setImageIndex(0);
    require(app.clearRoi("rois"), "assist ROI could not be cleared");
    require(app.setParameter("alignment", false, "[[[1,0,10],[0,1,5],[0,0,1]]]", true),
            "assist alignment literal was rejected");
    stable();
    require(app.assistHover(160.0, 120.0), "hover with alignment was rejected: " + app.assistHint().toStdString());
    require(QTest::qWaitFor([&] { return !app.assistPreview().isEmpty(); }, 5000),
            "hover with alignment produced no candidate: " + app.assistHint().toStdString());
    const auto alignedPreview = app.assistPreview().front().toList();
    require(std::abs(alignedPreview.at(1).toDouble() - 160.0) <= 1.5
                && std::abs(alignedPreview.at(2).toDouble() - 120.0) <= 1.5,
            "the preview must stay in current-image coordinates");
    require(app.assistClick(160.0, 120.0, false), "aligned click was rejected: " + app.assistHint().toStdString());
    require(QTest::qWaitFor([&] { return app.dirty(); }, 10000), "the aligned click was not committed");
    stable();
    require(app.save(root + "/assist.json"), "aligned assist project could not be saved");
    const auto alignedGroups = roiGroups(root + "/assist.json", circle);
    require(alignedGroups.at(0).toArray().size() == 1, "the aligned proposal replaced the wrong group");
    const auto aligned = recordCenter(alignedGroups.at(0).toArray().at(0).toArray());
    require(std::abs(aligned.x() - 150.0) <= 1.5 && std::abs(aligned.y() - 115.0) <= 1.5,
            "the proposal was not inverse-transformed into reference coordinates");

    // Editing a parameter invalidates the preview that was learned from the old configuration.
    require(app.assistHover(160.0, 120.0), "hover for the staleness check was rejected");
    require(QTest::qWaitFor([&] { return !app.assistPreview().isEmpty(); }, 5000),
            "hover produced no candidate for the staleness check");
    require(app.setParameter("search_half_width", false, "6"), "parameter edit was rejected");
    settle();
    require(app.assistPreview().isEmpty(), "a preview learned from an older configuration must be discarded");

    // Loop is a heavy-state mode: no hover request may start while a loop owns the runtime.
    stable();
    app.toggleLoop();
    require(QTest::qWaitFor([&] { return !app.assistActive(); }, 5000),
            "smart point selection stayed available during a loop");
    require(!app.assistHover(160.0, 120.0), "a hover request was accepted during a loop");
    app.toggleLoop();
    require(QTest::qWaitFor([&] { return !app.busy(); }, 20000), "the assist loop did not stop");

    // The second helper uses the same host path: the row-spacing proposal is a rectangle plus its
    // recommended width range, committed through the same single transaction.
    app.addNode("visiontools.measure.row_spacing", 0, 12);
    settle();
    const auto rowsNode = app.selectedNode();
    require(!rowsNode.isEmpty(), "row spacing node is missing");
    require(app.bindInput(app.portContext("images"), reader, "images"), "row spacing image input could not be bound");
    settle();
    app.runNode();
    settle();
    app.setImageIndex(2);
    stable();
    require(QTest::qWaitFor([&] { return app.assistActive(); }, 5000),
            "row spacing assist did not become available: " + app.assistHint().toStdString());
    require(app.assistHover(160.0, 120.0), "row spacing hover was rejected: " + app.assistHint().toStdString());
    const bool rowPreview = QTest::qWaitFor([&] { return !app.assistPreview().isEmpty(); }, 5000);
    require(rowPreview, "row spacing hover produced no candidate: " + app.assistHint().toStdString());
    require(app.assistClick(160.0, 120.0, false), "row spacing click was rejected: " + app.assistHint().toStdString());
    require(QTest::qWaitFor([&] { return rowOf(app.parameters(), "rois").value("roiCount").toInt() == 1; }, 10000),
            "row spacing proposal was not committed: " + app.assistHint().toStdString());
    stable();
    const auto rowWidths = app.parameters();
    require(numericValue(rowOf(rowWidths, "width_max").value("literal").toString()) < 1000000.0,
            "row spacing width range was not applied");
    require(app.save(root + "/assist.json"), "row spacing project could not be saved");
    const auto rowGroups = roiGroups(root + "/assist.json", rowsNode);
    require(rowGroups.size() == 3 && rowGroups.at(2).toArray().size() == 1
                && static_cast<int>(rowGroups.at(2).toArray().at(0).toArray().at(0).toDouble()) == 2,
            "row spacing proposal did not land as a rectangle on the selected image");
    app.setImageIndex(0);
    stable();

    // An upstream ROI is read-only: the click reports it instead of silently disconnecting.
    stable();
    app.addNode("visiontools.measure.circle", 0, 8);
    settle();
    const auto downstream = app.selectedNode();
    require(!downstream.isEmpty(), "downstream circle node is missing");
    require(app.bindInput(app.portContext("images"), reader, "images"), "downstream image input could not be bound");
    settle();
    require(app.bindInput(app.portContext("rois"), circle, "circles"), "downstream upstream ROI could not be bound");
    settle();
    require(!app.assistClick(160.0, 120.0, false), "an upstream ROI click must be refused");
    require(app.assistHint().contains(QStringLiteral("上游")), "the upstream refusal must explain itself");

    app.assistCancel();
    require(app.assistPreview().isEmpty(), "cancel left a candidate visible");

    // Closing must drain the assist channel: an in-flight helper is invalidated, its owner released,
    // and closeReady is emitted exactly once.
    require(app.save(root + "/assist.json"), "assist project could not be saved before closing");
    stable();
    QSignalSpy closeReady(&app, &AppController::closeReady);
    require(app.assistHover(160.0, 120.0), "hover before close was rejected");
    app.requestClose();
    require(QTest::qWaitFor([&] { return closeReady.count() == 1; }, 20000),
            "close did not drain the assist channel");

    std::cout << "WORKBENCH_ASSIST_PASS preview=1 dirty=0 click=roi+params append=2 batch=3 rows=rectangle "
                 "alignment=inverse upstream=refused stale=1\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << "[Assist error] " << error.what() << '\n';
    return 1;
}
