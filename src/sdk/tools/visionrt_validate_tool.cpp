#include "visionrt/sdk/SchemaYaml.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: visionrt_validate_tool <path_to_tool.yaml>\n";
        return 1;
    }
    try {
        const auto definition = visionrt::parseToolDefinitionFile(argv[1]);
        std::cout << "Valid tool definition: " << definition.id << " (" << definition.displayName << ")\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Validation error: " << error.what() << "\n";
        return 2;
    }
}
