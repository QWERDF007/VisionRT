#pragma once

#include <visionrt/contracts/TypeSpec.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace visionrt {
inline std::uint64_t parseSequence(std::string_view value, std::string_view field)
{
    if (value.empty() || (value.size() > 1 && value.front() == '0')
        || !std::all_of(value.begin(), value.end(),
                        [](char character) { return character >= '0' && character <= '9'; }))
        throw ContractError(std::string(field) + " 必须是无前导零的无符号十进制文本：" + std::string(value));
    std::uint64_t result    = 0;
    const auto    converted = std::from_chars(value.data(), value.data() + value.size(), result);
    if (converted.ec != std::errc{} || converted.ptr != value.data() + value.size())
        throw ContractError(std::string(field) + " 超出 uint64 范围：" + std::string(value));
    return result;
}
}
