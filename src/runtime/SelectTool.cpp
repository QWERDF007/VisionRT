#include "runtime/SelectTool.h"

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>

namespace visionrt {
namespace {
constexpr char kSelectBinding[] = "visionrt.select";

class SelectTool final : public IToolInstance
{
public:
    explicit SelectTool(TypeSpec type)
        : type_(std::move(type))
    {
    }

    ToolStatus initialize(const Values &parameters) override
    {
        return parameters.empty() ? ToolStatus{} : ToolStatus{1, "合流工具不接受初始化参数"};
    }

    ToolResult execute(const Values &inputs) override
    {
        const auto selector = inputs.find("selector");
        const auto *flag    = selector == inputs.end() ? nullptr : std::get_if<bool>(&selector->second);
        if (!flag)
            return {{1, "合流节点缺少布尔选择条件"}, {{"value", Missing{}}}};
        const auto &chosen = inputs.find(*flag ? "if_true" : "if_false");
        if (chosen == inputs.end() || isMissing(chosen->second))
            return {{1, *flag ? "成立分支没有可用值" : "否则分支没有可用值"}, {{"value", Missing{}}}};
        try
        {
            validateValue(type_, chosen->second);
        }
        catch (const ContractError &error)
        {
            return {{1, error.what()}, {{"value", Missing{}}}};
        }
        return {executionSuccess(), {{"value", chosen->second}}};
    }

    ToolStatus release() override
    {
        return {};
    }

private:
    TypeSpec type_;
};
} // namespace

void registerSelectTools(ToolRegistry &registry)
{
    registry.registerHostBinding(kSelectBinding,
                                 [](const ToolDefinition &definition, const PlatformToolServices &)
                                 {
                                     const auto output
                                         = std::find_if(definition.outputs.begin(), definition.outputs.end(),
                                                        [](const auto &port) { return port.id == "value"; });
                                     if (output == definition.outputs.end())
                                         throw ContractError("合流工具缺少 value 输出端口：" + definition.id);
                                     return std::make_unique<SelectTool>(output->type);
                                 });
}
} // namespace visionrt
