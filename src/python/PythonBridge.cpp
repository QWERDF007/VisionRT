#include "python/PythonBridgeApi.h"

#include "python/PythonRuntime.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <span>
#include <string>

namespace
{
void copyError(const std::string &text, char *error, std::size_t size)
{
    if (!error || size == 0)
        return;
    const auto length = std::min(text.size(), size - 1);
    std::memcpy(error, text.data(), length);
    error[length] = '\0';
}

visionrt::PythonEnvironment environmentFrom(const VrtPythonPaths &paths)
{
    visionrt::PythonEnvironment environment;
    environment.executable = paths.executable ? paths.executable : L"";
    environment.prefix     = paths.prefix ? paths.prefix : L"";
    environment.basePrefix = paths.base_prefix ? paths.base_prefix : L"";
    environment.dll        = paths.dll ? paths.dll : L"";
    for (std::size_t index = 0; index < paths.search_path_count; ++index)
        if (paths.search_paths[index])
            environment.searchPaths.emplace_back(paths.search_paths[index]);
    return environment;
}

visionrt::PythonRuntime *runtimeOf(void *handle)
{
    return static_cast<visionrt::PythonRuntime *>(handle);
}
} // namespace

void *vrt_python_create(const VrtPythonPaths *paths, char *error, std::size_t error_size)
{
    if (!paths)
    {
        copyError("缺少 Python 环境信息", error, error_size);
        return nullptr;
    }
    try
    {
        return new visionrt::PythonRuntime(environmentFrom(*paths));
    }
    catch (const std::exception &failure)
    {
        copyError(failure.what(), error, error_size);
        return nullptr;
    }
    catch (...)
    {
        copyError("Python 环境启动失败", error, error_size);
        return nullptr;
    }
}

void vrt_python_destroy(void *handle)
{
    delete runtimeOf(handle);
}

int vrt_python_refresh(void *handle, const visionrt::ToolDefinition *definitions,
                                                std::size_t count, visionrt::PythonImplementation *statuses,
                                                std::size_t capacity, std::size_t *written)
{
    if (written)
        *written = 0;
    auto *runtime = runtimeOf(handle);
    if (!runtime || !definitions)
        return 1;
    try
    {
        const auto result = runtime->refresh(std::span<const visionrt::ToolDefinition>(definitions, count));
        const auto stored = std::min(result.size(), capacity);
        for (std::size_t index = 0; index < stored; ++index)
            statuses[index] = result[index];
        if (written)
            *written = stored;
        return 0;
    }
    catch (...)
    {
        return 1;
    }
}

void vrt_python_factory(void *handle, const visionrt::ToolDefinition *definition,
                                                  void *factory)
{
    auto *storage = static_cast<visionrt::ToolFactory *>(factory);
    if (!storage)
        return;
    auto *runtime = runtimeOf(handle);
    if (!runtime || !definition)
    {
        new (storage) visionrt::ToolFactory();
        return;
    }
    new (storage) visionrt::ToolFactory(runtime->factory(*definition));
}

void vrt_python_destroy_factory(void *factory)
{
    if (auto *storage = static_cast<visionrt::ToolFactory *>(factory))
        std::destroy_at(storage);
}

void vrt_python_prepare_shutdown(void *handle)
{
    if (auto *runtime = runtimeOf(handle))
        runtime->prepareShutdown();
}

std::size_t vrt_python_active_references(void *handle)
{
    auto *runtime = runtimeOf(handle);
    return runtime ? runtime->activeReferences() : 0;
}

int vrt_python_close_ready(void *handle)
{
    auto *runtime = runtimeOf(handle);
    return runtime && runtime->isCloseReady() ? 1 : 0;
}
