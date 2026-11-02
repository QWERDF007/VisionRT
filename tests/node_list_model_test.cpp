// Contract for the node list model: stable id roles, row notifications for insert/remove/move and
// dataChanged limited to the roles that actually changed. Qt's own QAbstractItemModelTester checks
// the model protocol; this file only asserts the domain behaviour.
#include "ui/NodeListModel.h"

#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include <iostream>
#include <stdexcept>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

NodeListModel::Row row(QString id, QString title, double x, QString state = QStringLiteral("idle"))
{
    NodeListModel::Row value;
    value.id       = std::move(id);
    value.title    = std::move(title);
    value.x        = x;
    value.state    = std::move(state);
    value.version  = QStringLiteral("1.0");
    value.category = QStringLiteral("测试");
    return value;
}
} // namespace

int main(int argc, char **argv)
try
{
    QCoreApplication application(argc, argv);
    NodeListModel  model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);

    model.reset({row("1", "读图", 0), row("2", "裁剪", 2)});
    require(model.rowCount() == 2, "reset did not publish both rows");
    require(model.rowOf("2") == 1, "id index lost a row after reset");
    require(model.data(model.index(1), NodeListModel::TitleRole).toString() == QStringLiteral("裁剪"),
            "title role returned another row");

    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    require(!model.sync({row("1", "读图", 0), row("2", "裁剪", 2), row("3", "检测", 4)}),
            "adding a row reported a reorder");
    require(inserted.count() == 1 && model.rowCount() == 3, "insert did not use a row notification");
    require(model.rowOf("3") == 2, "inserted row is not addressable by id");

    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    model.sync({row("1", "读图", 0), row("3", "检测", 4)});
    require(removed.count() == 1 && model.rowCount() == 2, "remove did not use a row notification");
    require(model.rowOf("2") == -1, "removed row is still addressable");

    QSignalSpy moved(&model, &QAbstractItemModel::rowsMoved);
    model.sync({row("3", "检测", 4), row("1", "读图", 0)});
    require(moved.count() == 1 && model.rowOf("3") == 0, "reorder did not move the row");

    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    require(!model.update(row("3", "检测", 4)), "an identical row reported a change");
    require(changed.isEmpty(), "an identical row emitted dataChanged");
    require(model.update(row("3", "检测", 4, QStringLiteral("running"))), "state change was ignored");
    require(changed.count() == 1, "state change did not emit exactly one dataChanged");
    const auto roles = changed.first().at(2).value<QList<int>>();
    require(roles == QList<int>{NodeListModel::StateRole}, "state change reported unrelated roles");

    // The legacy map view stays consistent with the roles for existing consumers.
    const auto record = model.rowMap("3");
    require(record.value("id").toString() == QStringLiteral("3")
                && record.value("state").toString() == QStringLiteral("running")
                && record.value("x").toDouble() == 4,
            "row map disagrees with the model roles");
    require(model.snapshot().size() == 2, "snapshot is incomplete");

    model.reset({});
    require(model.rowCount() == 0 && model.rowOf("3") == -1, "reset did not clear the model");

    std::cout << "NODE_MODEL_CONTRACT_PASS\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
