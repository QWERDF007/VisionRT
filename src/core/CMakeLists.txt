visionrt_add_module(core SOURCES
    ToolDefinition.cpp
    ToolValueValidation.cpp
    InputNormalizer.cpp
    DefinitionJson.cpp
    ModuleIndex.cpp
    ToolIntegrations.cpp
    ToolPackage.cpp
    ValueJson.cpp
    ValueJsonDocument.cpp
    NodeContracts.cpp
    ToolRegistry.cpp
    ProjectEditor.cpp
    ProjectValues.cpp
    ProjectStorage.cpp
    ExampleProject.cpp
    CustomToolStore.cpp
    Error.cpp
    PUBLIC_LIBRARIES visionrt_common Qt6::Core)
