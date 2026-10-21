#include "ui/WireRouter.h"

#include <libavoid/libavoid.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace visionrt {

struct WireRouter::Impl
{
    std::vector<NodeBounds>   nodes;
    std::vector<PortRelation> relations;
    std::vector<RoutedWire>   routes;
};

WireRouter::WireRouter() : impl_(std::make_unique<Impl>())
{
}
WireRouter::~WireRouter() = default;

const std::vector<RoutedWire> &WireRouter::route(std::span<const NodeBounds> nodes,
                                               std::span<const PortRelation> relations)
{
    if (std::ranges::equal(nodes, impl_->nodes) && std::ranges::equal(relations, impl_->relations))
        return impl_->routes;
    Avoid::Router router(Avoid::OrthogonalRouting);
    router.setRoutingParameter(Avoid::shapeBufferDistance, 8);
    router.setRoutingOption(Avoid::nudgeSharedPathsWithCommonEndPoint, true);
    std::map<NodeId, Avoid::ShapeRef *> shapes;
    for (const auto &node : nodes)
    {
        const auto &bounds = node.rectangle;
        if (!std::isfinite(bounds.left()) || !std::isfinite(bounds.top()) || !std::isfinite(bounds.right())
            || !std::isfinite(bounds.bottom()) || bounds.isEmpty() || shapes.contains(node.id))
            throw ContractError("Invalid node geometry for wire routing");
        Avoid::Rectangle rectangle(Avoid::Point(bounds.left(), bounds.top()),
                                   Avoid::Point(bounds.right(), bounds.bottom()));
        auto *shape  = new Avoid::ShapeRef(&router, rectangle);
        auto *input  = new Avoid::ShapeConnectionPin(shape, 1, 0.5, 0, true, 0, Avoid::ConnDirUp);
        auto *output = new Avoid::ShapeConnectionPin(shape, 2, 0.5, 1, true, 0, Avoid::ConnDirDown);
        input->setExclusive(false);
        output->setExclusive(false);
        shapes.emplace(node.id, shape);
    }
    std::map<DependencyEdge, std::vector<PortRelation>> groups;
    for (const auto &relation : relations)
    {
        if (!shapes.contains(relation.source.node) || !shapes.contains(relation.target.node))
            throw ContractError("Wire endpoint has no node geometry");
        groups[{relation.source.node, relation.target.node}].push_back(relation);
    }
    std::vector<std::pair<RoutedWire, Avoid::ConnRef *>> connections;
    for (const auto &[edge, ports] : groups)
    {
        auto *connection = new Avoid::ConnRef(&router, Avoid::ConnEnd(shapes.at(edge.first), 2),
                                              Avoid::ConnEnd(shapes.at(edge.second), 1));
        connections.push_back({{edge.first, edge.second, ports, {}}, connection});
    }
    router.processTransaction();
    std::vector<RoutedWire> routes;
    for (auto &[wire, connection] : connections)
    {
        const auto &path = connection->displayRoute().ps;
        if (path.size() < 2)
            throw ContractError("No obstacle-avoiding wire route is available");
        for (const auto &point : path)
            wire.points.emplace_back(point.x, point.y);
        routes.push_back(std::move(wire));
    }
    impl_->nodes.assign(nodes.begin(), nodes.end());
    impl_->relations.assign(relations.begin(), relations.end());
    impl_->routes = std::move(routes);
    return impl_->routes;
}

}
