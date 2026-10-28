#include "core/Tool.h"

#include <iostream>
#include <stdexcept>

int main()
{
    using namespace visionrt;
    try
    {
        const auto unnamed = PathResolver{}.tryResolve("images/test.png");
        if (unnamed || unnamed.issue().kind != PortIssueKind::PathUnresolved)
            throw std::runtime_error("unnamed relative path accepted");
        if (unnamed.issue().describe().find("images/test.png") == std::string::npos)
            throw std::runtime_error("the issue must name the path");
        const auto absolute = std::filesystem::path("D:/images/test.png");
        const auto direct   = PathResolver{}.tryResolve("D:/images/test.png");
        if (!direct || direct.value() != absolute)
            throw std::runtime_error("absolute path changed");
        const auto relative = PathResolver{"D:/projects/example"}.tryResolve("../images/test.png");
        if (!relative || relative.value() != std::filesystem::path("D:/projects/images/test.png"))
            throw std::runtime_error("relative path resolved against wrong directory");
        std::cout << "path contract passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
