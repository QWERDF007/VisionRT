#include "YoloPostprocess.h"
#include <array>
#include <iostream>
#include <vector>
using namespace visionrt::infer;

int main() {
    // Network is 32x32, stride=8, grid=4x4. Image occupies y=[8,24].
    LetterboxGeometry geometry{32, 16, 32, 32, 32, 16, 0, 8, 1.0f};
    constexpr int grid = 16;
    std::vector<float> disjoint(5 * grid, 0.f);
    for (int i = 0; i < grid; ++i) disjoint[4 * grid + i] = -10.f;
    auto setBox = [](std::vector<float>& values, int index, float l, float t, float r, float b, float logit) {
        values[index] = l; values[grid + index] = t;
        values[2 * grid + index] = r; values[3 * grid + index] = b;
        values[4 * grid + index] = logit;
    };
    // High score: [0,0,8,8], entirely padding; lower score: [10,10,14,14], valid.
    setBox(disjoint, 0, .5f, .5f, .5f, .5f, 5.f);
    setBox(disjoint, 5, .25f, .25f, .25f, .25f, 4.f);
    std::array<HeadView, 1> heads{{{disjoint, 8, 4, 4, 1}}};
    auto limited = postprocess(heads, geometry, {.5, .5, 1});
    auto unlimited = postprocess(heads, geometry, {.5, .5, 10});
    std::cout << "disjoint maximum=1 -> " << limited.size()
              << "; maximum=10 -> " << unlimited.size() << '\n';
    auto overlap = disjoint;
    overlap[4 * grid + 5] = -10.f;
    // Second box [0,0,8,16] has valid image pixels. Network IoU=.5.
    setBox(overlap, 4, .5f, 1.5f, .5f, .5f, 4.f);
    std::array<HeadView, 1> vertical{{{overlap, 8, 4, 4, 1}}};
    auto suppressed = postprocess(vertical, geometry, {.5, .4, 10});
    // Filter out invalid padding candidate by confidence to demonstrate valid candidate remains.
    overlap[4 * grid] = -10.f;
    auto validOnly = postprocess(vertical, geometry, {.5, .4, 10});
    std::cout << "padding suppression maximum=10 -> " << suppressed.size()
              << "; without padding candidate -> " << validOnly.size() << '\n';
    const bool reproduced = limited.empty() && unlimited.size() == 1
                         && suppressed.empty() && validOnly.size() == 1;
    std::cout << (reproduced ? "REPRODUCED: padding candidates consume limit and suppress valid image boxes" : "NOT_REPRODUCED") << '\n';
    return reproduced ? 0 : 1;
}
