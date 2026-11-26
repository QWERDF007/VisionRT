#include "ui/ThumbnailImageProvider.h"
#include "ui/ThumbnailStore.h"
#include "ui/TrainingRecordModel.h"

#include <QGuiApplication>
#include <QQmlEngine>
#include <QSignalSpy>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main(int argc, char **argv)
try
{
    QGuiApplication app(argc, argv);

    // 1. TrainingRecordModel incremental sync
    TrainingRecordModel model;
    QSignalSpy          resets(&model, &QAbstractItemModel::modelReset);
    QSignalSpy          inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy          removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy          changed(&model, &QAbstractItemModel::dataChanged);

    TrainingRecordModel::Row r1{"1", 1, "image://visionrt_training/1/1/1?v=1", 1, true, {QVariantMap{{"id", 1}, {"value", "red"}}}};
    TrainingRecordModel::Row r2{"2", 2, "image://visionrt_training/1/1/2?v=2", 0, false, {QVariantMap{{"id", 1}, {"value", "blue"}}}};
    model.sync({r1, r2});
    require(resets.isEmpty() && inserted.count() == 2 && model.rowCount() == 2,
            "initial TrainingRecordModel sync did not insert incrementally");

    // Selection switch -> only SelectedRole changed on rows 0 and 1, 0 resets
    changed.clear();
    r1.selected = false;
    r2.selected = true;
    model.sync({r1, r2});
    require(resets.isEmpty() && changed.count() == 2, "selection switch did not emit targeted dataChanged");
    require(changed.at(0).at(2).value<QList<int>>() == QList<int>{TrainingRecordModel::SelectedRole}
                && changed.at(1).at(2).value<QList<int>>() == QList<int>{TrainingRecordModel::SelectedRole},
            "selection switch emitted unexpected roles that would recreate editors");

    // Field edit -> only FieldsRole changed on row 1
    changed.clear();
    r2.fields = {QVariantMap{{"id", 1}, {"value", "green"}}};
    model.sync({r1, r2});
    require(resets.isEmpty() && changed.count() == 1
                && changed.first().at(0).toModelIndex().row() == 1
                && changed.first().at(2).value<QList<int>>() == QList<int>{TrainingRecordModel::FieldsRole},
            "field edit did not emit FieldsRole-only dataChanged");

    // Remove first record -> rowsRemoved, 0 resets
    removed.clear();
    model.sync({r2});
    require(resets.isEmpty() && removed.count() == 1 && model.rowCount() == 1,
            "record removal did not use incremental rowsRemoved");

    // 2. ThumbnailStore & ThumbnailImageProvider
    auto store = std::make_shared<ThumbnailStore>();
    QImage large(640, 480, QImage::Format_RGB888);
    large.fill(QColor(20, 120, 220));
    const auto rev = store->set(QStringLiteral("training/1/1/1"), large);
    require(rev > 0, "ThumbnailStore did not advance revision");

    ThumbnailImageProvider provider(store, QStringLiteral("training/"));
    QSize                  returnedSize;
    const QImage           fetched = provider.requestImage(QStringLiteral("1/1/1?v=%1").arg(rev), &returnedSize, {});
    require(!fetched.isNull() && fetched.width() <= ThumbnailStore::kMaxEdge && fetched.height() <= ThumbnailStore::kMaxEdge,
            "ThumbnailImageProvider did not return downscaled thumbnail");
    require(fetched.pixelColor(fetched.width() / 2, fetched.height() / 2) == QColor(20, 120, 220),
            "ThumbnailImageProvider corrupted pixel colour");

    // Missing key returns 1x1 transparent fallback without null image
    QSize        missingSize;
    const QImage missing = provider.requestImage(QStringLiteral("9/9/9?v=1"), &missingSize, {});
    require(!missing.isNull() && missing.size() == QSize(1, 1) && missing.pixelColor(0, 0).alpha() == 0,
            "missing thumbnail did not return transparent fallback");

    // Safe ownership across QQmlEngine lifetime even after caller drops its store reference
    {
        QQmlEngine engine;
        engine.addImageProvider(QStringLiteral("visionrt_training"),
                                new ThumbnailImageProvider(store, QStringLiteral("training/")));
        store.reset();
        auto *registered = static_cast<ThumbnailImageProvider *>(engine.imageProvider(QStringLiteral("visionrt_training")));
        require(registered != nullptr, "provider not registered on QQmlEngine");
        QSize s;
        require(!registered->requestImage(QStringLiteral("1/1/1?v=1"), &s, {}).isNull(),
                "provider failed after caller dropped shared_ptr");
    }

    std::cout << "TRAINING_IMAGE_PROVIDER_CONTRACT_PASS\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
