#include "ui/AppController_p.h"

#include <algorithm>

namespace visionrt {

QVariantList AppController::routeWires(const QVariantList &geometry)
{
    const auto *flow = impl_->currentFlow();
    if (!flow || geometry.size() != static_cast<qsizetype>(flow->nodes.size()))
        return {};
    try
    {
        std::vector<NodeBounds> nodes;
        for (const auto &entry : geometry)
        {
            const auto value = entry.toMap();
            const auto id = workbench::nodeId(value.value("id").toString());
            if (std::none_of(flow->nodes.begin(), flow->nodes.end(),
                             [id](const auto &node) { return node.id == id; }))
                throw ContractError("Wire geometry does not belong to the current flow");
            nodes.push_back({id, value.value("rectangle").toRectF()});
        }
        QVariantList result;
        std::set<std::string> currentErrors;
        for (const auto &wire : impl_->wireRouter.route(nodes, portRelations(*flow).active))
        {
            QVariantList points;
            for (const auto &point : wire.points)
                points.push_back(point);
            QVariantList ports;
            for (const auto &relation : wire.ports)
                ports.push_back(QVariantMap{{"output", workbench::q(relation.source.port)},
                                             {"input", workbench::q(relation.target.port)}});
            if (!wire.error.empty())
            {
                const auto key = wire.error + ":" + std::to_string(wire.source) + ":" + std::to_string(wire.target);
                currentErrors.insert(key);
                if (!impl_->routingErrors.contains(key))
                    impl_->report(workbench::q(wire.error));
            }
            result.push_back(QVariantMap{{"source", QString::number(wire.source)},
                                         {"target", QString::number(wire.target)},
                                         {"count", static_cast<int>(wire.ports.size())},
                                         {"ports", ports}, {"points", points},
                                         {"error", workbench::q(wire.error)}});
        }
        impl_->routingErrors = std::move(currentErrors);
        return result;
    }
    catch (const std::exception &error)
    {
        impl_->report(error, QStringLiteral("连线路径计算失败"));
        return {};
    }
}

}
