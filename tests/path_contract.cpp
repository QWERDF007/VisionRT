#include "core/Tool.h"
#include <iostream>
#include <stdexcept>

int main() {
    using namespace visionrt;
    try {
        bool rejected = false;
        try { (void)PathResolver{}.resolve("images/test.png"); }
        catch (const std::runtime_error&) { rejected = true; }
        if (!rejected) throw std::runtime_error("unnamed relative path accepted");
        const auto absolute = std::filesystem::path("D:/images/test.png");
        if (PathResolver{}.resolve("D:/images/test.png") != absolute)
            throw std::runtime_error("absolute path changed");
        if (PathResolver{"D:/projects/example"}.resolve("../images/test.png") !=
            std::filesystem::path("D:/projects/images/test.png"))
            throw std::runtime_error("relative path resolved against wrong directory");
        std::cout << "path contract passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
