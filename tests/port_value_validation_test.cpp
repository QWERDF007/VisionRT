#ifdef NDEBUG
#    undef NDEBUG
#endif

#include "core/DefinitionJson.h"
#include "core/InputNormalizer.h"
#include "core/ToolDefinition.h"
#include "core/ToolValueValidation.h"
#include "ToolResultAssertions.h"

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

// A declaration port always carries a value: the explicit default or the type's empty value. The
// helper mirrors what normalisation does so a hand-built port behaves like a parsed one.
PortDefinition port(PortId id, TypeSpec type)
{
    PortDefinition definition;
    definition.id           = id;
    definition.nameZh       = "端口 " + std::to_string(id);
    definition.type         = type;
    definition.defaultValue = defaultValue(type);
    return definition;
}
} // namespace

int main()
try
{
    // CT-01: a single classification for every resource-path editor.
    {
        const auto definition = parseToolDefinitionJson(R"json({
  "schema_version": 6,
  "name_en": "runtime.paths",
  "name_zh": "路径分类",
  "version": "1.0.0",
  "implementation": {
    "kind": "cpp"
  },
  "next_port_id": {"init": 0, "inputs": 5, "outputs": 2},
  "ports": {
    "inputs": [
      {
        "id": 1,
        "name_en": "mode",
        "name_zh": "模式",
        "type": "string",
        "ui": {
          "editor": "text"
        }
      },
      {
        "id": 2,
        "name_en": "file",
        "name_zh": "文件",
        "type": "string",
        "ui": {
          "editor": "file"
        }
      },
      {
        "id": 3,
        "name_en": "directory",
        "name_zh": "目录",
        "type": "string",
        "ui": {
          "editor": "directory"
        }
      },
      {
        "id": 4,
        "name_en": "dynamic",
        "name_zh": "动态路径",
        "type": "string",
        "ui": {
          "editor": "path"
        },
        "relations": {
          "path_mode": {"stage": "inputs", "id": 1}
        }
      }
    ]
  }
})json");
        assert(isPathPort(definition.inputs[2]));
        assert(isPathPort(definition.inputs[3]));
        assert(isPathPort(definition.inputs[4]));
        assert(!isPathPort(definition.inputs[1]));
        auto plain   = port(8, {Type::String});
        plain.editor = "text";
        assert(!isPathPort(plain));
        auto wrongType   = port(12, {Type::Int});
        wrongType.editor = "file";
        assert(!isPathPort(wrongType));

        rejects(
            []
            {
                (void)parseToolDefinitionJson(R"json({
  "schema_version": 6,
  "name_en": "runtime.paths.bad.file",
  "next_port_id": {"init": 0, "inputs": 2, "outputs": 2},
  "name_zh": "错误文件端口",
  "version": "1.0.0",
  "implementation": {
    "kind": "cpp"
  },
  "ports": {
    "inputs": [
      {
        "id": 2,
        "name_en": "file",
        "name_zh": "文件",
        "type": "int",
        "ui": {
          "editor": "file"
        }
      }
    ]
  }
})json");
            },
            "路径编辑器");
        rejects(
            []
            {
                (void)parseToolDefinitionJson(R"json({
  "schema_version": 6,
  "name_en": "runtime.paths.bad.directory",
  "next_port_id": {"init": 0, "inputs": 2, "outputs": 2},
  "name_zh": "错误目录端口",
  "version": "1.0.0",
  "implementation": {
    "kind": "cpp"
  },
  "ports": {
    "inputs": [
      {
        "id": 3,
        "name_en": "directory",
        "name_zh": "目录",
        "type": "bool",
        "ui": {
          "editor": "directory"
        }
      }
    ]
  }
})json");
            },
            "路径编辑器");
    }

    // CT-02: one execution result shape. The tool writes both status keys and the host keeps the
    // message byte for byte, blank and negative codes included.
    {
        assert(executionSuccess().code == 0);
        assert(executionSuccess().message == "运行成功");
        assert(executionSuccess("   ").message == "运行成功");
        assert(executionSuccess("\t\r\n").message == "运行成功");
        assert(executionSuccess("已处理 3 张").message == "已处理 3 张");
        assert(ToolStatus{}.message.empty());

        const auto success = toolResult({{13, std::int64_t{3}}}, {0, ""});
        assert(testview::businessCount(success) == 1);
        assert(testview::code(success) == 0 && testview::message(success).empty());
        assert(std::get<std::int64_t>(success.outputs.at(13)) == 3);
        assert(success.outputs.contains(kStatusCodePort));
        assert(success.outputs.contains(kStatusMessagePort));

        const auto described = toolResult({}, {2, "输入图像为空"});
        assert(testview::code(described) == 2 && testview::message(described) == "输入图像为空");
        assert(testview::businessCount(described) == 0);

        // A negative code is data, not a framework failure, and it survives untouched.
        const auto negative = toolResult({}, {-7, "  保留原样"});
        assert(testview::code(negative) == -7 && testview::message(negative) == "  保留原样");
        assert(static_cast<bool>(ToolStatus{0, ""}));
        assert(!static_cast<bool>(ToolStatus{-1, ""}));
    }

    // CT-03: one actual-value implementation for options, range and finite values.
    {
        auto choice    = port(6, {Type::Int});
        choice.options = {PortValue{std::int64_t{1}}, PortValue{std::int64_t{2}}};
        validateToolValues(
            {
                choice
        },
            {{6, std::int64_t{2}}});
        rejects(
            [&]
            {
                validateToolValues(
                    {
                        choice
                },
                    {{6, std::int64_t{3}}});
            },
            "端口 6");

        auto ranged    = port(11, {Type::Int});
        ranged.minimum = PortValue{std::int64_t{0}};
        ranged.maximum = PortValue{std::int64_t{10}};
        validateToolValues(
            {
                ranged
        },
            {{11, std::int64_t{0}}});
        validateToolValues(
            {
                ranged
        },
            {{11, std::int64_t{10}}});
        rejects(
            [&]
            {
                validateToolValues(
                    {
                        ranged
                },
                    {{11, std::int64_t{-1}}});
            },
            "端口 11");
        rejects(
            [&]
            {
                validateToolValues(
                    {
                        ranged
                },
                    {{11, std::int64_t{11}}});
            },
            "端口 11");

        constexpr std::int64_t large   = 9007199254740993LL;
        auto                   precise = port(1, {Type::Int});
        precise.minimum                = PortValue{large};
        precise.maximum                = PortValue{large};
        validateToolValues(
            {
                precise
        },
            {{1, large}});
        rejects(
            [&]
            {
                validateToolValues(
                    {
                        precise
                },
                    {{1, large - 1}});
            },
            "端口 1");

        auto finite    = port(9, {Type::Float});
        finite.minimum = PortValue{0.0};
        rejects(
            [&]
            {
                validateToolValues(
                    {
                        finite
                },
                    {{9, std::numeric_limits<double>::quiet_NaN()}});
            },
            "端口 9");
        rejects(
            [&]
            {
                validateToolValues(
                    {
                        finite
                },
                    {{9, std::numeric_limits<double>::infinity()}});
            },
            "端口 9");
        validateToolValues(
            {
                port(9, {Type::Float}
                 )
        },
            {{9, std::numeric_limits<double>::quiet_NaN()}});

        // An absent business value is a normal state, not a validation failure: the port already
        // carries the declared default and the run status says whether the tool succeeded.
        validateToolValues({port(3, {Type::Image})}, {});
        validateToolValues({port(3, {Type::Image})}, {{3, Missing{}}});
        rejects(
            [&]
            {
                validateToolValues(
                    {
                        port(1, {Type::Int}
                         )
                },
                    {{1, true}});
            },
            "端口 1");

        PortDefinition declared = port(4, {Type::Int});
        declared.defaultValue   = PortValue{std::int64_t{5}};
        declared.minimum        = PortValue{std::int64_t{1}};
        declared.maximum        = PortValue{std::int64_t{9}};
        declared.options        = {PortValue{std::int64_t{5}}, PortValue{std::int64_t{7}}};
        // A missing value is not an error any more: the port already holds its declared default, and
        // an empty status port keeps Missing without turning into a 0.
        validateToolValues({declared}, {});
        validateToolValue(declared, Missing{});
        validateToolValue(declared, std::int64_t{5});
        rejects([&] { validateToolValue(declared, std::int64_t{0}); }, "端口 4");
        rejects([&] { validateToolValue(declared, std::int64_t{6}); }, "端口 4");
        const auto optional = port(7, {Type::Image});
        validateToolValue(optional, DataRef<Image>(Image{}));
        rejects([&] { validateToolValue(optional, std::int64_t{0}); }, "端口 7");
    }

    // CT-04: one legalisation entry for manual edits, upstream values, declared defaults and loaded
    // projects. It clamps declared ranges, falls back to a declared default outside the option set,
    // keeps int64 precision and never coerces a wrong type.
    {
        auto choice         = port(6, {Type::Int});
        choice.options      = {PortValue{std::int64_t{1}}, PortValue{std::int64_t{2}}};
        choice.defaultValue = PortValue{std::int64_t{2}};
        const auto fallback = normalizeInput(choice, std::int64_t{5});
        assert(equalValues(fallback.value, PortValue{std::int64_t{2}}) && !fallback.adjustment.empty());
        auto ranged    = port(11, {Type::Int});
        ranged.minimum = PortValue{std::int64_t{0}};
        ranged.maximum = PortValue{std::int64_t{10}};
        assert(equalValues(normalizeInput(ranged, std::int64_t{99}).value, PortValue{std::int64_t{10}}));
        assert(equalValues(normalizeInput(ranged, std::int64_t{9007199254740993LL}).value,
                           PortValue{std::int64_t{10}}));
        assert(equalValues(normalizeInput(ranged, std::int64_t{-99}).value, PortValue{std::int64_t{0}}));

        // Zero, false, an empty string and an empty array are real values, never Missing.
        assert(!isMissing(normalizeInput(port(1, {Type::Int}), std::int64_t{0}).value));
        assert(!isMissing(normalizeInput(port(2, {Type::Bool}), false).value));
        assert(!isMissing(normalizeInput(port(10, {Type::String}), std::string{}).value));
        assert(!isMissing(normalizeInput(port(5, {Type::Array, DType::Int64, 1}),
                                        Array::fromNested(NestedVector<std::int64_t, 1>{})).value));

        // A declared array port refuses a scalar with the same element dtype instead of reading it
        // as an array, and an int port refuses a float even when the value is integral.
        rejects([&] { normalizeInput(port(5, {Type::Array, DType::Int64, 1}), std::int64_t{1}); },
                "端口 5");
        rejects(
            [&]
            {
                normalizeInput(port(1, {Type::Int}), Array::fromNested(NestedVector<std::int64_t, 1>{1}));
            },
            "端口 1");
        rejects([&] { normalizeInput(port(9, {Type::Float}), std::numeric_limits<double>::quiet_NaN()); },
                "端口 9");
        rejects([&] { normalizeInput(port(9, {Type::Float}), std::numeric_limits<double>::infinity()); },
                "端口 9");
        // A boolean is not an integer, and a float is not truncated into one.
        rejects([&] { normalizeInput(ranged, true); }, "端口 11");
        rejects([&] { normalizeInput(ranged, 1.5); }, "端口 11");
    }

    {
        const auto declared = parseToolDefinitionJson(R"json({
  "schema_version": 6,
  "name_en": "runtime.required",
  "name_zh": "必填声明",
  "version": "1.0.0",
  "implementation": {
    "kind": "cpp"
  },
  "next_port_id": {"init": 1, "inputs": 3, "outputs": 2},
  "ports": {
    "init": [
      {
        "id": 0,
        "name_en": "seed",
        "name_zh": "种子",
        "type": "int",
        "default": 5
      }
    ],
    "inputs": [
      {
        "id": 1,
        "name_en": "input",
        "name_zh": "输入",
        "type": "string",
        "default": "text"
      },
      {
        "id": 2,
        "name_en": "optional",
        "name_zh": "可选输入",
        "type": "int"
      }
    ]
  }
})json");
        // Explicit defaults survive, omitted ones become the type's empty value.
        assert(equalValues(declared.initParams[0].defaultValue, PortValue{std::int64_t{5}}));
        assert(equalValues(declared.inputs[1].defaultValue, PortValue{std::string{"text"}}));
        assert(equalValues(declared.inputs[2].defaultValue, PortValue{std::int64_t{0}}));
        validateToolValues(declared.initParams, {
                                                    {14, Missing{}}
        });
        validateToolValue(declared.inputs[2], Missing{});
        validateToolValue(declared.inputs[1], Missing{});
        const auto roundTrip = parseToolDefinitionJson(emitNormalizedToolJson(declared));
        assert(equalValues(roundTrip.initParams[0].defaultValue, PortValue{std::int64_t{5}}));
        assert(equalValues(roundTrip.inputs[1].defaultValue, PortValue{std::string{"text"}}));
        assert(equalValues(roundTrip.inputs[2].defaultValue, PortValue{std::int64_t{0}}));
    }

    {
        // T41-02: testview helpers must be strict: at() + std::get(), no default fallbacks
        ToolResult incompleteCode{{{kStatusMessagePort, std::string("ok")}}, "detail"};
        bool caught = false;
        try { testview::code(incompleteCode); }
        catch (const std::out_of_range &) { caught = true; }
        assert(caught);

        ToolResult wrongTypeCode{{{kStatusCodePort, std::string("bad")}, {kStatusMessagePort, std::string("ok")}}, "detail"};
        caught = false;
        try { testview::code(wrongTypeCode); }
        catch (const std::bad_variant_access &) { caught = true; }
        assert(caught);

        ToolResult incompleteMsg{{{kStatusCodePort, std::int64_t(0)}}, "detail"};
        caught = false;
        try { testview::message(incompleteMsg); }
        catch (const std::out_of_range &) { caught = true; }
        assert(caught);

        ToolResult wrongTypeMsg{{{kStatusCodePort, std::int64_t(0)}, {kStatusMessagePort, std::int64_t(123)}}, "detail"};
        caught = false;
        try { testview::message(wrongTypeMsg); }
        catch (const std::bad_variant_access &) { caught = true; }
        assert(caught);

        ToolResult valid{{{kStatusCodePort, std::int64_t(7)}, {kStatusMessagePort, std::string("fail")}}, "d"};
        const auto st = testview::status(valid);
        assert(st.code == 7);
        assert(st.message == "fail");
        assert(st.detail == "d");

        ToolResult withExtra{{{kStatusCodePort, std::int64_t(0)}, {kStatusMessagePort, std::string("")}, {99, std::int64_t(1)}, {100, std::int64_t(2)}}, ""};
        assert(testview::businessCount(withExtra) == 2);
    }
    std::cout << "[Runtime Contract] Path classification, status normalization and value constraints verified.\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
