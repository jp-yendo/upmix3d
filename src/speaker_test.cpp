#include "upmix3d/core.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace upmix3d {
namespace {
constexpr double tone_seconds = 1., gap_seconds = .5, fade_seconds = .05;
// Pink noise at about -30 dBFS RMS, and a 60 Hz sine at -30 dBFS RMS.
constexpr float pink_scale = .0185f, lfe_amplitude = .0447f, lfe_hz = 60.f;
}
std::string_view position_name(Channel c) {
    constexpr std::array<std::string_view, channel_count> names{"Front left", "Front right", "Center", "Subwoofer (LFE)",
        "Side left", "Side right", "Back left", "Back right", "Back center",
        "Top front left", "Top front right", "Top back left", "Top back right"};
    return names[static_cast<unsigned>(c)];
}
void SpeakerTest::configure(unsigned rate, Mask positions) {
    if (!rate) throw std::invalid_argument("Invalid speaker test rate");
    rate_ = rate;
    order_ = channels(positions);
    tone_ = static_cast<std::size_t>(std::lround(tone_seconds * rate));
    period_ = tone_ + static_cast<std::size_t>(std::lround(gap_seconds * rate));
    fade_ = static_cast<std::size_t>(std::lround(fade_seconds * rate));
    slot_ = offset_ = 0; random_ = 24; pink_ = {};
}
Frame SpeakerTest::process() {
    Frame output{};
    if (!active()) return output;
    if (offset_ < tone_) {
        const auto channel = order_[slot_];
        float sample;
        if (channel == Channel::LFE) {
            sample = lfe_amplitude * std::sin(2 * std::numbers::pi_v<float> * lfe_hz * static_cast<float>(offset_) / static_cast<float>(rate_));
        } else {
            // xorshift32 white noise, shaped to pink with a three-pole filter.
            random_ ^= random_ << 13; random_ ^= random_ >> 17; random_ ^= random_ << 5;
            const float white = static_cast<float>(random_) / 2147483648.f - 1.f;
            pink_[0] = .99765f * pink_[0] + .099046f * white;
            pink_[1] = .963f * pink_[1] + .296516f * white;
            pink_[2] = .57f * pink_[2] + 1.052691f * white;
            sample = (pink_[0] + pink_[1] + pink_[2] + .1848f * white) * pink_scale;
        }
        const auto edge = std::min(offset_, tone_ - 1 - offset_);
        const float fade = edge >= fade_ ? 1.f : .5f - .5f * std::cos(std::numbers::pi_v<float> * static_cast<float>(edge) / static_cast<float>(fade_));
        output[static_cast<unsigned>(channel)] = sample * fade;
    }
    if (++offset_ == period_) { offset_ = 0; ++slot_; }
    return output;
}
}
