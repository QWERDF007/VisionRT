#pragma once

// The only symbols that cross the bridge boundary. The host resolves them from the bridge module
// by name, so nothing here may be referenced at link time by the application.
//
// The interpreter owns every Python object internally: no py::object, no interpreter state and no
// GIL crosses this interface. Definitions and statuses are plain contracts data, and a factory is
// created and destroyed through the same module that produced it.

#include <cstddef>

namespace visionrt
{
struct ToolDefinition;
struct PythonImplementation;
} // namespace visionrt

struct VrtPythonPaths
{
    // Wide strings owned by the caller and valid for the duration of vrt_python_create.
    const wchar_t *executable;
    const wchar_t *prefix;
    const wchar_t *base_prefix;
    const wchar_t *dll;
    const wchar_t *const *search_paths;
    std::size_t           search_path_count;
};

#if defined(VISIONRT_PYTHON_BRIDGE_BUILD)
#define VRT_PYTHON_EXPORT __declspec(dllexport)
#else
#define VRT_PYTHON_EXPORT
#endif

extern "C"
{
    // Creates the interpreter from the selected environment. Returns null on failure and writes a
    // UTF-8 reason into `error` when there is room.
    VRT_PYTHON_EXPORT void *vrt_python_create(const VrtPythonPaths *paths, char *error, std::size_t error_size);
    VRT_PYTHON_EXPORT void  vrt_python_destroy(void *handle);

    // Fills one status per definition, in the same order. `written` reports how many were stored.
    VRT_PYTHON_EXPORT int vrt_python_refresh(void *handle, const visionrt::ToolDefinition *definitions,
                                             std::size_t count, visionrt::PythonImplementation *statuses,
                                             std::size_t capacity, std::size_t *written);

    // Constructs a factory into the storage at `factory`, which must be large enough for a
    // visionrt::ToolFactory. It is destroyed with vrt_python_destroy_factory.
    VRT_PYTHON_EXPORT void vrt_python_factory(void *handle, const visionrt::ToolDefinition *definition, void *factory);
    VRT_PYTHON_EXPORT void vrt_python_destroy_factory(void *factory);

    VRT_PYTHON_EXPORT void        vrt_python_prepare_shutdown(void *handle);
    VRT_PYTHON_EXPORT std::size_t vrt_python_active_references(void *handle);
    VRT_PYTHON_EXPORT int         vrt_python_close_ready(void *handle);
}
