#pragma once

#include "core/ProjectEditor.h"

#include <QPointF>
#include <QRectF>
#include <memory>
#include <span>
#include <vector>

namespace visionrt {

struct NodeBounds
{
    NodeId id;
    QRectF rectangle;
    bool   operator==(const NodeBounds &) const = default;
};

struct RoutedWire
{
    NodeId                    source;
    NodeId                    target;
    std::vector<PortRelation> ports;
    std::vector<QPointF>       points;
};

class WireRouter
{
public:
    WireRouter();
    ~WireRouter();
    const std::vector<RoutedWire> &route(std::span<const NodeBounds> nodes,
                                        std::span<const PortRelation> relations);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
