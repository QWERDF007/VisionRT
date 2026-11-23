#pragma once

#include "core/ValueJson.h"
#include "runtime/RuntimeEngine.h"
#include "visionrt/contracts/Module.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <filesystem>
#include <iostream>

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <Windows.h>
#    include <psapi.h>
#endif

namespace testsupport {

#ifdef _WIN32
inline QJsonArray loadedModuleEvidence()
{
    HMODULE modules[1024];
    DWORD   bytes = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytes) || bytes > sizeof(modules))
        throw std::runtime_error("cannot enumerate loaded acceptance modules");
    QJsonArray result;
    for (std::size_t index = 0; index < bytes / sizeof(HMODULE); ++index)
    {
        wchar_t    buffer[32768];
        const auto length = GetModuleFileNameW(modules[index], buffer, std::size(buffer));
        if (length == 0 || length == std::size(buffer))
            throw std::runtime_error("cannot read a loaded module path");
        const std::filesystem::path path(std::wstring(buffer, length));
        const auto                  name = path.filename().string();
        if (!name.starts_with("visionrt_") && !name.starts_with("vst_"))
            continue;
        QFile              file(QString::fromStdWString(path.wstring()));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
            throw std::runtime_error("cannot hash a loaded module");
        QJsonObject module{
            {  "path",                            file.fileName()},
            {"sha256", QString::fromLatin1(hash.result().toHex())}
        };
        if (const auto proc = GetProcAddress(modules[index], "visiontoolsModuleApi"))
        {
            const auto api = reinterpret_cast<const visionrt::ModuleApi *(VT_CALL *)() noexcept>(proc);
            module.insert("abi", static_cast<int>(api()->abi_major));
        }
        result.append(module);
    }
    return result;
}
#endif

// Records the actual published values beside the assertions, so installed acceptance can retain
// numerical results and expected failures without reconstructing them from a pass count.
inline void printExecutionEvidence(const std::filesystem::path &path, const visionrt::ProjectDefinition &project,
                                   const visionrt::RuntimeUpdate &update)
{
    QJsonArray nodes;
    for (const auto &flow : project.flows)
        for (const auto &node : flow.nodes)
        {
            const auto found = std::find_if(update.nodes.begin(), update.nodes.end(),
                                            [&](const auto &entry) { return entry.node == node.id; });
            if (found == update.nodes.end() || !found->snapshot)
                continue;
            const auto &result = *found->snapshot;
            QJsonObject outputs;
            for (const auto &[port, value] : result.outputs)
                outputs.insert(QString::number(port),
                               visionrt::ValueJson::encode(value, node.signatures.outputs.at(port),
                                                           visionrt::ValueJson::Purpose::Display));
            nodes.append(QJsonObject{
                {   "node",                      QString::number(node.id)},
                {   "tool",           QString::fromStdString(node.toolId)},
                {   "code",                            result.status.code},
                {"message", QString::fromStdString(result.status.message)},
                { "detail",  QString::fromStdString(result.status.detail)},
                {"outputs",                                       outputs}
            });
        }
    QJsonObject record{
        {"project", QString::fromStdWString(path.wstring())},
        {  "nodes",                                   nodes}
    };
#ifdef _WIN32
    record.insert("modules", loadedModuleEvidence());
#endif
    std::cout << "EXAMPLE_RESULT " << QJsonDocument(record).toJson(QJsonDocument::Compact).toStdString() << '\n';
}

} // namespace testsupport
