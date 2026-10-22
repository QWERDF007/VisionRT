#ifdef NDEBUG
#    undef NDEBUG
#endif

#include "core/DefinitionJson.h"
#include "core/ToolDefinition.h"
#include "core/ToolValueValidation.h"

#include <visionrt/contracts/Tool.h>

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

using namespace visionrt;

namespace {
template<class F>
void rejects(F &&operation, const std::string &expectedFragment = {})
{
    try
    {
        operation();
    }
    catch (const ContractError &error)
    {
        if (!expectedFragment.empty())
        {
            const std::string message = error.what();
            if (message.find(expectedFragment) == std::string::npos)
            {
                std::cerr << "Unexpected message: " << message << " (expected fragment: " << expectedFragment << ")\n";
                std::abort();
            }
        }
        return;
    }
    std::cerr << "Expected ContractError was not thrown\n";
    std::abort();
}

PortDefinition port(std::string id, TypeSpec type, bool required = true)
{
    PortDefinition definition;
    definition.id       = std::move(id);
    definition.name     = definition.id;
    definition.type     = type;
    definition.required = required;
    return definition;
}
} // namespace

int main()
{
    // CT-01: a single classification for every resource-path editor.
    {
        const auto definition = parseToolDefinitionJson(R"json({
  "schema_version": 2,
  "tool_id": "runtime.paths",
  "display_name": "路径分类",
  "version": "1.0.0",
  "kind": "cpp",
  "init_params": [],
  "inputs": [
    {"id": "mode", "name": "模式", "type": "String", "editor": "text"},
    {"id": "file", "name": "文件", "type": "String", "editor": "file"},
    {"id": "directory", "name": "目录", "type": "String", "editor": "directory"},
    {"id": "dynamic", "name": "动态路径", "type": "String", "editor": "path", "path_mode_from": "mode"}
  ],
  "outputs": []
})json");
        assert(isPathPort(definition.inputs[1]));
        assert(isPathPort(definition.inputs[2]));
        assert(isPathPort(definition.inputs[3]));
        assert(!isPathPort(definition.inputs[0]));
        auto plain   = port("plain", {Type::String});
        plain.editor = "text";
        assert(!isPathPort(plain));
        auto wrongType   = port("wrong", {Type::Int});
        wrongType.editor = "file";
        assert(!isPathPort(wrongType));

        rejects(
            []
            {
                (void)parseToolDefinitionJson(R"json({
  "schema_version": 2,
  "tool_id": "runtime.paths.bad.file",
  "display_name": "错误文件端口",
  "version": "1.0.0",
  "kind": "cpp",
  "init_params": [],
  "inputs": [
    {"id": "file", "name": "文件", "type": "Int", "editor": "file"}
  ],
  "outputs": []
})json");
            },
            "路径编辑器");
        rejects(
            []
            {
                (void)parseToolDefinitionJson(R"json({
  "schema_version": 2,
  "tool_id": "runtime.paths.bad.directory",
  "display_name": "错误目录端口",
  "version": "1.0.0",
  "kind": "cpp",
  "init_params": [],
  "inputs": [
    {"id": "directory", "name": "目录", "type": "Bool", "editor": "directory"}
  ],
  "outputs": []
})json");
            },
            "路径编辑器");
    }

    // CT-02: one execution status rule shared by tools and the host.
    {
        assert(executionSuccess().code == 0);
        assert(executionSuccess().message == "运行成功");
        assert(executionSuccess("   ").message == "运行成功");
        assert(executionSuccess("\t\r\n").message == "运行成功");
        assert(executionSuccess("已处理 3 张").message == "已处理 3 张");
        assert(ToolStatus{}.message.empty());

        const auto success = normalizeExecutionStatus({0, ""});
        assert(success.code == 0 && success.message == "运行成功");
        const auto described = normalizeExecutionStatus({0, "自定义说明"});
        assert(described.message == "自定义说明");
        const auto failure = normalizeExecutionStatus({7, " \t"});
        assert(failure.code == 7 && failure.message == "运行失败（错误码：7）");
        const auto business = normalizeExecutionStatus({2, "输入图像为空"});
        assert(business.code == 2 && business.message == "输入图像为空");
        const auto cancelled = normalizeExecutionStatus({-5, ""});
        assert(cancelled.code == -5 && cancelled.message == "运行已取消");
        const auto cancelledWithReason = normalizeExecutionStatus({-5, "用户停止"});
        assert(cancelledWithReason.message == "用户停止");

        const auto once  = normalizeExecutionStatus({-1, ""});
        const auto twice = normalizeExecutionStatus(once);
        assert(once.code == twice.code && once.message == twice.message);
        assert(static_cast<bool>(ToolStatus{0, ""}));
        assert(!static_cast<bool>(ToolStatus{-1, ""}));
    }

    // CT-03: one actual-value implementation for options, range and finite values.
    {
        auto choice    = port("mode", {Type::Int});
        choice.options = {PortValue{std::int64_t{1}}, PortValue{std::int64_t{2}}};
        validateToolValues(
            {
                choice
        },
            {{"mode", std::int64_t{2}}});
        rejects(
            [&]
            {
                validateToolValues(
                    {
                        choice
                },
                    {{"mode", std::int64_t{3}}});
            },
            "mode");

        auto ranged    = port("threshold", {Type::Int});
        ranged.minimum = PortValue{std::int64_t{0}};
        ranged.maximum = PortValue{std::int64_t{10}};
        validateToolValues(
            {
                ranged
        },
            {{"threshold", std::int64_t{0}}});
        validateToolValues(
            {
                ranged
        },
            {{"threshold", std::int64_t{10}}});
        rejects(
            [&]
            {
                validateToolValues(
                    {
                        ranged
                },
                    {{"threshold", std::int64_t{-1}}});
            },
            "threshold");
        rejects(
            [&]
            {
                validateToolValues(
                    {
                        ranged
                },
                    {{"threshold", std::int64_t{11}}});
            },
            "threshold");

        constexpr std::int64_t large   = 9007199254740993LL;
        auto                   precise = port("count", {Type::Int});
        precise.minimum                = PortValue{large};
        precise.maximum                = PortValue{large};
        validateToolValues(
            {
                precise
        },
            {{"count", large}});
        rejects(
            [&]
            {
                validateToolValues(
                    {
                        precise
                },
                    {{"count", large - 1}});
            },
            "count");

        auto finite    = port("ratio", {Type::Float});
        finite.minimum = PortValue{0.0};
        rejects(
            [&]
            {
                validateToolValues(
                    {
                        finite
                },
                    {{"ratio", std::numeric_limits<double>::quiet_NaN()}});
            },
            "ratio");
        rejects(
            [&]
            {
                validateToolValues(
                    {
                        finite
                },
                    {{"ratio", std::numeric_limits<double>::infinity()}});
            },
            "ratio");
        validateToolValues(
            {
                port("ratio", {Type::Float}
                 )
        },
            {{"ratio", std::numeric_limits<double>::quiet_NaN()}});

        rejects([&] { validateToolValues({port("image", {Type::Image})}, {}); }, "image");
        validateToolValues({port("image", {Type::Image}, false)}, {});
        rejects(
            [&]
            {
                validateToolValues(
                    {
                        port("image", {Type::Image}
                         )
                },
                    {{"image", Missing{}}});
            },
            "image");
        rejects(
            [&]
            {
                validateToolValues(
                    {
                        port("count", {Type::Int}
                         )
                },
                    {{"count", true}});
            },
            "count");

        PortDefinition declared = port("limit", {Type::Int});
        declared.defaultValue   = PortValue{std::int64_t{5}};
        declared.minimum        = PortValue{std::int64_t{1}};
        declared.maximum        = PortValue{std::int64_t{9}};
        declared.options        = {PortValue{std::int64_t{5}}, PortValue{std::int64_t{7}}};
        assert(declared.required);
        rejects([&] { validateToolValues({declared}, {}); }, "limit");
        rejects([&] { validateToolValue(declared, Missing{}); }, "limit");
        validateToolValue(declared, std::int64_t{5});
        declared.required = false;
        validateToolValues({declared}, {});
        validateToolValue(declared, Missing{});
        rejects([&] { validateToolValue(declared, std::int64_t{0}); }, "limit");
        rejects([&] { validateToolValue(declared, std::int64_t{6}); }, "limit");
        auto optional = port("optional", {Type::Image}, false);
        validateToolValue(optional, DataRef<Image>(Image{}));
        rejects([&] { validateToolValue(optional, std::int64_t{0}); }, "optional");
    }

    {
        const auto declared = parseToolDefinitionJson(R"json({
  "schema_version": 2,
  "tool_id": "runtime.required",
  "display_name": "必填声明",
  "version": "1.0.0",
  "kind": "cpp",
  "init_params": [
    {"id": "seed", "name": "种子", "type": "Int", "default": 5, "required": false}
  ],
  "inputs": [
    {"id": "input", "name": "输入", "type": "String", "default": "text"},
    {"id": "optional", "name": "可选输入", "type": "Int", "required": false}
  ],
  "outputs": []
})json");
        assert(!declared.initParams[0].required);
        assert(declared.inputs[0].required);
        assert(!declared.inputs[1].required);
        validateToolValues(declared.initParams, {
                                                    {"seed", Missing{}}
        });
        validateToolValue(declared.inputs[1], Missing{});
        rejects([&] { validateToolValue(declared.inputs[0], Missing{}); }, "input");
        const auto roundTrip = parseToolDefinitionJson(emitNormalizedToolJson(declared));
        assert(!roundTrip.initParams[0].required);
        assert(roundTrip.inputs[0].required);
        assert(!roundTrip.inputs[1].required);
    }
    std::cout << "[Runtime Contract] Path classification, status normalization and value constraints verified.\n";
    return 0;
}
