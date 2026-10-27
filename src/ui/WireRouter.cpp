#include "ui/WireRouter.h"

#include <libavoid/libavoid.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <tuple>

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
    std::map<NodeId, QRectF>            rectangles;
    for (const auto &node : nodes)
    {
        const auto &bounds = node.rectangle;
        if (!std::isfinite(bounds.left()) || !std::isfinite(bounds.top()) || !std::isfinite(bounds.right())
            || !std::isfinite(bounds.bottom()) || bounds.isEmpty() || shapes.contains(node.id))
            continue;
        rectangles.emplace(node.id, bounds);
        Avoid::Rectangle rectangle(Avoid::Point(bounds.left(), bounds.top()),
                                   Avoid::Point(bounds.right(), bounds.bottom()));
        auto *shape  = new Avoid::ShapeRef(&router, rectangle);
        auto *input  = new Avoid::ShapeConnectionPin(shape, 1, 0.5, 0, true, 0, Avoid::ConnDirUp);
        auto *output = new Avoid::ShapeConnectionPin(shape, 2, 0.5, 1, true, 0, Avoid::ConnDirDown);
        input->setExclusive(false);
        output->setExclusive(false);
        shapes.emplace(node.id, shape);
    }
    const auto fallback = [&](NodeId source, NodeId target)
    {
        std::vector<QPointF> points;
        const auto          sourceRectangle = rectangles.find(source);
        const auto          targetRectangle = rectangles.find(target);
        if (sourceRectangle != rectangles.end() && targetRectangle != rectangles.end())
            points = {QPointF(sourceRectangle->second.center().x(), sourceRectangle->second.bottom()),
                      QPointF(targetRectangle->second.center().x(), targetRectangle->second.top())};
        return points;
    };
    std::map<DependencyEdge, std::vector<PortRelation>> groups;
    std::vector<RoutedWire>                             failed;
    for (const auto &relation : relations)
    {
        if (!shapes.contains(relation.source.node) || !shapes.contains(relation.target.node))
        {
            const DependencyEdge edge{relation.source.node, relation.target.node};
            const auto found = std::find_if(failed.begin(), failed.end(),
                                            [&](const auto &wire)
                                            { return wire.source == edge.first && wire.target == edge.second; });
            if (found == failed.end())
                failed.push_back({edge.first, edge.second, {relation}, fallback(edge.first, edge.second),
                                  "连线端点缺少可用节点几何，已保留简化连接"});
            else
                found->ports.push_back(relation);
            continue;
        }
        groups[{relation.source.node, relation.target.node}].push_back(relation);
    }
    std::vector<std::pair<RoutedWire, Avoid::ConnRef *>> connections;
    for (const auto &[edge, ports] : groups)
    {
        auto *connection = new Avoid::ConnRef(&router, Avoid::ConnEnd(shapes.at(edge.first), 2),
                                              Avoid::ConnEnd(shapes.at(edge.second), 1));
        connections.push_back({{edge.first, edge.second, ports, {}, {}}, connection});
    }
    router.processTransaction();
    std::vector<RoutedWire> routes;
    for (auto &[wire, connection] : connections)
    {
        const auto &path = connection->displayRoute().ps;
        if (path.size() < 2)
        {
            wire.points = fallback(wire.source, wire.target);
            wire.error  = "无法计算避障路径，已保留简化连接";
        }
        else
        {
            for (const auto &point : path)
                wire.points.emplace_back(point.x, point.y);
        }
        routes.push_back(std::move(wire));
    }
    routes.insert(routes.end(), std::make_move_iterator(failed.begin()), std::make_move_iterator(failed.end()));
    std::sort(routes.begin(), routes.end(), [](const auto &left, const auto &right)
    { return std::tie(left.source, left.target) < std::tie(right.source, right.target); });
    impl_->nodes.assign(nodes.begin(), nodes.end());
    impl_->relations.assign(relations.begin(), relations.end());
    impl_->routes = std::move(routes);
    return impl_->routes;
}

}
