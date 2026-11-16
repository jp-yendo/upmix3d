#include "upmix3d/core.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace upmix3d {
namespace {
constexpr std::size_t index(Channel c) { return static_cast<unsigned>(c); }
constexpr float pi = std::numbers::pi_v<float>;
void fft(std::vector<std::complex<float>>& a, bool inverse) {
    const auto n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        auto b = n >> 1;
        for (; j & b; b >>= 1) j ^= b;
        j ^= b;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t length = 2; length <= n; length <<= 1) {
        const auto root = std::polar(1.f, (inverse ? 2.f : -2.f) * pi / static_cast<float>(length));
        for (std::size_t start = 0; start < n; start += length) {
            std::complex<float> w{1.f, 0.f};
            for (std::size_t j = 0; j < length / 2; ++j) {
                const auto u = a[start + j], v = a[start + j + length / 2] * w;
                a[start + j] = u + v;
                a[start + j + length / 2] = u - v;
                w *= root;
            }
        }
    }
    if (inverse) for (auto& v : a) v /= static_cast<float>(n);
}
}
Mask layout_mask(std::string_view id) {
    for (const auto& l : layouts) if (l.id == id) return l.mask;
    throw std::invalid_argument("Unknown output layout");
}
std::string describe_layout(Mask mask) {
    for (const auto& l : layouts) if (l.mask == mask) return std::string(l.label);
    if (normalize(mask) == mask) {
        const auto input = input_layout_id(mask);
        if (input != "other") return std::string(input);
    }
    for (const auto& l : layouts) if ((l.mask | bit(Channel::BC)) == mask) return std::string(l.label) + " + BC";
    constexpr std::array<std::string_view, channel_count> names{"FL","FR","FC","LFE","SL","SR","BL","BR","BC","TFL","TFR","TBL","TBR"};
    std::string result;
    for (auto c : channels(mask)) { if (!result.empty()) result += '+'; result += names[index(c)]; }
    return result.empty() ? "none" : result;
}
Mask normalize(Mask input) {
    if (!(input & sides) && (input & backs)) return (input & ~backs) |
        ((input & bit(Channel::BL)) ? bit(Channel::SL) : 0) |
        ((input & bit(Channel::BR)) ? bit(Channel::SR) : 0);
    return input;
}
Channel normalize_channel(Channel c, Mask input) {
    if (!(input & sides)) {
        if (c == Channel::BL) return Channel::SL;
        if (c == Channel::BR) return Channel::SR;
    }
    return c;
}
Mask resolve(Mask input, Mask requested, bool downmix) { return downmix ? requested : normalize(input) | requested; }
std::vector<Channel> channels(Mask mask) {
    std::vector<Channel> result;
    for (unsigned i = 0; i < channel_count; ++i) if (mask & (Mask{1} << i)) result.push_back(static_cast<Channel>(i));
    return result;
}
float db_gain(float db) { return std::pow(10.f, db / 20.f); }
Settings preset(std::string_view name) {
    Settings s;
    if (name == "movie") {
        s.center_extract = .65f; s.center_width = 0; s.side_db = -7; s.back_db = -11;
        s.surround_ms = 15; s.height_db = -12; s.height_hz = 3500;
    } else if (name == "wide") {
        s.center_extract = .45f; s.side_db = -2; s.back_db = -4; s.height_db = -6; s.height_hz = 2800;
    } else if (name != "music" && name != "custom") throw std::invalid_argument("Unknown preset");
    return s;
}
void Delay::configure(std::size_t samples) { data_.assign(samples, 0); cursor_ = 0; }
float Delay::process(float input) {
    if (data_.empty()) return input;
    const float output = data_[cursor_]; data_[cursor_] = input;
    cursor_ = (cursor_ + 1) % data_.size(); return output;
}
void Delay::reset() { std::fill(data_.begin(), data_.end(), 0.f); cursor_ = 0; }
void Allpass::configure(std::size_t samples, float coefficient) { data_.assign(std::max<std::size_t>(1, samples), 0.f); coefficient_ = coefficient; cursor_ = 0; }
float Allpass::process(float input) {
    const float delayed = data_[cursor_];
    const float internal = input + coefficient_ * delayed;
    data_[cursor_] = internal; cursor_ = (cursor_ + 1) % data_.size();
    return delayed - coefficient_ * internal;
}
void Allpass::reset() { std::fill(data_.begin(), data_.end(), 0.f); cursor_ = 0; }

