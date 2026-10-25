#include "YoloPostprocess.h"
#include <array>
#include <iostream>
#include <cmath>
#include <cstdlib>
#define REQUIRE(cond, msg)                                                                                      \
    do                                                                                                          \
    {                                                                                                           \
        if (!(cond))                                                                                            \
        {                                                                                                       \
            std::cerr << "Assertion failed: " << #cond << " (" << msg << ") at line " << __LINE__ << std::endl; \
            std::exit(1);                                                                                       \
        }                                                                                                       \
    }                                                                                                           \
    while (0)

using namespace visionrt::infer;

void testPostprocessIoU()
{
    std::cout << "[TEST] Running testPostprocessIoU..." << std::endl;
    Box a{0, 0, 10, 10};
    Box b{0, 0, 10, 10};
    REQUIRE(std::abs(intersectionOverUnion(a, b) - 1.0) < 1e-6, "Identical boxes IoU must be 1.0");

    Box c{10, 10, 20, 20};
    REQUIRE(intersectionOverUnion(a, c) == 0.0, "Disjoint boxes IoU must be 0.0");

    Box d{5, 0, 15, 10};
    // Intersection: [5, 0] to [10, 10] -> area 50
    // Total area: 100 + 100 - 50 = 150 -> IoU = 50 / 150 = 1/3
    REQUIRE(std::abs(intersectionOverUnion(a, d) - (1.0 / 3.0)) < 1e-6, "Half overlap IoU must be 1/3");
    std::cout << "[PASS] testPostprocessIoU passed." << std::endl;
}

void testPostprocessGeometry()
{
    std::cout << "[TEST] Running testPostprocessGeometry..." << std::endl;
    LetterboxGeometry geo{
        1920,       1080, // original
        640,        640,  // network input
        640,        360,  // resized
        0,          140,  // padLeft, padTop
        1.0f / 3.0f       // scale
    };

    Box netBox{0, 140, 640, 500};
    Box srcBox = toSource(netBox, geo);
    REQUIRE(std::abs(srcBox.x1 - 0.0) < 1e-3, "src x1 must be 0");
    REQUIRE(std::abs(srcBox.y1 - 0.0) < 1e-3, "src y1 must be 0");
    REQUIRE(std::abs(srcBox.x2 - 1920.0) < 1e-3, "src x2 must be 1920");
    REQUIRE(std::abs(srcBox.y2 - 1080.0) < 1e-3, "src y2 must be 1080");
    std::cout << "[PASS] testPostprocessGeometry passed." << std::endl;
}

void testYoloPaddingSuppressionAndLimit()
{
    std::cout << "[TEST] Running testYoloPaddingSuppressionAndLimit..." << std::endl;
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
    REQUIRE(limited.size() == 1, "Disjoint scenario: valid box must be preserved even with maximum=1");
    REQUIRE(unlimited.size() == 1, "Disjoint scenario: padding candidate must not enter kept list");

    auto overlap = disjoint;
    overlap[4 * grid + 5] = -10.f;
    // Second box [0,0,8,16] has valid image pixels. Network IoU=.5.
    setBox(overlap, 4, .5f, 1.5f, .5f, .5f, 4.f);
    std::array<HeadView, 1> vertical{{{overlap, 8, 4, 4, 1}}};
    auto suppressed = postprocess(vertical, geometry, {.5, .4, 10});
    REQUIRE(suppressed.size() == 1, "Overlap scenario: padding candidate must not suppress valid box");
    std::cout << "[PASS] testYoloPaddingSuppressionAndLimit passed." << std::endl;
}


int main(){testPostprocessIoU();testPostprocessGeometry();testYoloPaddingSuppressionAndLimit();}
