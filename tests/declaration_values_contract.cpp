#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/ToolValueValidation.h"
#include "core/ToolDefinition.h"
#include "core/ToolDeclarationYaml.h"
#include <visionrt/contracts/Tool.h>

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

using namespace visionrt;

namespace {
template<class F> void rejects(F&& operation, const std::string& expectedFragment = {}) {
    try {
        operation();
    } catch (const ContractError& error) {
        if (!expectedFragment.empty()) {
            const std::string message = error.what();
            if (message.find(expectedFragment) == std::string::npos) {
                std::cerr << "Unexpected message: " << message << " (expected fragment: " << expectedFragment << ")\n";
                std::abort();
            }
        }
        return;
    }
    std::cerr << "Expected ContractError was not thrown\n";
    std::abort();
}

PortDefinition port(std::string id, TypeSpec type) {
    PortDefinition definition;
    definition.id = std::move(id);
    definition.name = definition.id;
    definition.type = type;
    return definition;
}

PortContract contract(std::string id, TypeSpec type, bool required = true) {
    PortContract result;
    result.id = std::move(id);
    result.type = type;
    result.required = required;
    return result;
}
} // namespace

int main() {
    // CT-01: a single classification for every resource-path editor.
    {
        const auto definition = parseToolDefinitionYaml(R"(
schema_version: 1
tool_id: runtime.paths
display_name: 路径分类
version: 1.0.0
kind: cpp_function
entry: {registry_name: runtime.paths}
init_params: []
inputs:
  - {id: mode, name: 模式, type: String, editor: text}
  - {id: file, name: 文件, type: String, editor: file}
  - {id: directory, name: 目录, type: String, editor: directory}
  - {id: dynamic, name: 动态路径, type: String, editor: path, path_mode_from: mode}
outputs: []
)");
        assert(isPathPort(definition.inputs[1]));
        assert(isPathPort(definition.inputs[2]));
        assert(isPathPort(definition.inputs[3]));
        assert(!isPathPort(definition.inputs[0]));
        auto plain = port("plain", {Type::String});
        plain.editor = "text";
        assert(!isPathPort(plain));
        auto wrongType = port("wrong", {Type::Int});
        wrongType.editor = "file";
        assert(!isPathPort(wrongType));

        rejects([] {
            (void)parseToolDefinitionYaml(R"(
schema_version: 1
tool_id: runtime.paths.bad.file
display_name: 错误文件端口
version: 1.0.0
kind: cpp_function
entry: {registry_name: runtime.paths.bad.file}
init_params: []
inputs:
  - {id: file, name: 文件, type: Int, editor: file}
outputs: []
)");
        }, "路径编辑器");
        rejects([] {
            (void)parseToolDefinitionYaml(R"(
schema_version: 1
tool_id: runtime.paths.bad.directory
display_name: 错误目录端口
version: 1.0.0
kind: cpp_function
entry: {registry_name: runtime.paths.bad.directory}
init_params: []
inputs:
  - {id: directory, name: 目录, type: Bool, editor: directory}
outputs: []
)");
        }, "路径编辑器");
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

        const auto once = normalizeExecutionStatus({-1, ""});
        const auto twice = normalizeExecutionStatus(once);
        assert(once.code == twice.code && once.message == twice.message);
        assert(static_cast<bool>(ToolStatus{0, ""}));
        assert(!static_cast<bool>(ToolStatus{-1, ""}));
    }

    // CT-03: one actual-value implementation for options, range and finite values.
    {
        auto choice = contract("mode", {Type::Int});
        choice.options = {PortValue{std::int64_t{1}}, PortValue{std::int64_t{2}}};
        validateToolValues({choice}, {{"mode", std::int64_t{2}}}, false);
        rejects([&] { validateToolValues({choice}, {{"mode", std::int64_t{3}}}, false); }, "mode");

        auto ranged = contract("threshold", {Type::Int});
        ranged.minimum = PortValue{std::int64_t{0}};
        ranged.maximum = PortValue{std::int64_t{10}};
        validateToolValues({ranged}, {{"threshold", std::int64_t{0}}}, false);
        validateToolValues({ranged}, {{"threshold", std::int64_t{10}}}, false);
        rejects([&] { validateToolValues({ranged}, {{"threshold", std::int64_t{-1}}}, false); }, "threshold");
        rejects([&] { validateToolValues({ranged}, {{"threshold", std::int64_t{11}}}, false); }, "threshold");

        constexpr std::int64_t large = 9007199254740993LL;
        auto precise = contract("count", {Type::Int});
        precise.minimum = PortValue{large};
        precise.maximum = PortValue{large};
        validateToolValues({precise}, {{"count", large}}, false);
        rejects([&] { validateToolValues({precise}, {{"count", large - 1}}, false); }, "count");

        auto finite = contract("ratio", {Type::Float});
        finite.minimum = PortValue{0.0};
        rejects([&] {
            validateToolValues({finite}, {{"ratio", std::numeric_limits<double>::quiet_NaN()}}, false);
        }, "ratio");
        rejects([&] {
            validateToolValues({finite}, {{"ratio", std::numeric_limits<double>::infinity()}}, false);
        }, "ratio");
        validateToolValues({contract("ratio", {Type::Float})},
                           {{"ratio", std::numeric_limits<double>::quiet_NaN()}}, false);

        rejects([&] { validateToolValues({contract("image", {Type::ImageBatch})}, {}, false); }, "image");
        validateToolValues({contract("image", {Type::ImageBatch})}, {}, true);
        rejects([&] { validateToolValues({contract("image", {Type::ImageBatch})}, {{"image", Missing{}}}, false); },
                "image");
        rejects([&] { validateToolValues({contract("count", {Type::Int})}, {{"count", true}}, false); }, "count");

        PortDefinition declared = port("limit", {Type::Int});
        declared.defaultValue = PortValue{std::int64_t{5}};
        declared.minimum = PortValue{std::int64_t{1}};
        declared.maximum = PortValue{std::int64_t{9}};
        declared.options = {PortValue{std::int64_t{5}}, PortValue{std::int64_t{7}}};
        const auto generated = portContractFromDefinition(declared);
        assert(generated.id == "limit");
        assert(!generated.required);
        assert(generated.options.size() == 2);
        assert(generated.minimum && generated.maximum);
        const auto required = portContractFromDefinition(port("required", {Type::String}));
        assert(required.required);
    }

    std::cout << "[Runtime Contract] Path classification, status normalization and value constraints verified.\n";
    return 0;
}