Extractor::Extractor(std::size_t window, std::size_t hop, unsigned rate, bool dialogue)
    : size_(window), hop_(hop), rate_(rate), dialogue_(dialogue), left_(window), right_(window),
      window_(window), weights_(window * 2), l_(window), r_(window), work_(window) {
    if ((window != 1024 && window != 2048 && window != 4096) || (hop != window / 4 && hop != window / 2) || !rate)
        throw std::invalid_argument("Invalid STFT configuration");
    for (std::size_t i = 0; i < window; ++i) window_[i] = .5f - .5f * std::cos(2.f * pi * static_cast<float>(i) / static_cast<float>(window));
    for (auto& o : overlap_) o.resize(window * 2);
}
std::array<float, 3> Extractor::process(float left, float right) {
    const auto p = cursor_ % (size_ * 2);
    std::array<float, 3> result{};
    for (std::size_t j = 0; j < 3; ++j) {
        result[j] = cursor_ >= size_ && weights_[p] > 1e-8f ? overlap_[j][p] / weights_[p] : 0;
        overlap_[j][p] = 0;
    }
    weights_[p] = 0;
    left_[cursor_ % size_] = left; right_[cursor_ % size_] = right;
    ++cursor_;
    if (cursor_ % hop_ == 0) analyze();
    return result;
}
void Extractor::analyze() {
    for (std::size_t i = 0; i < size_; ++i) {
        const auto p = (cursor_ + i) % size_;
        l_[i] = left_[p] * window_[i]; r_[i] = right_[p] * window_[i];
    }
    fft(l_, false); fft(r_, false);
    for (unsigned stream = 0; stream < 3; ++stream) {
        for (std::size_t k = 0; k < size_; ++k) {
            const float frequency = static_cast<float>(std::min(k, size_ - k)) * static_cast<float>(rate_) / static_cast<float>(size_);
            const float lmag = std::abs(l_[k]), rmag = std::abs(r_[k]);
            const auto mid = .5f * (l_[k] + r_[k]);
            const float similarity = 2.f * std::min(lmag, rmag) / (lmag + rmag + 1e-12f);
            const auto common = mid * similarity;
            auto ambient = (stream == 1 ? l_[k] : r_[k]) - common;
            if (dialogue_ && frequency >= 200.f && frequency <= 4000.f) ambient *= .15f;
            work_[k] = stream == 0 ? common : ambient;
        }
        fft(work_, true);
        for (std::size_t i = 0; i < size_; ++i) {
            const auto p = (cursor_ + i) % (size_ * 2);
            overlap_[stream][p] += work_[i].real() * window_[i];
            if (stream == 0) weights_[p] += window_[i] * window_[i];
        }
    }
}
void Extractor::reset() {
    cursor_ = 0;
    std::fill(left_.begin(), left_.end(), 0.f); std::fill(right_.begin(), right_.end(), 0.f);
    std::fill(weights_.begin(), weights_.end(), 0.f);
    for (auto& o : overlap_) std::fill(o.begin(), o.end(), 0.f);
}

