#include "upmix3d/core.hpp"
#include "upmix3d/transport.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

using namespace upmix3d;
namespace {
int checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
float get(const Frame& f, Channel c) { return f[static_cast<unsigned>(c)]; }
void near(float actual, float expected, float tolerance, const char* message) { check(std::abs(actual - expected) <= tolerance, message); }
Settings simple() {
    auto s = preset("music"); s.true_peak = false; s.method = Method::Matrix; s.center_extract = 0; s.surround_ms = 0;
    s.decorrelation = 0; s.dialogue_protect = false; s.threshold_db = 0; return s;
}
void layouts_test() {
    check(layouts.size() == 10, "All requested output layouts");
    check(channels(layout_mask("7.1.4")).size() == 12, "7.1.4 has 12 physical positions");
    check(normalize(fronts | backs) == (fronts | sides), "Quad rear positions normalize to sides");
    check(normalize(bed_71) == bed_71, "7.1 remains unchanged");
    check(resolve(fronts, bed_71 | heights, false) == (bed_71 | heights), "T-01 stereo to 7.1.4");
    check(resolve(bed_51, bed_51 | top_fronts, false) == (bed_51 | top_fronts), "T-02 preserve 5.1 bed");
    check(resolve(bed_71, bed_51, false) == bed_71, "T-03 preserve extra channels");
    check(resolve(bed_71, bed_51, true) == bed_51, "T-04 explicit downmix");
    check(resolve(bed_71, bed_51 | top_fronts, false) == (bed_71 | top_fronts), "T-05 union includes backs and heights");
    check(resolve(bed_71 | bit(Channel::BC), bed_71 | heights, false) & bit(Channel::BC), "T-12 retain back center");
    bool rejected = false; try { layout_mask("invalid"); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Unknown layout rejected");
}
void transparent_test() {
    const std::vector<Channel> order{Channel::FL,Channel::FR,Channel::SL,Channel::SR,Channel::BL,Channel::BR,Channel::BC,Channel::FC,Channel::LFE};
    const Mask mask = bed_71 | bit(Channel::BC);
    Processor p(order, mask, 48000, simple());
    check(p.bypass() && p.latency_frames() == 0, "Matching PCM bypass has no processing delay");
    const std::array samples{.11f,-.22f,.33f,-.44f,.55f,-.66f,.77f,-.88f,.99f};
    const auto output = p.process(samples);
    for (unsigned i = 0; i < order.size(); ++i) check(get(output, order[i]) == samples[i], "Q-02 VLC channel order sample transparency");
    Processor normalized({Channel::FL,Channel::FR,Channel::BL,Channel::BR,Channel::FC,Channel::LFE}, bed_51, 48000, simple());
    const std::array surround{.1f,.2f,.3f,.4f,.5f,.6f};
    auto result = normalized.process(surround);
    near(get(result, Channel::SL), .3f, 0, "T-06 rear left becomes side left");
    near(get(result, Channel::SR), .4f, 0, "T-06 rear right becomes side right");
}
void downmix_test() {
    auto s = simple(); s.downmix = true;
    Processor p({Channel::FL,Channel::FR,Channel::SL,Channel::SR,Channel::BL,Channel::BR,Channel::FC,Channel::LFE}, bed_51, 48000, s);
    const auto output = p.process(std::array{0.f,0.f,0.f,0.f,.2f,.4f,0.f,0.f});
    near(get(output, Channel::SL), .2f * db_gain(-3), 1e-6f, "Back left folds to side left");
    near(get(output, Channel::SR), .4f * db_gain(-3), 1e-6f, "Back right folds to side right");
    Processor center({Channel::FL,Channel::FR,Channel::FC}, fronts, 48000, s);
    const auto folded = center.process(std::array{0.f,0.f,.2f});
    near(get(folded, Channel::FL), .2f * db_gain(-3), 1e-6f, "Center folds at -3 dB");
    Processor lfe({Channel::FL,Channel::FR,Channel::LFE}, fronts, 48000, s);
    check(lfe.process(std::array{0.f,0.f,.5f}) == Frame{}, "LFE is discarded by default when absent from output");
    Processor mono({Channel::FC}, fronts, 48000, s);
    const auto mono_out = mono.process(std::array{.2f});
    near(get(mono_out, Channel::FL), .2f * db_gain(-3), 1e-6f, "Mono is folded exactly once");
}
void dsp_test() {
    auto s = simple();
    Processor p({Channel::FL,Channel::FR}, bed_71 | heights, 48000, s);
    double height_energy = 0, difference = 0;
    std::mt19937 rng(17); std::uniform_real_distribution<float> noise(-.2f,.2f);
    for (unsigned i = 0; i < 12000; ++i) {
        const auto output = p.process(std::array{noise(rng),noise(rng)});
        check(get(output, Channel::LFE) == 0, "T-11 LFE never synthesized");
        const auto a = get(output, Channel::TFL);
        height_energy += a*a;
        for (auto v : output) check(std::isfinite(v) && std::abs(v) <= 1, "Finite protected DSP output");
    }
    check(height_energy > 0, "Stereo ambience generates heights");
    // Heights always have separate allpass paths, independent of the surround control.
    s.decorrelation = 1;
    Processor diffuse({Channel::FL,Channel::FR}, bed_71 | heights, 48000, s);
    difference = 0;
    for (unsigned i = 0; i < 2000; ++i) {
        const auto output = diffuse.process(std::array{noise(rng),noise(rng)});
        difference += std::pow(get(output, Channel::TFL) - get(output, Channel::TBL), 2);
    }
    check(difference > 1e-4, "Four heights use different decorrelation paths");
    auto protected_settings = s; protected_settings.gain_db = 6; protected_settings.threshold_db = -1;
    Processor limiter({Channel::FL,Channel::FR}, bed_71 | heights, 48000, protected_settings);
    const auto loud = limiter.process(std::array{1.f,-1.f});
    for (auto value : loud) check(std::abs(value) <= db_gain(-1) + 1e-6f, "Q-01 linked limiter ceiling");
    p.reset(); const auto silence = p.process(std::array{0.f,0.f}); check(silence == Frame{}, "Flush clears filter state");
}
void stft_test() {
    for (auto n : {1024u,2048u,4096u}) for (auto divisor : {2u,4u}) {
        Extractor e(n, n/divisor, 48000, false);
        double error = 0, reference = 0;
        for (unsigned i = 0; i < n * 5; ++i) {
            const float input = i == 100 ? .7f : 0.f;
            const auto out = e.process(input,input);
            const float expected = i == n + 100 ? .7f : 0.f;
            error += std::pow(out[0] - expected, 2); reference += std::abs(out[1]) + std::abs(out[2]);
        }
        if (error >= 1e-8) std::cerr << "STFT N=" << n << " divisor=" << divisor << " squared error=" << error << '\n';
        check(error < 1e-8, "STFT overlap-add reconstructs coherent signal at exactly N samples");
        check(reference < 1e-5, "Coherent center does not leak into spectral ambience");
        e.reset(); for (unsigned i = 0; i < n * 2; ++i) check(e.process(0,0) == std::array<float,3>{}, "STFT reset removes overlap history");
    }
    auto s = simple(); s.method = Method::Frequency;
    Processor p({Channel::FL,Channel::FR}, bed_51 | top_fronts, 48000, s);
    check(p.latency_frames() == 2048, "Frequency processing declares analysis delay");
    for (unsigned i = 0; i <= 2048; ++i) {
        const auto out = p.process(std::array{i == 0 ? .2f : 0.f,0.f});
        near(get(out, Channel::FL), i == 2048 ? .2f : 0.f, 1e-6f, "Direct bed aligned with STFT-generated channels");
    }
}
void ring_test() {
    FrameRing ring(3); Frame value{}; value[0] = 7;
    check(ring.push(value) && ring.push(value) && ring.push(value) && !ring.push(value), "Ring refuses overflow");
    Frame read{}; check(ring.pop(read) && read[0] == 7 && ring.push(value), "Ring wraps without overwriting");
    ring.clear(); check(ring.size() == 0 && !ring.pop(read) && read == Frame{}, "Ring underrun emits silence");
}
void invalid_test() {
    auto settings = simple(); settings.height_db = std::numeric_limits<float>::quiet_NaN();
    bool rejected = false;
    try { Processor p({Channel::FL,Channel::FR}, bed_71, 48000, settings); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Reject non-finite DSP parameters");
    rejected = false;
    try { Extractor e(2048, 0, 48000, true); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Reject invalid STFT hop");
    Processor p({Channel::FL,Channel::FR}, fronts, 48000, simple());
    const auto output = p.process(std::array{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()});
    check(output == Frame{}, "Non-finite audio samples cannot poison the filters");
}
void transport_test() {
    for (auto codec : {EncodedCodec::Ac3, EncodedCodec::Dts, EncodedCodec::Eac3, EncodedCodec::DtsHd, EncodedCodec::TrueHd, EncodedCodec::Spdif}) {
        check(!permits(Passthrough::Disabled, codec), "Disabled policy rejects all encoded formats");
        check(permits(Passthrough::All, codec), "All policy includes every encoded format");
    }
    check(permits(Passthrough::Core, EncodedCodec::Ac3) && permits(Passthrough::Core, EncodedCodec::Dts) && !permits(Passthrough::Core, EncodedCodec::Eac3), "Core passthrough policy");
    near(static_cast<float>(encoded_spec(EncodedCodec::Ac3,48000,6).seconds(6144)), .032f, 1e-7f, "AC3 carrier time");
    near(static_cast<float>(encoded_spec(EncodedCodec::Eac3,48000,6).seconds(24576)), .032f, 1e-7f, "EAC3 carrier time");
    near(static_cast<float>(encoded_spec(EncodedCodec::TrueHd,48000,8).seconds(61440)), .02f, 1e-7f, "TrueHD MAT carrier time");
    auto hd = encoded_spec(EncodedCodec::DtsHd,48000,8);
    check(hd.carrier_rate == 192000 && hd.carrier_channels == 8 && hd.converter_rate == 768000, "DTS-HD converter and carrier clocks are distinct");
    for (unsigned rate : {16000u,22050u,24000u,32000u,44100u,48000u}) {
        const auto spec = encoded_spec(EncodedCodec::Eac3,rate,6);
        near(static_cast<float>(spec.seconds(24576)), 1536.f / rate, 1e-7f, "EAC3 burst duration follows source rate");
    }
    for (unsigned rate : {44100u,88200u,176400u,48000u,96000u,192000u}) {
        const unsigned carrier = rate % 44100 == 0 ? 176400 : 192000;
        const auto dts = encoded_spec(EncodedCodec::DtsHd,rate,8);
        const auto truehd = encoded_spec(EncodedCodec::TrueHd,rate,8);
        check(dts.carrier_rate == carrier && dts.converter_rate == carrier * 4, "DTS-HD preserves the sample-rate family");
        check(truehd.carrier_rate == carrier && truehd.converter_rate == carrier, "TrueHD preserves the sample-rate family");
        near(static_cast<float>(truehd.seconds(61440)), 3840.f / carrier, 1e-7f, "TrueHD MAT duration follows the rate family");
    }
    bool rejected = false;
    try { encoded_spec(EncodedCodec::TrueHd,32000,8); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Unknown HBR sample-rate families request PCM instead of changing playback speed");
    ByteRing ring(16);
    const std::array<std::uint8_t,8> burst{0x72,0xf8,0x1f,0x4e,1,0,8,0};
    check(ring.push(burst) && ring.push(burst) && !ring.push(burst), "Encoded backpressure does not overwrite a burst");
    std::array<std::uint8_t,12> first{};
    check(ring.pop(first) == 12 && std::equal(burst.begin(), burst.end(), first.begin()), "IEC payload remains byte exact");
    check(ring.push(burst), "Encoded ring wraps");
    ring.clear(); check(ring.push(burst,true), "Big endian IEC words accepted");
    std::array<std::uint8_t,8> swapped{}; ring.pop(swapped);
    for (unsigned i = 0; i < burst.size(); ++i) check(swapped[i] == burst[i^1], "IEC endianness conversion");
}
void profile_test() {
    check(input_layout_id(fronts | backs | bit(Channel::FC) | bit(Channel::LFE)) == "5.1", "Profile recognizes normalized rear 5.1");
    check(input_layout_id(bed_51 | bit(Channel::BC)) == "6.1", "Profile recognizes back-center 6.1");
    check(input_layout_id(bed_71 | bit(Channel::BC)) == "8.1", "Profile recognizes all nine input positions");
    auto movie = preset("movie"); const auto baseline = preset_values(preset("music"));
    check(!apply_preset_overrides(movie, baseline, baseline) && movie.center_extract == .65f, "Selecting a preset applies its own values");
    auto changed = preset_values(movie); auto movie_baseline = changed; changed[0] = 50;
    check(apply_preset_overrides(movie, changed, movie_baseline) && movie.center_extract == .5f && movie.height_db == -12, "Editing one preset value selects custom and retains the others");
    changed = movie_baseline; changed[10] = .5f; changed[11] = 0;
    check(apply_preset_overrides(movie, changed, movie_baseline) && movie.height_balance == .5f && !movie.dialogue_protect,
        "Height balance and dialogue protection participate in custom presets");
}
void peak_test() {
    TruePeakLimiter limiter; const float ceiling = db_gain(-1); limiter.configure(48000,ceiling);
    for (unsigned i = 0; i < 100; ++i) {
        Frame sample{}; sample[0] = .1f;
        const auto out = limiter.process(sample);
        near(out[0], i < TruePeakLimiter::latency_frames() ? 0 : .1f, 0, "Inactive true-peak limiter is sample transparent after declared delay");
    }
    limiter.reset();
    std::mt19937 random(89); std::uniform_real_distribution<float> noise(-2.f,2.f);
    std::array<Frame,TruePeakLimiter::taps> history{};
    // Independent dense sinc reconstruction checks intersample peaks after gain changes.
    for (unsigned i = 0; i < 20000; ++i) {
        Frame sample{}; sample[0] = noise(random); sample[1] = noise(random);
        const auto out = limiter.process(sample); history[i % history.size()] = out;
        for (auto v : out) check(std::isfinite(v) && std::abs(v) <= ceiling + 1e-6f, "True-peak limiting keeps sample peaks bounded");
        if (i < history.size()) continue;
        for (unsigned phase = 0; phase < 16; ++phase) {
            double sum = 0, normalization = 0;
            for (unsigned k = 0; k < history.size(); ++k) {
                const double x = static_cast<double>(k) - 11. - static_cast<double>(phase)/16.;
                const double window = .42 - .5*std::cos(2*3.141592653589793*k/23.) + .08*std::cos(4*3.141592653589793*k/23.);
                const double h = (std::abs(x)<1e-10 ? 1 : std::sin(3.141592653589793*x)/(3.141592653589793*x))*window;
                sum += h*history[(i+history.size()-k)%history.size()][0]; normalization += h;
            }
            check(std::abs(sum/normalization) <= ceiling + .002, "Dense reconstructed true peaks stay below the ceiling");
        }
    }
    Frame quiet{}; quiet[0]=.1f; Frame released{};
    for (unsigned i=0;i<48000;++i) released=limiter.process(quiet);
    check(released[0]==.1f, "Limiter release returns to exact unity without a persistent rounding attenuation");
    limiter.reset(); check(limiter.process(Frame{}) == Frame{}, "True-peak reset drops lookahead history");
}
void boundary_test() {
    BoundaryRamp ramp; ramp.configure(48000,5);
    Frame input{}; input[0] = 1;
    float previous = 0;
    for (unsigned i = 0; i < 300; ++i) { const float out = ramp.process(input)[0]; check(out >= previous && out - previous < .01f, "Boundary fade-in is continuous"); previous = out; }
    ramp.fade_out();
    for (unsigned i = 0; i < 300; ++i) { const float out = ramp.process(input)[0]; check(out <= previous && previous - out < .01f, "Boundary fade-out is continuous"); previous = out; }
    check(previous == 0, "Boundary fade ends at exact silence");
}
void configuration_matrix_test() {
    const std::array inputs{bit(Channel::FC),fronts,fronts|bit(Channel::LFE),fronts|bit(Channel::FC),fronts|bit(Channel::FC)|bit(Channel::LFE),
        fronts|backs,fronts|backs|bit(Channel::LFE),fronts|backs|bit(Channel::FC),fronts|backs|bit(Channel::FC)|bit(Channel::LFE),
        fronts|sides|backs,bed_51|bit(Channel::BC),bed_71&~bit(Channel::LFE),bed_71,bed_71|bit(Channel::BC)};
    for (auto input : inputs) for (const auto& layout : layouts) for (bool downmix : {false,true}) {
        auto settings = simple(); settings.downmix = downmix;
        const auto output_mask = resolve(input,layout.mask,downmix);
        Processor processor(channels(input),output_mask,48000,settings);
        std::vector<float> samples(channels(input).size(),.001f);
        for (unsigned i = 0; i < 20; ++i) {
            const auto output = processor.process(samples);
            for (unsigned c = 0; c < channel_count; ++c) {
                check(std::isfinite(output[c]), "Every documented input/output configuration produces finite samples");
                if (!(output_mask & (Mask{1}<<c))) check(output[c]==0, "No signal is written to absent output positions");
            }
            if (!(input & bit(Channel::LFE))) check(get(output,Channel::LFE)==0, "Every layout preserves the no-generated-LFE rule");
        }
        check(downmix || (processor.input_mask() & ~output_mask)==0, "Every non-downmixed input position is retained");
    }
    for (auto codec : {EncodedCodec::Ac3,EncodedCodec::Dts,EncodedCodec::Eac3,EncodedCodec::DtsHd,EncodedCodec::TrueHd}) {
        const auto spec=encoded_spec(codec,48000,8);
        const std::uint64_t bytes=static_cast<std::uint64_t>(spec.bytes_per_second())*7200;
        check(std::abs(spec.seconds(static_cast<std::size_t>(bytes))-7200.)<1e-9, "Two-hour transport time uses 64-bit byte accounting without accumulated drift");
    }
}
}
int main() {
    try { layouts_test(); transparent_test(); downmix_test(); dsp_test(); stft_test(); ring_test(); invalid_test(); transport_test(); profile_test(); peak_test(); boundary_test(); configuration_matrix_test(); }
    catch (const std::exception& e) { std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n'; return 1; }
    std::cout << "PASS: " << checks << " checks\n";
}
