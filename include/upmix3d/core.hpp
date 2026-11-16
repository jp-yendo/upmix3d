#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace upmix3d {
enum class Channel : unsigned { FL, FR, FC, LFE, SL, SR, BL, BR, BC, TFL, TFR, TBL, TBR, Count };
constexpr std::size_t channel_count = static_cast<unsigned>(Channel::Count);
using Mask = std::uint32_t;
using Frame = std::array<float, channel_count>;
constexpr Mask bit(Channel c) { return Mask{1} << static_cast<unsigned>(c); }
constexpr Mask fronts = bit(Channel::FL) | bit(Channel::FR);
constexpr Mask sides = bit(Channel::SL) | bit(Channel::SR);
constexpr Mask backs = bit(Channel::BL) | bit(Channel::BR);
constexpr Mask top_fronts = bit(Channel::TFL) | bit(Channel::TFR);
constexpr Mask top_backs = bit(Channel::TBL) | bit(Channel::TBR);
constexpr Mask heights = top_fronts | top_backs;
constexpr Mask bed_51 = fronts | sides | bit(Channel::FC) | bit(Channel::LFE);
constexpr Mask bed_71 = bed_51 | backs;
struct Layout { std::string_view id; std::string_view label; Mask mask; };
inline constexpr std::array layouts{
    Layout{"3.1", "3.1", fronts | bit(Channel::FC) | bit(Channel::LFE)},
    Layout{"4.0", "4.0", fronts | sides},
    Layout{"5.1", "5.1", bed_51}, Layout{"7.1", "7.1", bed_71},
    Layout{"2.1.2", "2.1.2 (4.1)", fronts | bit(Channel::LFE) | top_fronts},
    Layout{"3.1.2", "3.1.2 (5.1)", fronts | bit(Channel::FC) | bit(Channel::LFE) | top_fronts},
    Layout{"5.1.2", "5.1.2 (7.1)", bed_51 | top_fronts},
    Layout{"5.1.4", "5.1.4 (9.1)", bed_51 | heights},
    Layout{"7.1.2", "7.1.2 (9.1)", bed_71 | top_fronts},
    Layout{"7.1.4", "7.1.4 (11.1)", bed_71 | heights}};
Mask layout_mask(std::string_view id);
// Human-readable name of a channel mask, such as "7.1.4 (11.1)" or "7.1.4 (11.1) + BC".
std::string describe_layout(Mask mask);
Mask normalize(Mask input);
Channel normalize_channel(Channel c, Mask input);
Mask resolve(Mask input, Mask requested, bool downmix);
std::vector<Channel> channels(Mask mask);
float db_gain(float db);

enum class Method { Matrix, Frequency };
enum class Protection { Fixed, Normalize, Limiter };
// Linked lookahead peak limiter. Peaks are measured as sample peaks or as 4x
// interpolated (true) peaks. The gain envelope is a sliding minimum followed by
// a moving average of the same length, so the gain reaches its target before the
// peak leaves the lookahead and never changes in steps. With hold enabled the
// gain never recovers until reset, which acts as peak normalization.
class PeakLimiter {
public:
    static constexpr std::size_t taps = 24;
    void configure(unsigned rate, float threshold, bool true_peak, bool hold, Mask channels);
    Frame process(const Frame& input);
    void reset();
    std::size_t latency_frames() const { return latency_; }
    float gain() const { return static_cast<float>(gain_); }
private:
    double required_gain() const;
    std::array<std::array<float, taps>, 4> coefficients_{};
    std::vector<Frame> history_;
    std::vector<double> required_, minimum_;
    std::vector<unsigned> active_;
    std::size_t cursor_ = 0, attack_ = 1, latency_ = 0;
    double previous_segment_ = 1;
    double gain_ = 1, release_ = 0;
    float threshold_ = 1;
    bool true_peak_ = true, hold_ = false;
};
struct Settings {
    Method method = Method::Frequency;
    std::size_t window = 2048;
    std::size_t hop = 512;
    float center_extract = .5f, center_width = 3.f, dimension = 0.f;
    float side_db = -4.f, back_db = -7.f, surround_ms = 10.f, decorrelation = 1.f;
    bool dialogue_protect = true;
    float height_db = -9.f, height_hz = 3000.f, height_ms = 0.f, height_balance = 0.f;
    bool downmix = false, mix_lfe = false;
    float center_mix_db = -3.f, surround_mix_db = -3.f, height_mix_db = -3.f, lfe_mix_db = -6.f;
    Protection protection = Protection::Limiter;
    bool true_peak = true;
    float threshold_db = -1.f, gain_db = 0.f, lfe_db = 0.f;
};
Settings preset(std::string_view name);
std::string_view input_layout_id(Mask input);
using PresetValues = std::array<float, 12>;
PresetValues preset_values(const Settings& settings);
void apply_preset_values(Settings& settings, const PresetValues& values);
bool apply_preset_overrides(Settings& settings, const PresetValues& values, const PresetValues& baseline);
// English name of a position for display, such as "Front left".
std::string_view position_name(Channel c);
// Japanese translation of an English user interface string, or nullptr when there is none.
const char* japanese_text(std::string_view english);
// All English user interface strings paired with their Japanese translations.
std::span<const std::pair<const char*, const char*>> ui_texts();
// Speaker identification sequence. Each position of the mask plays alone for one
// second followed by half a second of silence: pink noise for regular positions and
// a 60 Hz sine for LFE, both at about -30 dBFS RMS with 50 ms fades.
class SpeakerTest {
public:
    void configure(unsigned rate, Mask positions);
    Frame process();
    void stop() { slot_ = order_.size(); }
    bool active() const { return slot_ < order_.size(); }
    // Index and position of the slot currently playing. Valid while active.
    std::size_t slot() const { return slot_; }
    std::size_t slots() const { return order_.size(); }
    Channel channel() const { return order_[slot_]; }
private:
    std::vector<Channel> order_;
    std::size_t slot_ = 0, offset_ = 0, tone_ = 0, period_ = 1, fade_ = 1;
    unsigned rate_ = 48000;
    std::uint32_t random_ = 24;
    std::array<float, 3> pink_{};
};
class BoundaryRamp {
public:
    void configure(unsigned rate, float milliseconds);
    void arm();
    void fully_open();
    void fade_in();
    void fade_out();
    Frame process(Frame input);
    std::size_t remaining() const { return remaining_; }
    std::size_t length() const { return length_; }
private:
    std::size_t length_ = 0, remaining_ = 0;
    float start_ = 0, target_ = 1, gain_ = 0;
    bool armed_ = true;
};

