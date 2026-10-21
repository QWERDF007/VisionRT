#include <libavoid/libavoid.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>

int main()
{
    const auto require = [](bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };
    Avoid::Router router(Avoid::OrthogonalRouting);
    router.setRoutingParameter(Avoid::shapeBufferDistance, 8);
    Avoid::Rectangle sourceBounds(Avoid::Point(0, 0), Avoid::Point(156, 52));
    Avoid::Rectangle middleBounds(Avoid::Point(0, 160), Avoid::Point(156, 212));
    Avoid::Rectangle targetBounds(Avoid::Point(0, 320), Avoid::Point(156, 372));
    auto *source = new Avoid::ShapeRef(&router, sourceBounds);
    auto *middle = new Avoid::ShapeRef(&router, middleBounds);
    auto *target = new Avoid::ShapeRef(&router, targetBounds);
    for (auto *shape : {source, middle, target})
    {
        auto *input = new Avoid::ShapeConnectionPin(shape, 1, 0.5, 0, true, 0, Avoid::ConnDirUp);
        auto *output = new Avoid::ShapeConnectionPin(shape, 2, 0.5, 1, true, 0, Avoid::ConnDirDown);
        input->setExclusive(false);
        output->setExclusive(false);
    }
    new Avoid::ConnRef(&router, Avoid::ConnEnd(source, 2), Avoid::ConnEnd(middle, 1));
    auto *bypass = new Avoid::ConnRef(&router, Avoid::ConnEnd(source, 2), Avoid::ConnEnd(target, 1));
    router.processTransaction();
    const auto &path = bypass->displayRoute().ps;
    require(path.size() >= 4, "route does not bypass the obstacle");
    require(path.front().x == 78 && path.front().y == 52, "route lost the source output pin");
    require(path.back().x == 78 && path.back().y == 320, "route lost the target input pin");
    require(path[1].y > path.front().y && path[path.size() - 2].y < path.back().y,
            "route violates the output or input direction");
    for (std::size_t index = 1; index < path.size(); ++index)
    {
        const auto &start = path[index - 1];
        const auto &finish = path[index];
        require(start.x == finish.x || start.y == finish.y, "route has a diagonal segment");
        if (start.x == finish.x && start.x > 0 && start.x < 156)
            require(std::max(start.y, finish.y) <= 160 || std::min(start.y, finish.y) >= 212,
                    "vertical segment crosses the middle node");
        if (start.y == finish.y && start.y > 160 && start.y < 212)
            require(std::max(start.x, finish.x) <= 0 || std::min(start.x, finish.x) >= 156,
                    "horizontal segment crosses the middle node");
    }
    router.moveShape(middle, 300, 0);
    router.processTransaction();
    require(bypass->displayRoute().ps.front().y == 52 && bypass->displayRoute().ps.back().y == 320,
            "moving an obstacle disconnects the route");
    std::cout << "[libavoid Contract Passed] Orthogonal pins bypass the middle node and survive obstacle movement.\n";
}
