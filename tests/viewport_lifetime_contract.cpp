#include "ui/ImageViewport.h"

#include <QGuiApplication>
#include <QPointer>
#include <QQuickWindow>
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
}

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    int             destroyed = 0;
    for (int iteration = 0; iteration < 16; ++iteration)
    {
        QQuickWindow            first;
        QQuickWindow            second;
        auto                   *viewport = new ImageViewport(first.contentItem());
        QPointer<ImageViewport> tracked(viewport);
        QSignalSpy              removed(viewport, &QObject::destroyed);
        viewport->setParentItem(second.contentItem());
        require(viewport->window() == &second, "viewport did not move to the second window");
        delete viewport;
        require(tracked.isNull() && removed.count() == 1 && second.contentItem()->childItems().isEmpty(),
                "direct viewport destruction left a live item or duplicate destruction");
        ++destroyed;
        auto window = std::make_unique<QQuickWindow>();
        viewport    = new ImageViewport(window->contentItem());
        tracked     = viewport;
        QSignalSpy removedWithParent(viewport, &QObject::destroyed);
        window.reset();
        require(tracked.isNull() && removedWithParent.count() == 1,
                "parent window destruction left a live viewport or duplicate destruction");
        ++destroyed;
    }
    std::cout << "VIEWPORT_LIFETIME_PASS destroyed=" << destroyed << " reparented=16 parent_owned=16" << std::endl;
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
