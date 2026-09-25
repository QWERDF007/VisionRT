#include "infer/YoloPostprocess.h"

#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace visionrt::infer;

int main() {
    // Two coincident boxes in one class suppress; a different class survives.
    // Equal logits choose class zero, equal scores retain the first grid index.
    std::vector<float> values(6 * 4, -100.0F);
    for (int i = 0; i != 4; ++i) {
        values[i] = static_cast<float>(i) + 0.5F;
        values[4 + i] = 0.5F;
        values[8 + i] = 3.5F - static_cast<float>(i);
        values[12 + i] = 0.5F;
    }
    values[16] = values[20] = 0.0F;
    values[17] = 0.0F;
    values[22] = 0.0F;
    const std::array heads{HeadView{values, 8, 4, 1, 2}};
    const LetterboxGeometry geometry{32, 8, 32, 8, 32, 8, 0, 0, 1.0F};
    auto result = postprocess(heads, geometry, {0.5, 0.45, 100});
    assert(result.size() == 2);
    assert(result[0].candidateIndex == 0 && result[0].classId == 0);
    assert(result[1].candidateIndex == 2 && result[1].classId == 1);
    assert(result[0].score == 0.5F);
    assert(result[0].box.x1 == 0 && result[0].box.y1 == 0);
    assert(result[0].box.x2 == 32 && result[0].box.y2 == 8);
    assert(postprocess(heads, geometry, {0.5, 1.0, 100}).size() == 3);
    assert(postprocess(heads, geometry, {0.500001, 0.45, 100}).empty());
    assert(postprocess(heads, geometry, {0.5, 0.45, 1})[0].candidateIndex == 0);
    bool rejected = false;
    try { postprocess(heads, geometry, {std::numeric_limits<double>::quiet_NaN(), 0.5, 10}); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    // Actual 321x197 -> 640x393 integer resize has unequal axis scales.
    const LetterboxGeometry rounded{321, 197, 640, 640, 640, 393, 0, 123, 640.0F / 321};
    const auto full = toSource({0, 123, 640, 516}, rounded);
    assert(full.x1 == 0 && full.y1 == 0 && full.x2 == 321 && full.y2 == 197);
    const auto half = toSource({320, 319.5, 800, 900}, rounded);
    assert(half.x1 == 160.5 && half.y1 == 98.5 && half.x2 == 321 && half.y2 == 197);
    // Bad logits must not turn NaNs into zero detections or leak partial output.
    values[17] = std::numeric_limits<float>::infinity();
    rejected = false;
    try { postprocess(heads, geometry, {0.5, 0.45, 100}); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
}