Processor::Processor(std::vector<Channel> input_order, Mask output, unsigned rate, Settings settings)
    : order_(std::move(input_order)), output_(output), rate_(rate), settings_(settings), bypass_(false), spectral_(false),
      extractor_(settings.window, settings.hop, rate, settings.dialogue_protect) {
    if (order_.empty() || order_.size() > channel_count || !rate || !output || (output >> channel_count))
        throw std::invalid_argument("Invalid audio layout");
    const std::array values{settings.center_extract, settings.center_width, settings.dimension,
        settings.side_db, settings.back_db, settings.surround_ms, settings.decorrelation,
        settings.height_db, settings.height_hz, settings.height_ms, settings.height_balance,
        settings.center_mix_db, settings.surround_mix_db, settings.height_mix_db, settings.lfe_mix_db,
        settings.threshold_db, settings.gain_db, settings.lfe_db};
    if (!std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); }) ||
        settings.center_extract < 0 || settings.center_extract > 1 || settings.center_width < 0 || settings.center_width > 7 ||
        settings.surround_ms < 0 || settings.surround_ms > 25 || settings.height_ms < 0 || settings.height_ms > 20 ||
        settings.height_hz < 1000 || settings.height_hz > 5000 || settings.threshold_db < -6 || settings.threshold_db > 0 ||
        settings.gain_db < -12 || settings.gain_db > 6 || settings.lfe_db < -12 || settings.lfe_db > 6)
        throw std::invalid_argument("Invalid DSP parameter range");
    if (settings.dimension < -3 || settings.dimension > 3 || settings.side_db < -18 || settings.side_db > 0 ||
        settings.back_db < -18 || settings.back_db > 0 || settings.height_db < -18 || settings.height_db > 0 ||
        settings.decorrelation < 0 || settings.decorrelation > 2 || settings.height_balance < -1 || settings.height_balance > 1 ||
        settings.center_mix_db < -6 || settings.center_mix_db > 0 || settings.surround_mix_db < -6 || settings.surround_mix_db > 0 ||
        settings.height_mix_db < -6 || settings.height_mix_db > 0 || settings.lfe_mix_db < -18 || settings.lfe_mix_db > 0)
        throw std::invalid_argument("Invalid mix parameter range");
    for (auto c : order_) {
        if (index(c) >= channel_count || (input_ & bit(c))) throw std::invalid_argument("Invalid input channel");
        input_ |= bit(c);
    }
    const auto raw = input_;
    input_ = normalize(raw);
    if ((input_ & ~output_) && !settings.downmix) throw std::invalid_argument("Output drops input positions with downmix disabled");
    for (auto& c : order_) c = normalize_channel(c, raw);
    bypass_ = input_ == output_ && settings.gain_db == 0 && settings.lfe_db == 0;
    spectral_ = settings.method == Method::Frequency && (output_ & ~input_ & ~bit(Channel::LFE)) != 0;
    highpass_coefficient_ = 1.f - std::exp(-2.f * pi * settings.height_hz / static_cast<float>(rate));
    voice_low_coefficient_ = 1.f - std::exp(-2.f * pi * 200.f / static_cast<float>(rate));
    voice_high_coefficient_ = 1.f - std::exp(-2.f * pi * 4000.f / static_cast<float>(rate));
    side_gain_ = db_gain(settings.side_db + settings.dimension * 2.f);
    back_gain_ = db_gain(settings.back_db + settings.dimension * 2.f);
    height_gain_ = db_gain(settings.height_db); gain_ = db_gain(settings.gain_db);
    lfe_gain_ = db_gain(settings.lfe_db);
    limiter_.configure(rate, db_gain(settings.threshold_db), settings.true_peak, settings.protection == Protection::Normalize, output_);
    for (auto& a : alignment_) a.configure(spectral_ ? settings.window : 0);
    for (unsigned i = 0; i < 8; ++i) {
        effect_delay_[i].configure(static_cast<std::size_t>((i < 4 ? settings.surround_ms : settings.height_ms) * static_cast<float>(rate) / 1000.f));
        diffuse_[i].configure(std::max<std::size_t>(1, static_cast<std::size_t>((.0011f + .00037f * static_cast<float>(i)) * static_cast<float>(rate))), .45f + .035f * static_cast<float>(i));
    }
}
void Processor::fold(Frame& out, Channel source, float value) const {
    auto add = [&](Channel c, float scale = 1.f) { if (output_ & bit(c)) { out[index(c)] += value * scale; return true; } return false; };
    auto pair = [&](Channel l, Channel r, float scale) {
        const bool a = add(l, scale), b = add(r, scale); return a || b;
    };
    const float surround = db_gain(settings_.surround_mix_db);
    switch (source) {
    case Channel::FC: pair(Channel::FL, Channel::FR, db_gain(settings_.center_mix_db)); break;
    case Channel::LFE: if (settings_.mix_lfe) pair(Channel::FL, Channel::FR, db_gain(settings_.lfe_mix_db)); break;
    case Channel::BC:
        if (!pair(Channel::BL, Channel::BR, surround) && !pair(Channel::SL, Channel::SR, surround))
            pair(Channel::FL, Channel::FR, surround);
        break;
    case Channel::BL: if (!add(Channel::SL, surround)) add(Channel::FL, surround); break;
    case Channel::BR: if (!add(Channel::SR, surround)) add(Channel::FR, surround); break;
    case Channel::SL: if (!add(Channel::BL, surround)) add(Channel::FL, surround); break;
    case Channel::SR: if (!add(Channel::BR, surround)) add(Channel::FR, surround); break;
    case Channel::TFL: add(Channel::FL, db_gain(settings_.height_mix_db)); break;
    case Channel::TFR: add(Channel::FR, db_gain(settings_.height_mix_db)); break;
    case Channel::TBL: if (!add(Channel::BL, db_gain(settings_.height_mix_db)) && !add(Channel::SL, db_gain(settings_.height_mix_db))) add(Channel::FL, db_gain(settings_.height_mix_db)); break;
    case Channel::TBR: if (!add(Channel::BR, db_gain(settings_.height_mix_db)) && !add(Channel::SR, db_gain(settings_.height_mix_db))) add(Channel::FR, db_gain(settings_.height_mix_db)); break;
    case Channel::FL: case Channel::FR: add(Channel::FC, .70710678f); break;
    default: break;
    }
}
Frame Processor::process(std::span<const float> samples) {
    if (samples.size() != order_.size()) throw std::invalid_argument("Invalid input frame size");
    Frame in{}, out{};
    for (std::size_t i = 0; i < order_.size(); ++i) in[index(order_[i])] = std::isfinite(samples[i]) ? std::clamp(samples[i],-1048576.f,1048576.f) : 0;
    if (bypass_) {
        return protect(in);
    }
    float left = in[index(Channel::FL)], right = in[index(Channel::FR)];
    if (!(input_ & fronts)) left = right = in[index(Channel::FC)] * .70710678f;
    float common = .5f * (left + right);
    std::array<float, 2> ambient{.5f * (left - right), .5f * (right - left)};
    if (spectral_) {
        const auto extracted = extractor_.process(left, right);
        common = extracted[0]; ambient = {extracted[1], extracted[2]};
    }
    for (std::size_t i = 0; i < channel_count; ++i) in[i] = alignment_[i].process(in[i]);
    for (std::size_t i = 0; i < channel_count; ++i) if ((input_ & output_) & (Mask{1} << i)) out[i] = in[i];
    // Center width moves the given share of center power to the front pair (0 = all in center, 7 = all phantom).
    const float spread = std::clamp(settings_.center_width / 7.f, 0.f, 1.f);
    if (!(input_ & fronts) && (input_ & bit(Channel::FC)) && (output_ & bit(Channel::FC)) && (output_ & fronts) == fronts) {
        const float center = in[index(Channel::FC)];
        out[index(Channel::FC)] = center * std::sqrt(1.f - spread);
        out[index(Channel::FL)] = out[index(Channel::FR)] = center * std::sqrt(spread * .5f);
    }
    if ((input_ & fronts) == fronts && !(input_ & bit(Channel::FC)) && (output_ & bit(Channel::FC))) {
        // The common component c appears in both fronts. Moving it to the center as sqrt(2) c keeps its power.
        const float extracted = common * settings_.center_extract;
        out[index(Channel::FC)] = extracted * std::sqrt(2.f * (1.f - spread));
        const float residual = extracted * (1.f - std::sqrt(spread));
        out[index(Channel::FL)] -= residual;
        out[index(Channel::FR)] -= residual;
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (input_ & sides) ambient[i] += in[index(i ? Channel::SR : Channel::SL)] * .5f;
        if (input_ & backs) ambient[i] += in[index(i ? Channel::BR : Channel::BL)] * .5f;
        if (settings_.dialogue_protect) {
            voice_low_[i] += voice_low_coefficient_ * (ambient[i] - voice_low_[i]);
            voice_high_[i] += voice_high_coefficient_ * (ambient[i] - voice_high_[i]);
            ambient[i] -= .85f * (voice_high_[i] - voice_low_[i]);
        }
    }
    const std::array<Channel, 8> generated{Channel::SL, Channel::SR, Channel::BL, Channel::BR, Channel::TFL, Channel::TFR, Channel::TBL, Channel::TBR};
    for (unsigned i = 0; i < 8; ++i) {
        float signal = ambient[i % 2];
        if (i >= 4) {
            if (i < 6) {
                lowpass_[i % 2] += highpass_coefficient_ * (signal - lowpass_[i % 2]);
            }
            signal -= lowpass_[i % 2];
        }
        const float decorrelated = diffuse_[i].process(signal);
        const float amount = i >= 4 ? 1.f : std::clamp(settings_.decorrelation, 0.f, 2.f) * .5f;
        signal += amount * (decorrelated - signal);
        signal = effect_delay_[i].process(signal);
        if ((output_ & bit(generated[i])) && !(input_ & bit(generated[i]))) {
            if (i < 4) out[index(generated[i])] = signal * (i < 2 ? side_gain_ : back_gain_);
            else {
                const float balance = (output_ & top_backs) ? std::sqrt(std::clamp((i < 6 ? 1.f - settings_.height_balance : 1.f + settings_.height_balance) * .5f, 0.f, 1.f)) : 1.f;
                out[index(generated[i])] = signal * height_gain_ * balance;
            }
        }
    }
    if (settings_.downmix) for (unsigned i = 0; i < channel_count; ++i)
        if ((input_ & ~output_) & (Mask{1} << i)) fold(out, static_cast<Channel>(i), in[i]);
    return protect(out);
}
Frame Processor::protect(Frame out) {
    out[index(Channel::LFE)] *= lfe_gain_;
    for (auto& value : out) value *= gain_;
    if (settings_.protection != Protection::Fixed) return limiter_.process(out);
    // Fixed attenuation provides headroom for processing; a matching layout has no processing to make room for.
    if (bypass_) return out;
    for (auto& value : out) value = std::clamp(value * .5f, -1.f, 1.f);
    return out;
}
void Processor::reset() {
    extractor_.reset(); limiter_.reset(); for (auto& d : alignment_) d.reset(); for (auto& d : effect_delay_) d.reset();
    for (auto& d : diffuse_) d.reset(); lowpass_ = {}; voice_low_ = {}; voice_high_ = {};
}
std::size_t Processor::tail_frames() const {
    if (bypass_) return latency_frames();
    return latency_frames() + static_cast<std::size_t>(static_cast<float>(rate_) *
        (.1f + std::max(settings_.surround_ms, settings_.height_ms) / 1000.f));
}
FrameRing::FrameRing(std::size_t capacity) : data_(capacity) { if (!capacity) throw std::invalid_argument("Empty ring buffer"); }
bool FrameRing::push(const Frame& frame) {
    if (!available()) return false;
    data_[write_] = frame; write_ = (write_ + 1) % data_.size(); ++size_; return true;
}
bool FrameRing::pop(Frame& frame) {
    if (!size_) { frame = {}; return false; }
    frame = data_[read_]; read_ = (read_ + 1) % data_.size(); --size_; return true;
}
void FrameRing::clear() { read_ = write_ = size_ = 0; }
}
