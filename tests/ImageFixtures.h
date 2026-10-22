#pragma once

#include <opencv2/imgcodecs.hpp>

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace visionrt::testing {
inline std::vector<unsigned char> oversizedBmp()
{
    std::vector<unsigned char> bytes;
    if (!cv::imencode(".bmp", cv::Mat(2, 3, CV_8UC3, cv::Scalar(1, 2, 3)), bytes))
        throw std::runtime_error("Cannot encode BMP fixture");
    const std::uint32_t width = (1U << 20) + 1;
    for (std::size_t index = 0; index < sizeof(width); ++index)
        bytes.at(18 + index) = static_cast<unsigned char>(width >> (8 * index));
    return bytes;
}
}
