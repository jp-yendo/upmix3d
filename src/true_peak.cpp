#include "upmix3d/core.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace upmix3d {
void TruePeakLimiter::configure(unsigned rate, float threshold) {
    if (!rate || !std::isfinite(threshold) || threshold <= 0 || threshold > 1) throw std::invalid_argument("Invalid true-peak limiter configuration");
    threshold_ = threshold;
    release_ = 1.f - std::exp(-1.f / (.05f * static_cast<float>(rate)));
    constexpr double pi = std::numbers::pi_v<double>;
    for (std::size_t phase = 0; phase < 4; ++phase) {
        double sum = 0;
        for (std::size_t k = 0; k < taps; ++k) {
            const double x = static_cast<double>(k) - static_cast<double>(taps / 2 - 1) - static_cast<double>(phase) / 4.;
            const double window = .42 - .5 * std::cos(2 * pi * static_cast<double>(k) / (taps - 1)) +
                .08 * std::cos(4 * pi * static_cast<double>(k) / (taps - 1));
            coefficients_[phase][k] = (std::abs(x) < 1e-12 ? 1 : std::sin(pi*x) / (pi*x)) * window;
            sum += coefficients_[phase][k];
        }
        for (auto& value : coefficients_[phase]) value /= sum;
    }
    reset();
}
Frame TruePeakLimiter::process(const Frame& input) {
    history_[cursor_ % taps] = input;
    double bound = 0;
    // A triangle-inequality bound remains valid when the gain changes inside the
    // reconstruction window. It deliberately favors headroom over loudness.
    for (std::size_t channel = 0; channel < channel_count; ++channel) {
        bound = std::max(bound, std::abs(static_cast<double>(input[channel])));
        for (const auto& phase : coefficients_) {
            double value = 0;
            for (std::size_t k = 0; k < taps; ++k)
                value += std::abs(phase[k] * history_[(cursor_ + taps - k) % taps][channel]);
            bound = std::max(bound, value);
        }
    }
    bounds_[cursor_ % taps] = bound;
    Frame output{};
    if (cursor_ >= taps - 1) {
        const auto maximum = *std::max_element(bounds_.begin(), bounds_.end());
        const float target = maximum > threshold_ ? static_cast<float>(static_cast<double>(threshold_) / maximum) : 1.f;
        gain_ = std::min(target, gain_ + (1.f - gain_) * release_);
        if (target == 1.f && gain_ > .9998f) gain_ = 1.f;
        output = history_[(cursor_ + 1) % taps];
        for (auto& sample : output) sample *= gain_;
    }
    ++cursor_; return output;
}
void TruePeakLimiter::reset() { history_ = {}; bounds_ = {}; cursor_ = 0; gain_ = 1; }
}
