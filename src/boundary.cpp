#include "upmix3d/core.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace upmix3d {
void BoundaryRamp::configure(unsigned rate, float milliseconds) {
    length_ = static_cast<std::size_t>(static_cast<float>(rate) * std::clamp(milliseconds, 0.f, 20.f) / 1000.f); arm();
}
void BoundaryRamp::arm() { armed_ = true; gain_ = start_ = 0; target_ = 1; remaining_ = length_; }
void BoundaryRamp::fully_open() { armed_ = false; gain_ = start_ = target_ = 1; remaining_ = 0; }
void BoundaryRamp::fade_in() { armed_ = gain_ == 0; start_ = gain_; target_ = 1; remaining_ = length_; }
void BoundaryRamp::fade_out() { armed_ = false; start_ = gain_; target_ = 0; remaining_ = length_; }
Frame BoundaryRamp::process(Frame input) {
    if (!length_) return input;
    if (armed_) {
        if (std::all_of(input.begin(), input.end(), [](float value) { return std::abs(value) < 1e-10f; })) return input;
        armed_ = false;
    }
    if (remaining_) {
        const float progress = static_cast<float>(length_ - remaining_) / static_cast<float>(length_);
        gain_ = start_ + (target_ - start_) * (.5f - .5f * std::cos(std::numbers::pi_v<float> * progress));
        --remaining_;
    } else gain_ = target_;
    for (auto& value : input) value *= gain_;
    return input;
}
}
