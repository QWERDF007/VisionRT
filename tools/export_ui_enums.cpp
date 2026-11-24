#include "EnumExport.h"

#define VRT_ENUM_FILE "visionrt/contracts/ProtocolEnums.def"
#define VRT_ENUM_HEADER "visionrt/contracts/ProtocolEnums.h"
#define VRT_ENUM_CATALOG protocol
#include "EnumCatalog.h"
#define VRT_ENUM_FILE "visionrt/contracts/TypeSpec.def"
#define VRT_ENUM_HEADER "visionrt/contracts/TypeSpec.h"
#define VRT_ENUM_CATALOG types
#include "EnumCatalog.h"
#define VRT_ENUM_FILE "visionrt/contracts/ROI.def"
#define VRT_ENUM_HEADER "visionrt/contracts/ROI.h"
#define VRT_ENUM_CATALOG roi
#include "EnumCatalog.h"
#define VRT_ENUM_FILE "ui/UiState.def"
#define VRT_ENUM_HEADER "ui/UiState.h"
#define VRT_ENUM_CATALOG ui
#include "EnumCatalog.h"
#define VRT_ENUM_FILE "runtime/RuntimeEngine.def"
#define VRT_ENUM_HEADER "runtime/RuntimeEngine.h"
#define VRT_ENUM_CATALOG runtime
#include "EnumCatalog.h"
#define VRT_ENUM_FILE "runtime/Snapshot.def"
#define VRT_ENUM_HEADER "runtime/Snapshot.h"
#define VRT_ENUM_CATALOG snapshot
#include "EnumCatalog.h"
#define VRT_ENUM_FILE "python/PythonEnvironment.def"
#define VRT_ENUM_HEADER "python/PythonEnvironment.h"
#define VRT_ENUM_CATALOG python
#include "EnumCatalog.h"
#define VRT_ENUM_FILE "log/LogLevel.def"
#define VRT_ENUM_HEADER "log/LogLevel.h"
#define VRT_ENUM_CATALOG loglevel
#include "EnumCatalog.h"

#ifdef _WIN32
int wmain(int argc, wchar_t **argv)
#else
int main(int argc, char **argv)
#endif
{
    return visionrt::codegen::run(argc, argv, {protocol::enums, types::enums, roi::enums,
        ui::enums, runtime::enums, snapshot::enums, python::enums, loglevel::enums});
}
