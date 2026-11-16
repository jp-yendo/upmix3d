#include "upmix3d/core.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace upmix3d {
namespace {
// Interpolated points at time n lie between input samples n - centre - 1 and n - centre.
constexpr std::size_t centre = PeakLimiter::taps / 2 - 1;
constexpr double attack_seconds = .0015, release_seconds = .1;
}
void PeakLimiter::configure(unsigned rate, float threshold, bool true_peak, bool hold, Mask channels) {
    if (!rate || !std::isfinite(threshold) || threshold <= 0 || threshold > 1) throw std::invalid_argument("Invalid limiter configuration");
    threshold_ = threshold; true_peak_ = true_peak; hold_ = hold;
    release_ = 1. - std::exp(-1. / (release_seconds * rate));
    attack_ = std::max<std::size_t>(2, static_cast<std::size_t>(std::lround(attack_seconds * rate)));
    latency_ = attack_ - 1 + (true_peak ? centre + 1 : 0);
    active_.clear();
    for (unsigned c = 0; c < channel_count; ++c) if (channels & (Mask{1} << c)) active_.push_back(c);
    constexpr double pi = std::numbers::pi_v<double>;
    for (std::size_t phase = 0; phase < 4; ++phase) {
        double sum = 0;
        std::array<double, taps> h{};
        for (std::size_t k = 0; k < taps; ++k) {
            const double x = static_cast<double>(k) - static_cast<double>(centre) - static_cast<double>(phase) / 4.;
            const double window = .42 - .5 * std::cos(2 * pi * static_cast<double>(k) / (taps - 1)) +
                .08 * std::cos(4 * pi * static_cast<double>(k) / (taps - 1));
            h[k] = (std::abs(x) < 1e-12 ? 1 : std::sin(pi * x) / (pi * x)) * window;
            sum += h[k];
        }
        for (std::size_t k = 0; k < taps; ++k) coefficients_[phase][k] = static_cast<float>(h[k] / sum);
    }
    history_.assign(latency_ + taps, Frame{});
    required_.assign(attack_, 1.); minimum_.assign(attack_, 1.);
    reset();
}
double PeakLimiter::required_gain() const {
    const auto size = history_.size();
    auto sample = [&](std::size_t age, unsigned channel) { return history_[(cursor_ + size - age) % size][channel]; };
    double peak = 0;
    if (!true_peak_) {
        for (auto c : active_) peak = std::max(peak, static_cast<double>(std::abs(sample(0, c))));
    } else {
        // Segment between samples (centre + 1) and centre ago: both ends plus three interpolated points.
        for (auto c : active_) {
            peak = std::max({peak, static_cast<double>(std::abs(sample(centre, c))), static_cast<double>(std::abs(sample(centre + 1, c)))});
            for (std::size_t phase = 1; phase < 4; ++phase) {
                float value = 0;
                for (std::size_t k = 0; k < taps; ++k) value += coefficients_[phase][k] * sample(k, c);
                peak = std::max(peak, static_cast<double>(std::abs(value)));
            }
        }
    }
    return peak > threshold_ ? threshold_ / peak : 1.;
}
Frame PeakLimiter::process(const Frame& input) {
    const auto size = history_.size();
    history_[cursor_ % size] = input;
    double required = required_gain();
    if (true_peak_) {
        // A sample belongs to the segments on both of its sides.
        const double segment = required;
        required = std::min(segment, previous_segment_);
        previous_segment_ = segment;
    }
    required_[cursor_ % attack_] = required;
    minimum_[cursor_ % attack_] = *std::min_element(required_.begin(), required_.end());
    double sum = 0;
    for (double value : minimum_) sum += value;
    const double target = sum / static_cast<double>(attack_);
    if (target < gain_) gain_ = target;
    else if (!hold_) {
        gain_ += (target - gain_) * release_;
        // The final step of at most 0.001 dB is inaudible and restores exact transparency.
        if (target == 1. && gain_ > .9999) gain_ = 1.;
    }
    Frame output = history_[(cursor_ + size - latency_) % size];
    if (gain_ != 1.) for (auto& value : output) value = static_cast<float>(value * gain_);
    ++cursor_;
    return output;
}
void PeakLimiter::reset() {
    std::fill(history_.begin(), history_.end(), Frame{});
    std::fill(required_.begin(), required_.end(), 1.);
    std::fill(minimum_.begin(), minimum_.end(), 1.);
    cursor_ = 0; previous_segment_ = 1; gain_ = 1;
}
}
