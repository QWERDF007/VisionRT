#include "ui/WireRouter.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>

int main()
{
    using namespace visionrt;
    constexpr NodeId sourceId = 9'007'199'254'740'993ULL;
    WireRouter router;
    std::vector<NodeBounds> nodes{{sourceId, QRectF(0, 0, 156, 52)},
                                   {2, QRectF(0, 160, 156, 52)}, {3, QRectF(0, 320, 156, 52)}};
    std::vector<PortRelation> relations{{{sourceId, "images"}, {2, "images"}},
                                        {{sourceId, "images"}, {3, "images"}},
                                        {{sourceId, "paths"}, {3, "paths"}}};
    const auto verify = [&](const std::vector<RoutedWire> &routes)
    {
        for (const auto &wire : routes)
        {
            const auto source = std::ranges::find(nodes, wire.source, &NodeBounds::id);
            const auto target = std::ranges::find(nodes, wire.target, &NodeBounds::id);
            assert(source != nodes.end() && target != nodes.end());
            assert(wire.points.size() >= 2);
            const auto sourcePin = QPointF(source->rectangle.center().x(), source->rectangle.bottom());
            const auto targetPin = QPointF(target->rectangle.center().x(), target->rectangle.top());
            assert(wire.points.front() == sourcePin && wire.points.back() == targetPin);
            assert(wire.points[1].y() > sourcePin.y());
            assert(wire.points[wire.points.size() - 2].y() < targetPin.y());
            for (std::size_t index = 1; index < wire.points.size(); ++index)
            {
                const auto &start = wire.points[index - 1];
                const auto &finish = wire.points[index];
                assert(start.x() == finish.x() || start.y() == finish.y());
                for (const auto &node : nodes)
                {
                    const auto bounds = node.rectangle.adjusted(0.001, 0.001, -0.001, -0.001);
                    if (start.x() == finish.x() && start.x() > bounds.left() && start.x() < bounds.right())
                        assert(std::max(start.y(), finish.y()) <= bounds.top()
                               || std::min(start.y(), finish.y()) >= bounds.bottom());
                    if (start.y() == finish.y() && start.y() > bounds.top() && start.y() < bounds.bottom())
                        assert(std::max(start.x(), finish.x()) <= bounds.left()
                               || std::min(start.x(), finish.x()) >= bounds.right());
                }
            }
        }
    };
    auto routes = router.route(nodes, relations);
    assert(routes.size() == 2);
    assert(routes[1].source == sourceId && routes[1].target == 3 && routes[1].ports.size() == 2);
    assert(routes[1].ports[0] == relations[1] && routes[1].ports[1] == relations[2]);
    assert(routes[1].points.size() >= 4);
    verify(routes);
    const auto blockedPath = routes[1].points;
    nodes[1].rectangle.translate(300, 0);
    routes = router.route(nodes, relations);
    verify(routes);
    assert(routes[1].points != blockedPath);
    nodes[2].rectangle = QRectF(-250, -160, 240, 76);
    routes = router.route(nodes, relations);
    verify(routes);
    nodes.erase(nodes.begin() + 1);
    relations.erase(relations.begin());
    routes = router.route(nodes, relations);
    assert(routes.size() == 1 && routes.front().ports.size() == 2);
    verify(routes);
    assert(router.route(nodes, {}).empty());
    relations.push_back({{sourceId, "value"}, {999, "value"}});
    routes = router.route(nodes, relations);
    assert(routes.size() == 2);
    const auto broken = std::find_if(routes.begin(), routes.end(), [](const auto &wire) { return wire.target == 999; });
    const auto valid  = std::find_if(routes.begin(), routes.end(), [](const auto &wire) { return wire.target == 3; });
    assert(broken != routes.end() && !broken->error.empty());
    assert(valid != routes.end() && valid->error.empty() && valid->points.size() >= 2);
    nodes[0].rectangle.setX(std::numeric_limits<double>::quiet_NaN());
    routes = router.route(nodes, relations);
    assert(std::any_of(routes.begin(), routes.end(), [](const auto &wire) { return !wire.error.empty(); }));
    std::cout << "[Wire Router Contract Passed] Per-line errors preserve valid routes; geometry failures stay diagnosable.\n";
}