class Delay {
public:
    void configure(std::size_t samples);
    float process(float input);
    void reset();
private:
    std::vector<float> data_;
    std::size_t cursor_ = 0;
};
class Allpass {
public:
    void configure(std::size_t samples, float coefficient);
    float process(float input);
    void reset();
private:
    std::vector<float> data_;
    std::size_t cursor_ = 0;
    float coefficient_ = .5f;
};

// Streaming Hann-window STFT. All three outputs have exactly window samples of delay.
class Extractor {
public:
    Extractor(std::size_t window, std::size_t hop, unsigned rate, bool dialogue);
    std::array<float, 3> process(float left, float right);
    void reset();
private:
    void analyze();
    std::size_t size_, hop_, cursor_ = 0;
    unsigned rate_;
    bool dialogue_;
    std::vector<float> left_, right_, window_, weights_;
    std::array<std::vector<float>, 3> overlap_;
    std::vector<std::complex<float>> l_, r_, work_;
};

class Processor {
public:
    Processor(std::vector<Channel> input_order, Mask output, unsigned rate, Settings settings);
    Frame process(std::span<const float> input);
    void reset();
    std::size_t latency_frames() const { return (spectral_ ? settings_.window : 0) +
        (settings_.protection != Protection::Fixed ? limiter_.latency_frames() : 0); }
    std::size_t tail_frames() const;
    Mask output_mask() const { return output_; }
    Mask input_mask() const { return input_; }
    bool bypass() const { return bypass_; }
private:
    void fold(Frame& output, Channel source, float value) const;
    Frame protect(Frame output);
    std::vector<Channel> order_;
    Mask input_ = 0, output_;
    unsigned rate_;
    Settings settings_;
    bool bypass_, spectral_;
    Extractor extractor_;
    PeakLimiter limiter_;
    std::array<Delay, channel_count> alignment_;
    std::array<Delay, 8> effect_delay_;
    std::array<Allpass, 8> diffuse_;
    std::array<float, 2> lowpass_{}, voice_low_{}, voice_high_{};
    float highpass_coefficient_, voice_low_coefficient_, voice_high_coefficient_;
    float side_gain_, back_gain_, height_gain_, gain_, lfe_gain_;
};

// Frame-based bounded ring. The owner supplies synchronization and back-pressure.
class FrameRing {
public:
    explicit FrameRing(std::size_t capacity);
    std::size_t size() const { return size_; }
    std::size_t available() const { return data_.size() - size_; }
    bool push(const Frame& frame);
    bool pop(Frame& frame);
    void clear();
private:
    std::vector<Frame> data_;
    std::size_t read_ = 0, write_ = 0, size_ = 0;
};
}
