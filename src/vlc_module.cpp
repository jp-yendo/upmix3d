#include "vlc_compat.hpp"
#include "upmix3d/windows_audio.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <map>
#include <set>
#include <thread>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>

using namespace upmix3d;
namespace {
using CoreFree = void (__cdecl*)(void*);
CoreFree core_free() {
    // Official VLC 3 Windows binaries use a different CRT from MSVC. Free returned
    // strings through the exact function imported by libvlccore, never our CRT.
    static const CoreFree release = []() -> CoreFree {
        const auto module = reinterpret_cast<const BYTE*>(GetModuleHandleW(L"libvlccore.dll"));
        if (!module) return nullptr;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
        const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!directory.VirtualAddress) return nullptr;
        auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(module + directory.VirtualAddress);
        for (; descriptor->Name; ++descriptor) {
            if (!descriptor->OriginalFirstThunk) continue;
            auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(module + descriptor->OriginalFirstThunk);
            auto* addresses = reinterpret_cast<const IMAGE_THUNK_DATA64*>(module + descriptor->FirstThunk);
            for (; names->u1.AddressOfData; ++names, ++addresses) {
                if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
                const auto* name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(module + names->u1.AddressOfData);
                if (std::strcmp(name->Name, "free") == 0) return reinterpret_cast<CoreFree>(addresses->u1.Function);
            }
        }
        return nullptr;
    }();
    return release;
}
// True when VLC shows its interface in Japanese. VLC applies its language setting to the
// environment before loading plugins; without a setting, the Windows display language applies.
bool japanese_ui() {
    static const bool japanese = [] {
        for (const wchar_t* name : {L"LANGUAGE", L"LC_ALL", L"LC_MESSAGES", L"LANG"}) {
            wchar_t value[64]{};
            const DWORD length = GetEnvironmentVariableW(name, value, 64);
            if (length > 0 && length < 64) return value[0] == L'j' && value[1] == L'a';
        }
        return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_JAPANESE;
    }();
    return japanese;
}
// User interface text in the language of the VLC interface: Japanese or English.
const char* tr(const char* english) {
    if (!japanese_ui()) return english;
    const char* japanese = japanese_text(english);
    return japanese ? japanese : english;
}
std::string format_text(const char* format, ...) {
    va_list arguments; va_start(arguments, format);
    va_list measured; va_copy(measured, arguments);
    const int length = std::vsnprintf(nullptr, 0, format, measured); va_end(measured);
    std::string text(length > 0 ? static_cast<std::size_t>(length) : 0, ' ');
    if (length > 0) std::vsnprintf(text.data(), text.size() + 1, format, arguments);
    va_end(arguments); return text;
}
struct Changes { std::atomic<unsigned> flags{0}; };
class DeviceWatcher final : public IMMNotificationClient {
public:
    DeviceWatcher(std::shared_ptr<Changes> changes, std::wstring selected, bool use_default)
        : changes_(std::move(changes)), selected_(std::move(selected)), default_(use_default) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** value) override {
        if (!value) return E_POINTER;
        if (id == __uuidof(IUnknown) || id == __uuidof(IMMNotificationClient)) { *value = static_cast<IMMNotificationClient*>(this); AddRef(); return S_OK; }
        *value = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --references_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR id, DWORD) override { return signal(id, true); }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR id) override { return signal(id, false); }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR id) override { return signal(id, true); }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (default_ && flow == eRender && role == eMultimedia) changes_->flags.fetch_or(3);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR id, const PROPERTYKEY key) override {
        // Session startup and volume changes also generate property notifications.
        // Only format/speaker changes on the active endpoint require a restart.
        if (key == PKEY_AudioEngine_DeviceFormat || key == PKEY_AudioEndpoint_PhysicalSpeakers) return signal(id, true);
        return S_OK;
    }
private:
    HRESULT signal(LPCWSTR id, bool restart) { changes_->flags.fetch_or(restart && id && selected_ == id ? 3 : 1); return S_OK; }
    std::atomic<ULONG> references_{1};
    std::shared_ptr<Changes> changes_;
    std::wstring selected_;
    bool default_;
};
std::string string_setting(audio_output_t* aout, const char* name) {
    vlc_value_t inherited{};
    if (var_Inherit(VLC_OBJECT(aout), name, VLC_VAR_STRING, &inherited)) return {};
    std::unique_ptr<char, CoreFree> value(inherited.psz_string, core_free());
    return value ? value.get() : "";
}
constexpr std::array<const char*, 12> preset_options{"upmix3d-center","upmix3d-width","upmix3d-dimension",
    "upmix3d-side-db","upmix3d-back-db","upmix3d-surround-ms","upmix3d-decorrelation",
    "upmix3d-height-db","upmix3d-height-hz","upmix3d-height-ms","upmix3d-height-balance","upmix3d-dialogue-protect"};
std::string serialize_values(const PresetValues& values) {
    std::ostringstream text; text.imbue(std::locale::classic()); text << std::setprecision(9);
    for (float value : values) text << value << ' ';
    return text.str();
}
bool parse_values(const std::string& text, PresetValues& values) {
    std::istringstream input(text); input.imbue(std::locale::classic());
    PresetValues parsed = preset_values(preset("music"));
    for (std::size_t i=0;i<parsed.size();++i) {
        if (!(input >> parsed[i])) { if (i>=10) { values=parsed;return true; } return false; }
        if (!std::isfinite(parsed[i])) return false;
    }
    values = parsed; return true;
}
Settings read_profile(audio_output_t* aout, Mask input_mask) {
    const auto layout = input_layout_id(input_mask);
    std::string key = "upmix3d-preset-" + std::string(layout);
    std::replace(key.begin(), key.end(), '.', '-');
    auto selected = string_setting(aout, key.c_str());
    if (selected == "inherit" || selected.empty()) {
        key = channels(input_mask).size() <= 2 ? "upmix3d-preset-stereo" : "upmix3d-preset-multichannel";
        selected = string_setting(aout, key.c_str());
    }
    if (selected == "inherit") { key = "upmix3d-preset"; selected = string_setting(aout, key.c_str()); }
    PresetValues current{}, baseline = preset_values(preset("music"));
    for (std::size_t i = 0; i < current.size(); ++i) current[i] = i==11 ? (var_InheritBool(aout,preset_options[i])?1.f:0.f) : var_InheritFloat(aout, preset_options[i]);
    parse_values(string_setting(aout, "upmix3d-controls-baseline"), baseline);
    std::map<std::string, PresetValues> snapshots;
    std::istringstream saved(string_setting(aout, "upmix3d-custom-profiles"));
    std::string line;
    while (std::getline(saved, line, ';')) {
        const auto separator = line.find('='); PresetValues values{};
        if (separator != std::string::npos && parse_values(line.substr(separator+1), values)) snapshots[line.substr(0, separator)] = values;
    }
    Settings settings = preset(selected);
    if (selected == "custom") {
        const auto found = snapshots.find(key);
        apply_preset_values(settings, found == snapshots.end() ? current : found->second);
    }
    if (apply_preset_overrides(settings, current, baseline)) {
        selected = "custom";
        config_PutPsz(aout, key.c_str(), "custom");
    }
    const auto applied = preset_values(settings);
    for (std::size_t i = 0; i < applied.size(); ++i) {
        if(i==11)config_PutInt(aout,preset_options[i],applied[i]>=.5f); else config_PutFloat(aout, preset_options[i], applied[i]);
    }
    const auto serialized = serialize_values(applied);
    config_PutPsz(aout, "upmix3d-controls-baseline", serialized.c_str());
    if (selected == "custom") {
        snapshots[key] = applied;
        std::string all;
        for (const auto& [name, values] : snapshots) all += name + '=' + serialize_values(values) + ';';
        config_PutPsz(aout, "upmix3d-custom-profiles", all.c_str());
    }
    if (var_InheritBool(aout, "upmix3d-save-settings") && config_SaveConfigFile(aout)) msg_Warn(aout, "Cannot save upmix3d settings to the VLC configuration file");
    msg_Info(aout, "Input layout %s; effective preset %s", layout.data(), selected.c_str());
    return settings;
}
Settings read_settings(audio_output_t* aout, Mask input) {
    Settings s = read_profile(aout, input);
    s.method = string_setting(aout, "upmix3d-method") == "matrix" ? Method::Matrix : Method::Frequency;
    s.window = static_cast<std::size_t>(var_InheritInteger(aout, "upmix3d-window"));
    const auto divisor = var_InheritInteger(aout, "upmix3d-hop-divisor");
    if ((s.window != 1024 && s.window != 2048 && s.window != 4096) || (divisor != 2 && divisor != 4)) throw std::invalid_argument("Invalid STFT window or hop divisor");
    s.hop = s.window / static_cast<std::size_t>(divisor);
    const auto latency = string_setting(aout, "upmix3d-latency");
    if (latency == "low") s.method = Method::Matrix;
    if (latency == "quality") { s.window = 4096; s.hop = 1024; }
    s.dialogue_protect = var_InheritBool(aout, "upmix3d-dialogue-protect");
    s.height_balance = var_InheritFloat(aout, "upmix3d-height-balance");
    s.downmix = var_InheritBool(aout, "upmix3d-downmix");
    if (string_setting(aout, "upmix3d-downmix-coefficients") == "custom") {
        s.center_mix_db = var_InheritFloat(aout, "upmix3d-center-mix-db");
        s.surround_mix_db = var_InheritFloat(aout, "upmix3d-surround-mix-db");
        s.height_mix_db = var_InheritFloat(aout, "upmix3d-height-mix-db");
    }
    s.mix_lfe = var_InheritBool(aout, "upmix3d-mix-lfe");
    s.lfe_mix_db = var_InheritFloat(aout, "upmix3d-lfe-mix-db");
    s.gain_db = var_InheritFloat(aout, "upmix3d-gain-db");
    s.lfe_db = var_InheritFloat(aout, "upmix3d-lfe-db");
    s.threshold_db = var_InheritFloat(aout, "upmix3d-threshold-db");
    s.true_peak = var_InheritBool(aout, "upmix3d-true-peak");
    const auto protect = string_setting(aout, "upmix3d-protection");
    s.protection = protect == "fixed" ? Protection::Fixed : protect == "normalize" ? Protection::Normalize : Protection::Limiter;
    return s;
}
std::vector<Channel> input_order(const audio_sample_format_t& fmt) {
    // VLC WG4 order is NOT WAVEFORMATEXTENSIBLE order.
    constexpr std::array flags{AOUT_CHAN_LEFT,AOUT_CHAN_RIGHT,AOUT_CHAN_MIDDLELEFT,AOUT_CHAN_MIDDLERIGHT,
        AOUT_CHAN_REARLEFT,AOUT_CHAN_REARRIGHT,AOUT_CHAN_REARCENTER,AOUT_CHAN_CENTER,AOUT_CHAN_LFE};
    constexpr std::array order{Channel::FL,Channel::FR,Channel::SL,Channel::SR,Channel::BL,Channel::BR,Channel::BC,Channel::FC,Channel::LFE};
    std::vector<Channel> result;
    unsigned known = 0;
    for (unsigned i = 0; i < order.size(); ++i) { known |= flags[i]; if (fmt.i_physical_channels & flags[i]) result.push_back(order[i]); }
    if (fmt.channel_type != AUDIO_CHANNEL_TYPE_BITMAP || (fmt.i_physical_channels & ~known)) return {};
    return result;
}
// Restores every upmix3d setting to its default value and saves the configuration file.
void reset_settings(audio_output_t* aout) {
    const module_t* module = module_find("upmix3d");
    unsigned count = 0;
    module_config_t* items = module ? module_config_get(module, &count) : nullptr;
    if (!items) { msg_Warn(aout, "Cannot read the upmix3d settings to reset them"); return; }
    for (unsigned i = 0; i < count; ++i) {
        const auto& item = items[i];
        if (!item.psz_name || std::strncmp(item.psz_name, "upmix3d-", 8) != 0) continue;
        if (item.i_type & CONFIG_ITEM_STRING) config_PutPsz(aout, item.psz_name, item.orig.psz);
        else if (item.i_type == CONFIG_ITEM_FLOAT) config_PutFloat(aout, item.psz_name, item.orig.f);
        else if (item.i_type == CONFIG_ITEM_INTEGER || item.i_type == CONFIG_ITEM_BOOL) config_PutInt(aout, item.psz_name, item.orig.i);
    }
    module_config_free(items);
    if (config_SaveConfigFile(aout)) msg_Warn(aout, "Cannot save the reset upmix3d settings to the VLC configuration file");
    msg_Info(aout, "All upmix3d settings were reset to their defaults");
}
const char* route_name(Route route) { return route == Route::Spatial ? "spatial" : route == Route::Exclusive ? "exclusive" : "shared"; }
}

struct aout_sys_t {
    audio_output_t* aout;
    std::mutex mutex;
    std::condition_variable condition;
    std::thread worker;
    HANDLE wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::unique_ptr<FrameRing> queue;
    std::unique_ptr<ByteRing> encoded_queue;
    EncodedSpec encoded_specification;
    bool encoded = false, swap_encoded_words = false, force_shared = false;
    // Consecutive runtime failures of the current stream. The first reinitializes the configured
    // route; a repeated failure reinitializes on shared WASAPI.
    unsigned failures = 0;
    std::chrono::steady_clock::time_point failed_at{};
    std::vector<Channel> order;
    Settings settings;
    std::string device;
    Mask requested = 0;
    Route requested_route = Route::Shared, actual_route = Route::Shared;
    bool top_middle = false, fold_back_center = false, quit = false, initialized = false, failed = false, paused = false;
    bool has_data = false, drain = false, reset_requested = false, volume_dirty = true;
    std::uint64_t reset_id = 0, completed_reset = 0;
    HRESULT init_result = E_FAIL;
    unsigned rate = 0;
    unsigned buffer_ms = 20;
    float transition_ms = 5;
    std::size_t processing_pending = 0, tail_pending = 0;
    double device_pending = 0;
    std::chrono::steady_clock::time_point clock_sampled = std::chrono::steady_clock::now();
    bool clock_running = false;
    float volume = 1.f;
    bool muted = false;
    std::array<int, 3> compensation{};
    // Notices already shown in this VLC session.
    std::set<std::string> notices;
    explicit aout_sys_t(audio_output_t* output) : aout(output) {}
    ~aout_sys_t() { if (wake) CloseHandle(wake); }
};

namespace {
// Requests reinitialization after a runtime failure. The caller holds state.mutex.
void request_recovery(aout_sys_t& state, HRESULT hr) {
    ++state.failures; state.failed_at = std::chrono::steady_clock::now();
    state.force_shared = !state.encoded && state.failures >= 2;
    msg_Err(state.aout, "Audio output failed (0x%lx); reinitializing %s", static_cast<unsigned long>(hr),
        state.encoded ? "the bitstream output" : state.force_shared ? "on shared WASAPI" : "on the configured route");
    aout_RestartRequest(state.aout, AOUT_RESTART_OUTPUT);
}
// Shows a VLC error dialog once per session for each distinct text. The caller must not hold state.mutex.
void notify_once(aout_sys_t& state, const std::string& text) {
    { std::lock_guard lock(state.mutex); if (!state.notices.insert(text).second) return; }
    msg_Warn(state.aout, "%s", text.c_str());
    vlc_dialog_display_error(state.aout, "upmix3d", "%s", text.c_str());
}
// Describes why the opened output differs from the configured one, or returns an empty string.
std::string output_notice(const aout_sys_t& state, const AudioSink& sink) {
    const Route wanted = (state.requested & heights) ? Route::Spatial : state.requested_route;
    const auto requested = describe_layout(state.requested), actual = describe_layout(sink.mask());
    if (sink.route() != wanted && wanted == Route::Spatial)
        return format_text(tr("Windows spatial sound is unavailable on this output device, so %s is played as %s. To use height speakers, turn on spatial sound for the device in the Windows sound settings, for example Dolby Atmos for home theater."),
            requested.c_str(), actual.c_str());
    if (sink.route() != wanted)
        return tr("Exclusive output is unavailable on this output device, so shared output is used. Another application may be using the device, or exclusive control may be turned off in the device properties (Advanced tab).");
    const Mask missing = state.requested & ~sink.mask() & ~(state.fold_back_center ? bit(Channel::BC) : 0);
    if (missing)
        return format_text(tr("The output device cannot play every position of %s, so it is played as %s and the missing positions are mixed into nearby speakers. Check the speaker setup of the device in the Windows sound settings."),
            requested.c_str(), actual.c_str());
    return {};
}
void worker_body(aout_sys_t& state) {
    AudioSink sink([&](const auto& s) { msg_Warn(state.aout, "%s", s.c_str()); });
    auto changes = std::make_shared<Changes>();
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    Microsoft::WRL::ComPtr<IMMNotificationClient> watcher;
    std::string selected_device;
    { std::lock_guard lock(state.mutex); selected_device = state.device; }
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
        auto watched_id = widen(selected_device);
        if (selected_device.empty()) {
            Microsoft::WRL::ComPtr<IMMDevice> active;
            if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &active))) {
                LPWSTR id = nullptr; if (SUCCEEDED(active->GetId(&id))) { watched_id = id; CoTaskMemFree(id); }
            }
        }
        watcher.Attach(new DeviceWatcher(changes, std::move(watched_id), selected_device.empty())); enumerator->RegisterEndpointNotificationCallback(watcher.Get());
    }
    struct Unregister {
        IMMDeviceEnumerator* enumerator; IMMNotificationClient* watcher;
        ~Unregister() { if (enumerator && watcher) enumerator->UnregisterEndpointNotificationCallback(watcher); }
    } unregister{enumerator.Get(), watcher.Get()};
    auto devices = enumerate_devices();
    for (const auto& device : devices) aout_HotplugReport(state.aout, device.id.c_str(), device.name.c_str());
    HRESULT hr;
    if (state.encoded) {
        hr = sink.open_encoded(selected_device, state.encoded_specification);
        if (FAILED(hr) && state.encoded_specification.codec == EncodedCodec::DtsHd) {
            msg_Warn(state.aout, "DTS-HD transport unavailable; trying DTS core");
            state.encoded_specification = encoded_spec(EncodedCodec::Dts, state.encoded_specification.source_rate, state.encoded_specification.source_channels);
            var_SetBool(state.aout, "dtshd", false);
            hr = sink.open_encoded(selected_device, state.encoded_specification);
        }
    } else hr = sink.open(selected_device, state.force_shared ? state.requested & bed_71 : state.requested,
        state.force_shared ? Route::Shared : state.requested_route, state.top_middle, state.buffer_ms, state.fold_back_center);
    std::unique_ptr<Processor> processor;
    if (SUCCEEDED(hr) && !state.encoded) {
        auto settings = state.settings;
        // Device fallback is a capability limit, so fold positions that cannot be represented.
        settings.downmix = settings.downmix || sink.mask() != state.requested;
        processor = std::make_unique<Processor>(state.order, sink.mask(), sink.rate(), settings);
    }
    if (SUCCEEDED(hr) && !state.encoded && !state.force_shared) {
        const auto notice = output_notice(state, sink);
        if (!notice.empty()) notify_once(state, notice);
    }
    {
        std::lock_guard lock(state.mutex);
        if (SUCCEEDED(hr)) {
            state.rate = sink.rate(); state.actual_route = sink.route();
            if (state.encoded) state.encoded_queue = std::make_unique<ByteRing>(std::max<std::size_t>(262144,
                static_cast<std::size_t>(sink.capacity()) * state.encoded_specification.block_align() * 4));
            else state.queue = std::make_unique<FrameRing>(std::max<std::size_t>(static_cast<std::size_t>(sink.capacity()),
                static_cast<std::size_t>(state.rate) / (state.buffer_ms == 10 ? 100 : 50)));
        }
        state.init_result = hr; state.initialized = true; state.condition.notify_all();
    }
    if (FAILED(hr)) return;
    if (state.encoded) msg_Info(state.aout, "Bitstream passthrough %s, carrier %u Hz / %u channels, route exclusive; DSP and volume bypassed",
        codec_name(state.encoded_specification.codec).data(), sink.rate(), state.encoded_specification.carrier_channels);
    else msg_Info(state.aout, "PCM input %s, output layout %s, route %s, %u Hz, DSP delay %zu frames",
        describe_layout(processor->input_mask()).c_str(), describe_layout(processor->output_mask()).c_str(),
        route_name(sink.route()), sink.rate(), processor->latency_frames());
    DWORD task_index = 0; HANDLE task = AvSetMmThreadCharacteristicsW(L"Audio", &task_index);
    struct TaskGuard { HANDLE handle; ~TaskGuard() { if (handle) AvRevertMmThreadCharacteristics(handle); } } guard{task};
    bool running = false, was_paused = false, underflow = false, software_volume = false;
    bool pausing = false, pause_tail_written = false, terminating = false, played = false, transition_active = false, draining_fade = false;
    BoundaryRamp ramp;
    ramp.configure(state.rate, state.transition_ms);
    unsigned timeouts = 0;
    std::uint64_t rendered = 0;
    std::array<float, channel_count> compact{};
    Frame last_frame{};
    auto queue_empty = [&] { return state.encoded ? state.encoded_queue->size() == 0 : state.queue->size() == 0; };
    auto pump = [&](UINT32 frames) {
        bool empty = false;
        if (state.encoded) {
            auto bytes = sink.encoded_buffer(frames);
            const auto copied = state.encoded_queue->pop(bytes);
            std::fill(bytes.begin() + copied, bytes.end(), std::uint8_t{});
            empty = copied < bytes.size();
        } else {
            for (UINT32 i = 0; i < frames; ++i) {
                if (pausing && ramp.remaining() == 0) { sink.write(i, Frame{}); continue; }
                if (pausing) { sink.write(i, ramp.process(last_frame)); continue; }
                Frame input{};
                if (state.queue->pop(input)) { state.processing_pending = processor->latency_frames(); state.tail_pending = processor->tail_frames(); }
                else { empty = true; if (state.processing_pending) --state.processing_pending; if (state.tail_pending) --state.tail_pending; }
                if (state.drain && !processor->bypass() && state.queue->size() == 0 && state.processing_pending <= ramp.length() && !draining_fade) {
                    ramp.fade_out(); draining_fade = true;
                }
                for (std::size_t c = 0; c < state.order.size(); ++c) compact[c] = input[c];
                if (state.volume > 1) for (std::size_t c = 0; c < state.order.size(); ++c) compact[c] *= state.volume;
                auto output = processor->process(std::span(compact.data(), state.order.size()));
                if (pausing || transition_active || !processor->bypass()) output = ramp.process(output);
                else ramp.fully_open();
                if (transition_active && ramp.remaining() == 0) transition_active = false;
                const float gain = state.muted ? 0.f : software_volume ? std::min(1.f, state.volume) : 1.f;
                for (auto& sample : output) sample = std::clamp(sample * gain, -1.f, 1.f);
                last_frame = output;
                sink.write(i, output);
            }
        }
        state.condition.notify_all(); return empty;
    };
    for (;;) {
        std::unique_lock lock(state.mutex);
        if (state.quit) {
            if (!running || state.encoded || state.transition_ms == 0) break;
            terminating = true;
        }
        const auto changed = changes->flags.exchange(0);
        if (changed & 1) {
            auto current = enumerate_devices();
            for (const auto& previous : devices)
                if (std::none_of(current.begin(), current.end(), [&](const auto& d) { return d.id == previous.id; })) aout_HotplugReport(state.aout, previous.id.c_str(), nullptr);
            for (const auto& device : current) aout_HotplugReport(state.aout, device.id.c_str(), device.name.c_str());
            devices = std::move(current);
        }
        if (changed & 2) {
            state.force_shared = false; state.failures = 0;
            msg_Info(state.aout, "Audio endpoint configuration changed; requesting output restart");
            aout_RestartRequest(state.aout, AOUT_RESTART_OUTPUT);
        }
        if (state.reset_requested && (!running || state.encoded || state.transition_ms == 0)) {
            hr = sink.reset(); running = false; was_paused = false; pausing = false; pause_tail_written = false; draining_fade = false; ramp.arm();
            if (processor) processor->reset(); transition_active = true; last_frame = {};
            if (state.encoded) state.encoded_queue->clear(); else state.queue->clear();
            state.processing_pending = state.tail_pending = 0; state.device_pending = 0; state.has_data = false; state.drain = false;
            state.reset_requested = false; state.completed_reset = state.reset_id; state.condition.notify_all();
            if (FAILED(hr)) { state.failed = true; state.condition.notify_all(); break; }
        }
        const bool pause = (state.paused || state.reset_requested || terminating) && !state.drain;
        if (pause && running && !pausing) {
            if (state.encoded || state.transition_ms == 0) { hr = sink.stop(); running = false; was_paused = true; }
            else { pausing = true; pause_tail_written = false; ramp.fade_out(); }
        }
        if (!pause && pausing) { pausing = false; pause_tail_written = false; ramp.fade_in(); transition_active = true; }
        if (pausing && pause_tail_written) {
            state.device_pending = sink.pending_seconds();
            if (state.device_pending <= .001) {
                hr = sink.stop(); running = false; was_paused = false; pausing = false; ramp.arm(); transition_active = true;
                if (terminating) break;
                if (state.reset_requested) continue;
            }
        }
        if (state.volume_dirty && !state.encoded) {
            const HRESULT volume_hr = sink.volume(state.muted ? 0.f : std::min(1.f, state.volume));
            software_volume = FAILED(volume_hr);
            if (FAILED(volume_hr)) msg_Warn(state.aout, "Stream volume service unavailable (0x%lx)", static_cast<unsigned long>(volume_hr));
            state.volume_dirty = false;
        }
        const bool prebuffered = was_paused || state.drain || (state.encoded ?
            state.encoded_queue->size() >= static_cast<std::size_t>(sink.capacity()) * state.encoded_specification.block_align() :
            state.queue->size() >= (sink.route() == Route::Spatial ? state.rate / 100 : sink.capacity()));
        if (!pause && state.has_data && !running && prebuffered) {
            ramp.arm(); draining_fade = false;
            if (played) transition_active = true;
            // WASAPI exclusive event mode requires a full initial packet before Start.
            if (!was_paused && sink.route() != Route::Spatial) {
                UINT32 frames = 0; hr = sink.begin(frames);
                if (SUCCEEDED(hr)) {
                    pump(frames);
                    hr = sink.end(frames); state.condition.notify_all();
                }
            }
            if (SUCCEEDED(hr)) hr = sink.start();
            if (FAILED(hr)) { state.failed = true; state.condition.notify_all(); break; }
            running = true; was_paused = false; played = true;
        }
        if (state.drain && queue_empty() && !state.tail_pending) state.device_pending = sink.pending_seconds();
        if (state.drain && queue_empty() && !state.tail_pending && state.device_pending <= .001) {
            sink.reset(); if (processor) processor->reset(); running = false; state.has_data = false;
            state.device_pending = 0; state.drain = false; state.condition.notify_all();
        }
        state.clock_running = running;
        const HANDLE events[]{state.wake, sink.event()};
        lock.unlock();
        const DWORD result = WaitForMultipleObjects(running ? 2 : 1, events, FALSE, 100);
        lock.lock();
        if (state.quit && !running) break;
        if (result == WAIT_TIMEOUT) {
            if (running && sink.route() == Route::Spatial && ++timeouts <= 2) {
                msg_Warn(state.aout, "Spatial render event timed out; resetting stream");
                hr = sink.reset();
                if (SUCCEEDED(hr)) hr = sink.start();
                if (FAILED(hr)) { state.failed = true; state.condition.notify_all(); break; }
                state.volume_dirty = true;
            } else if (running && ++timeouts > 3) {
                hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
                state.failed = true; state.condition.notify_all(); break;
            }
        }
        if (result == WAIT_FAILED) {
            hr = HRESULT_FROM_WIN32(GetLastError()); state.failed = true; state.condition.notify_all(); break;
        }
        if (result != WAIT_OBJECT_0 + 1 || !running) continue;
        timeouts = 0;
        if (pausing && pause_tail_written) { state.device_pending = sink.pending_seconds(); continue; }
        if (state.drain && queue_empty() && !state.tail_pending) {
            state.device_pending = sink.pending_seconds(); continue;
        }
        UINT32 frames = 0; hr = sink.begin(frames);
        if (FAILED(hr)) { state.failed = true; state.condition.notify_all(); break; }
        if (sink.route() == Route::Spatial) software_volume = FAILED(sink.volume(state.muted ? 0.f : std::min(1.f, state.volume)));
        const bool empty = pump(frames);
        if (pausing && ramp.remaining() == 0) pause_tail_written = true;
        hr = sink.end(frames);
        if (FAILED(hr)) { state.failed = true; state.condition.notify_all(); break; }
        rendered += frames;
        if (state.failures && rendered >= static_cast<std::uint64_t>(state.rate) * 5) state.failures = 0;
        state.device_pending = sink.pending_seconds();
        state.clock_sampled = std::chrono::steady_clock::now();
        if (state.device_pending < 0) { state.failed = true; state.condition.notify_all(); break; }
        if (empty && !underflow && !state.drain) msg_Dbg(state.aout, "Audio ring underrun; emitting silence");
        underflow = empty;
        state.condition.notify_all();
    }
    sink.stop();
    {
        std::lock_guard lock(state.mutex);
        if (state.failed) request_recovery(state, hr);
        state.condition.notify_all();
    }
}
void worker_entry(aout_sys_t* state) noexcept {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) {
        std::lock_guard lock(state->mutex); state->init_result = initialized; state->initialized = true; state->condition.notify_all(); return;
    }
    try { worker_body(*state); }
    catch (const std::exception& e) {
        msg_Err(state->aout, "Audio worker exception: %s", e.what());
        std::lock_guard lock(state->mutex);
        const bool recover = state->initialized;
        state->init_result = E_FAIL; state->initialized = true; state->failed = true; state->condition.notify_all();
        if (recover) request_recovery(*state, E_FAIL);
    } catch (...) {
        std::lock_guard lock(state->mutex);
        const bool recover = state->initialized;
        state->init_result = E_FAIL; state->initialized = true; state->failed = true; state->condition.notify_all();
        if (recover) request_recovery(*state, E_FAIL);
    }
    CoUninitialize();
}
void stop(audio_output_t* aout) {
    auto& s = *aout->sys;
    { std::lock_guard lock(s.mutex); s.quit = true; s.condition.notify_all(); SetEvent(s.wake); }
    if (s.worker.joinable()) s.worker.join();
    std::lock_guard lock(s.mutex); s.queue.reset(); s.encoded_queue.reset(); s.has_data = false;
}
int start(audio_output_t* aout, audio_sample_format_t* fmt) {
    auto& s = *aout->sys;
    try {
        s.encoded = !AOUT_FMT_LINEAR(fmt);
        if (s.encoded) {
            EncodedCodec codec;
            switch (fmt->i_format) {
            case VLC_CODEC_A52: codec = EncodedCodec::Ac3; break;
            case VLC_CODEC_DTS: codec = var_GetBool(aout, "dtshd") || var_InheritBool(aout, "upmix3d-dts-hd") ? EncodedCodec::DtsHd : EncodedCodec::Dts; break;
            case VLC_CODEC_EAC3: codec = EncodedCodec::Eac3; break;
            case VLC_CODEC_TRUEHD: case VLC_CODEC_MLP: codec = EncodedCodec::TrueHd; break;
            case VLC_CODEC_SPDIFL: case VLC_CODEC_SPDIFB: codec = EncodedCodec::Spdif; break;
            default: return VLC_EGENERIC;
            }
            const auto policy = static_cast<Passthrough>(var_InheritInteger(aout, "upmix3d-passthrough"));
            if (policy == Passthrough::Core && codec == EncodedCodec::DtsHd) { codec = EncodedCodec::Dts; var_SetBool(aout, "dtshd", false); }
            if (!permits(policy, codec)) { msg_Dbg(aout, "Passthrough policy requests PCM decoding for %s", codec_name(codec).data()); return VLC_EGENERIC; }
            s.encoded_specification = encoded_spec(codec, fmt->i_rate, fmt->i_channels ? fmt->i_channels : 2);
            s.swap_encoded_words = fmt->i_format == VLC_CODEC_SPDIFB;
            s.order.clear(); s.requested = 0;
        } else {
            s.order = input_order(*fmt); if (s.order.empty() || s.order.size() > 9) return VLC_EGENERIC;
            Mask input = 0; for (auto c : s.order) input |= bit(c);
            s.settings = read_settings(aout, input);
            s.requested = resolve(input, layout_mask(string_setting(aout, "upmix3d-layout")), s.settings.downmix);
        }
        s.buffer_ms = string_setting(aout, "upmix3d-latency") == "low" ? 10u : 20u;
        s.transition_ms = var_InheritFloat(aout, "upmix3d-transition-ms");
        s.requested_route = var_InheritBool(aout, "upmix3d-exclusive") ? Route::Exclusive : Route::Shared;
        s.top_middle = var_InheritBool(aout, "upmix3d-top-middle");
        s.fold_back_center = string_setting(aout, "upmix3d-back-center") == "fold";
        s.compensation = {static_cast<int>(var_InheritInteger(aout, "upmix3d-spatial-offset-ms")),
            static_cast<int>(var_InheritInteger(aout, "upmix3d-shared-offset-ms")), static_cast<int>(var_InheritInteger(aout, "upmix3d-exclusive-offset-ms"))};
        { std::lock_guard lock(s.mutex);
            // A start long after the last failure is a new stream, which tries the configured route again.
            if (std::chrono::steady_clock::now() - s.failed_at > std::chrono::seconds(5)) { s.failures = 0; s.force_shared = false; }
            s.quit = s.initialized = s.failed = s.paused = s.has_data = s.drain = s.reset_requested = false;
            s.device_pending = 0; s.clock_running = false; s.clock_sampled = std::chrono::steady_clock::now();
            s.processing_pending = s.tail_pending = 0; s.volume_dirty = true;
        }
        if (!s.wake) return VLC_EGENERIC;
        s.worker = std::thread(worker_entry, &s);
        std::unique_lock lock(s.mutex); s.condition.wait(lock, [&] { return s.initialized; });
        if (FAILED(s.init_result) || s.failed) {
            lock.unlock(); stop(aout);
            if (s.encoded) notify_once(s, format_text(tr("Bitstream output of %s is unavailable on this output device, so VLC decodes it instead. Check that no other application is using the device, that exclusive control is allowed in the device properties (Advanced tab), and that the AV receiver supports the format. To always decode in VLC, set Bitstream passthrough to Off."),
                codec_name(s.encoded_specification.codec).data()));
            return VLC_EGENERIC;
        }
        if (s.encoded) {
            fmt->i_format = s.swap_encoded_words ? VLC_CODEC_SPDIFB : VLC_CODEC_SPDIFL;
            fmt->i_rate = s.encoded_specification.converter_rate;
            fmt->i_bytes_per_frame = s.encoded_specification.block_align(); fmt->i_frame_length = 1;
        } else { fmt->i_format = VLC_CODEC_FL32; fmt->i_rate = s.rate; }
        aout_FormatPrepare(fmt);
        return VLC_SUCCESS;
    } catch (const std::exception& e) { msg_Err(aout, "Cannot start upmix3d: %s", e.what()); if (s.worker.joinable()) stop(aout); return VLC_EGENERIC; }
}
void play(audio_output_t* aout, block_t* block) {
    std::unique_ptr<block_t, decltype(&block_Release)> owned(block, block_Release);
    auto& s = *aout->sys;
    if (s.encoded) {
        std::unique_lock lock(s.mutex);
        if (!s.encoded_queue || block->i_buffer % s.encoded_specification.block_align() || block->i_buffer > s.encoded_queue->capacity()) {
            msg_Err(aout, "Invalid IEC 61937 block size"); return;
        }
        s.condition.wait(lock, [&] { return s.quit || s.failed || s.encoded_queue->available() >= block->i_buffer; });
        if (s.quit || s.failed) return;
        s.encoded_queue->push(std::span(block->p_buffer, block->i_buffer), s.swap_encoded_words);
        s.has_data = true; SetEvent(s.wake); return;
    }
    const auto count = s.order.size();
    if (!count || block->i_nb_samples > block->i_buffer / (sizeof(float) * count)) { msg_Err(aout, "Invalid PCM block size"); return; }
    const float* samples = reinterpret_cast<const float*>(block->p_buffer);
    std::unique_lock lock(s.mutex);
    for (std::size_t i = 0; i < block->i_nb_samples; ++i) {
        s.condition.wait(lock, [&] { return s.quit || s.failed || (s.queue && s.queue->available()); });
        if (s.quit || s.failed) return;
        Frame input{}; std::copy_n(samples + i * count, count, input.begin()); s.queue->push(input);
        s.has_data = true;
        if (i % 256 == 0) SetEvent(s.wake);
    }
    SetEvent(s.wake);
}
int time_get(audio_output_t* aout, vlc_tick_t* delay) {
    auto& s = *aout->sys; std::lock_guard lock(s.mutex);
    if ((!s.queue && !s.encoded_queue) || !s.has_data || s.failed || !s.rate) return VLC_EGENERIC;
    const auto elapsed = s.clock_running && !s.paused ? std::chrono::duration<double>(std::chrono::steady_clock::now() - s.clock_sampled).count() : 0.;
    const auto seconds = (s.encoded ? s.encoded_specification.seconds(s.encoded_queue->size()) :
        static_cast<double>(s.queue->size() + s.processing_pending) / s.rate) + std::max(0., s.device_pending - elapsed);
    *delay = static_cast<vlc_tick_t>(seconds * CLOCK_FREQ) + static_cast<vlc_tick_t>(s.compensation[static_cast<unsigned>(s.actual_route)]) * 1000;
    return VLC_SUCCESS;
}
void pause(audio_output_t* aout, bool paused, vlc_tick_t) {
    auto& s = *aout->sys; std::lock_guard lock(s.mutex); s.paused = paused; SetEvent(s.wake);
}
void flush(audio_output_t* aout, bool wait) {
    auto& s = *aout->sys; std::unique_lock lock(s.mutex);
    if (!s.worker.joinable() || s.failed || s.quit) return;
    if (wait && s.has_data) {
        s.drain = true; SetEvent(s.wake);
        s.condition.wait(lock, [&] { return !s.drain || s.failed || s.quit; });
    } else {
        const auto generation = ++s.reset_id; s.reset_requested = true; SetEvent(s.wake);
        s.condition.wait(lock, [&] { return s.completed_reset >= generation || s.failed || s.quit; });
    }
}
int volume_set(audio_output_t* aout, float volume) {
    if (!std::isfinite(volume) || volume < 0) return VLC_EGENERIC;
    auto& s = *aout->sys;
    { std::lock_guard lock(s.mutex); s.volume = volume; s.volume_dirty = true; SetEvent(s.wake); }
    aout_VolumeReport(aout, volume); return VLC_SUCCESS;
}
int mute_set(audio_output_t* aout, bool muted) {
    auto& s = *aout->sys;
    { std::lock_guard lock(s.mutex); s.muted = muted; s.volume_dirty = true; SetEvent(s.wake); }
    aout_MuteReport(aout, muted); return VLC_SUCCESS;
}
int device_select(audio_output_t* aout, const char* id) {
    auto& s = *aout->sys;
    { std::lock_guard lock(s.mutex); s.device = id ? id : ""; s.force_shared = false; s.failures = 0; }
    aout_DeviceReport(aout, id); aout_RestartRequest(aout, AOUT_RESTART_OUTPUT); return VLC_SUCCESS;
}
int open(vlc_object_t* object) {
    auto* aout = reinterpret_cast<audio_output_t*>(object);
    // VLC opens the output module once at startup, which applies a reset requested in the previous session.
    if (var_InheritBool(aout, "upmix3d-reset-settings")) reset_settings(aout);
    if (!core_free()) { msg_Err(aout, "Cannot resolve the libvlccore string allocator; refusing unsafe CRT ownership"); return VLC_EGENERIC; }
    try { aout->sys = new aout_sys_t(aout); }
    catch (...) { return VLC_ENOMEM; }
    aout->start = start; aout->stop = stop; aout->play = play; aout->time_get = time_get;
    aout->pause = pause; aout->flush = flush; aout->volume_set = volume_set; aout->mute_set = mute_set; aout->device_select = device_select;
    aout_HotplugReport(aout, "", "Windows default output");
    try {
        std::vector<DeviceInfo> devices;
        std::thread enumerate([&] {
            if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
                try { devices = enumerate_devices(); } catch (...) {}
                CoUninitialize();
            }
        });
        enumerate.join();
        for (const auto& device : devices) aout_HotplugReport(aout, device.id.c_str(), device.name.c_str());
    } catch (...) { msg_Warn(aout, "Initial device enumeration is unavailable"); }
    aout_VolumeReport(aout, 1.f); aout_MuteReport(aout, false);
    return VLC_SUCCESS;
}
void close(vlc_object_t* object) {
    auto* aout = reinterpret_cast<audio_output_t*>(object);
    if (aout->sys->worker.joinable()) stop(aout);
    delete aout->sys; aout->sys = nullptr;
}
const char* const layout_values[]{"3.1","4.0","5.1","7.1","2.1.2","3.1.2","5.1.2","5.1.4","7.1.2","7.1.4"};
const char* const layout_labels[]{"3.1","4.0","5.1","7.1","2.1.2 (4.1)","3.1.2 (5.1)","5.1.2 (7.1)","5.1.4 (9.1)","7.1.2 (9.1)","7.1.4 (11.1)"};
const char* const preset_names[]{"movie","music","wide","custom"};
const char* const preset_labels[]{tr("Movie"),tr("Music"),tr("Music (wide)"),tr("Custom")};
const char* const input_preset_values[]{"inherit","movie","music","wide","custom"};
const char* const input_preset_labels[]{tr("Use global preset"),tr("Movie"),tr("Music"),tr("Music (wide)"),tr("Custom")};
const char* const method_values[]{"matrix","frequency"};
const char* const method_labels[]{tr("Matrix (light)"),tr("Frequency analysis (better separation)")};
const char* const latency_values[]{"low","standard","quality"};
const char* const latency_labels[]{tr("Low latency"),tr("Standard"),tr("High quality")};
const char* const protection_values[]{"fixed","normalize","limiter"};
const char* const protection_labels[]{tr("Fixed -6 dB attenuation"),tr("Peak normalization (gain only decreases)"),tr("Limiter")};
const char* const downmix_values[]{"standard","custom"};
const char* const downmix_labels[]{tr("Standard (-3 dB center and surround)"),tr("Custom")};
const int window_values[]{1024,2048,4096};
const char* const window_labels[]{"1024","2048","4096"};
const int hop_values[]{4,2};
const char* const hop_labels[]{tr("Window / 4"),tr("Window / 2")};
const int passthrough_values[]{0,1,2};
const char* const passthrough_labels[]{tr("Off (decode in VLC)"),tr("Dolby Digital and DTS only"),tr("All supported formats")};
const char* const back_center_values[]{"dynamic","fold"};
const char* const back_center_labels[]{tr("Separate sound object"),tr("Mix into the back speakers")};
}

vlc_module_begin()
    set_shortname("upmix3d")
    set_description(tr("upmix3d spatial upmix output"))
    set_capability("audio output", 0)
    add_shortcut("upmix3d")
    set_category(CAT_AUDIO)
    set_subcategory(SUBCAT_AUDIO_AOUT)
    set_callbacks(open, close)
    add_string("upmix3d-layout", "5.1.2", tr("Output layout"), tr("Your speaker layout. Height speakers require Windows spatial sound."), false)
        change_string_list(layout_values, layout_labels)
    add_bool("upmix3d-reset-settings", false, tr("Reset all settings at next VLC start"),
        tr("Returns every upmix3d setting to its default value when VLC next starts. Turns itself off after use."), false)
    add_bool("upmix3d-downmix", false, tr("Downmix to the selected layout"), tr("When off, source channels that are not in the selected layout are still played."), false)
    add_integer("upmix3d-passthrough", 2, tr("Bitstream passthrough"), tr("Sends Dolby and DTS audio to the AV receiver unchanged. VLC's volume does not apply to it."), true)
        change_integer_list(passthrough_values, passthrough_labels)
    add_bool("upmix3d-dts-hd", false, tr("Send DTS as DTS-HD"), tr("Sends DTS audio in the DTS-HD format. DTS is sent instead when this is not possible."), true)
    add_string("upmix3d-preset", "music", tr("Preset"), tr("Changing an individual sound setting switches to Custom."), false)
        change_string_list(preset_names, preset_labels)
    add_string("upmix3d-preset-stereo", "inherit", tr("Preset for mono/stereo input"), nullptr, true)
        change_string_list(input_preset_values, input_preset_labels)
    add_string("upmix3d-preset-multichannel", "inherit", tr("Preset for multichannel input"), nullptr, true)
        change_string_list(input_preset_values, input_preset_labels)
    add_string("upmix3d-preset-1-0", "inherit", tr("Preset for 1.0 input"), nullptr, true) change_string_list(input_preset_values, input_preset_labels)
    add_string("upmix3d-preset-2-0", "inherit", tr("Preset for 2.0 input"), nullptr, true) change_string_list(input_preset_values, input_preset_labels)
    add_string("upmix3d-preset-2-1", "inherit", tr("Preset for 2.1 input"), nullptr, true) change_string_list(input_preset_values, input_preset_labels)
    add_string("upmix3d-preset-3-0", "inherit", tr("Preset for 3.0 input"), nullptr, true) change_string_list(input_preset_values, input_preset_labels)
    add_string("upmix3d-preset-3-1", "inherit", tr("Preset for 3.1 input"), nullptr, true) change_string_list(input_preset_values, input_preset_labels)
    add_string("upmix3d-preset-4-0", "inherit", tr("Preset for 4.0 input"), nullptr, true) change_string_list(input_preset_values, input_preset_labels)
    add_string("upmix3d-preset-4-1", "inherit", tr("Preset for 4.1 input"), nullptr, true) change_string_list(input_preset_values, input_preset_labels)
    add_string("upmix3d-preset-5-0", "inherit", tr("Preset for 5.0 input"), nullptr, true) change_string_list(input_preset_values, input_preset_labels)
    add_string("upmix3d-preset-5-1", "inherit", tr("Preset for 5.1 input"), nullptr, true) change_string_list(input_preset_values, input_preset_labels)
    add_string("upmix3d-preset-6-0", "inherit", tr("Preset for 6.0 input"), nullptr, true) change_string_list(input_preset_values, input_preset_labels)
    add_string("upmix3d-preset-6-1", "inherit", tr("Preset for 6.1 input"), nullptr, true) change_string_list(input_preset_values, input_preset_labels)
    add_string("upmix3d-preset-7-0", "inherit", tr("Preset for 7.0 input"), nullptr, true) change_string_list(input_preset_values, input_preset_labels)
    add_string("upmix3d-preset-7-1", "inherit", tr("Preset for 7.1 input"), nullptr, true) change_string_list(input_preset_values, input_preset_labels)
    add_string("upmix3d-preset-8-1", "inherit", tr("Preset for 8.1 input"), nullptr, true) change_string_list(input_preset_values, input_preset_labels)
    add_string("upmix3d-preset-other", "inherit", tr("Preset for other inputs"), nullptr, true) change_string_list(input_preset_values, input_preset_labels)
    add_string("upmix3d-controls-baseline", "", "Control baseline", nullptr, true) change_private()
    add_string("upmix3d-custom-profiles", "", "Custom preset values", nullptr, true) change_private()
    add_bool("upmix3d-save-settings", true, tr("Remember preset changes"), tr("Keeps custom preset values after VLC restarts."), true)
    add_bool("upmix3d-exclusive", false, tr("Exclusive mode for layouts without heights"),
        tr("For layouts without height speakers, VLC takes over the output device and sends audio to it directly instead of through the Windows mixer. Other apps cannot play sound meanwhile. Layouts with height speakers always play through Windows spatial sound."), true)
    add_bool("upmix3d-top-middle", false, tr("Height speakers directly overhead"),
        tr("Turn on when the two height speakers of a .2 layout are on the ceiling above the listening position. Leave off for speakers high on the front wall or upward-firing speakers."), true)
    add_float_with_range("upmix3d-transition-ms", 5, 0, 20, tr("Fade at start, seek and pause (ms)"), tr("Prevents clicks when playback starts, seeks or pauses."), true)
    add_string("upmix3d-back-center", "dynamic", tr("Back center"), tr("How the back center channel is played when the device has no back center speaker."), true)
        change_string_list(back_center_values, back_center_labels)
    add_string("upmix3d-latency", "standard", tr("Latency mode"), nullptr, true)
        change_string_list(latency_values, latency_labels)
    add_string("upmix3d-method", "frequency", tr("Upmix method"), nullptr, true)
        change_string_list(method_values, method_labels)
    add_integer("upmix3d-window", 2048, tr("Frequency analysis window"), nullptr, true)
        change_integer_list(window_values, window_labels)
    add_integer("upmix3d-hop-divisor", 4, tr("Frequency analysis step"), nullptr, true)
        change_integer_list(hop_values, hop_labels)
    add_float_with_range("upmix3d-center", 50, 0, 100, tr("Center extraction (%)"), tr("Changing this value selects a custom preset."), true)
    add_float_with_range("upmix3d-width", 3, 0, 7, tr("Center width"), tr("Changing this value selects a custom preset."), true)
    add_float_with_range("upmix3d-dimension", 0, -3, 3, tr("Front/back dimension"), tr("Changing this value selects a custom preset."), true)
    add_float_with_range("upmix3d-side-db", -4, -18, 0, tr("Generated side level (dB)"), tr("Changing this value selects a custom preset."), true)
    add_float_with_range("upmix3d-back-db", -7, -18, 0, tr("Generated back level (dB)"), tr("Changing this value selects a custom preset."), true)
    add_float_with_range("upmix3d-surround-ms", 10, 0, 25, tr("Surround delay (ms)"), tr("Changing this value selects a custom preset."), true)
    add_float_with_range("upmix3d-decorrelation", 100, 0, 200, tr("Decorrelation (%)"), tr("Changing this value selects a custom preset."), true)
    add_bool("upmix3d-dialogue-protect", true, tr("Keep dialogue out of surround and height speakers"), tr("Reduces the speech range in the created surround and height sound. Instruments in the same range are reduced too."), true)
    add_float_with_range("upmix3d-height-db", -9, -18, 0, tr("Height level (dB)"), tr("Changing this value selects a custom preset."), true)
    add_float_with_range("upmix3d-height-hz", 3000, 1000, 5000, tr("Height high-pass cutoff (Hz)"), tr("Changing this value selects a custom preset."), true)
    add_float_with_range("upmix3d-height-ms", 0, 0, 20, tr("Height delay (ms)"), tr("Changing this value selects a custom preset."), true)
    add_float_with_range("upmix3d-height-balance", 0, -1, 1, tr("Height front/back balance"), tr("-1 front, 0 equal power, +1 back."), true)
    add_string("upmix3d-downmix-coefficients", "standard", tr("Downmix coefficients"), nullptr, true)
        change_string_list(downmix_values, downmix_labels)
    add_float_with_range("upmix3d-center-mix-db", -3, -6, 0, tr("Center mix level (dB)"), nullptr, true)
    add_float_with_range("upmix3d-surround-mix-db", -3, -6, 0, tr("Surround mix level (dB)"), nullptr, true)
    add_float_with_range("upmix3d-height-mix-db", -3, -6, 0, tr("Height mix level (dB)"), nullptr, true)
    add_bool("upmix3d-mix-lfe", false, tr("Mix LFE into the speakers when the layout has no subwoofer"), nullptr, true)
    add_float_with_range("upmix3d-lfe-mix-db", -6, -18, 0, tr("LFE mix level (dB)"), nullptr, true)
    add_string("upmix3d-protection", "limiter", tr("Level protection"),
        tr("Fixed always lowers the processed sound by 6 dB. Normalization lowers the volume to fit the loudest sound and keeps it until playback restarts or you seek. The limiter lowers the volume only at loud moments and then restores it."), true)
        change_string_list(protection_values, protection_labels)
    add_bool("upmix3d-true-peak", true, tr("Detect peaks between samples (true peak)"), tr("Normalization and the limiter also catch peaks that occur between samples."), true)
    add_float_with_range("upmix3d-threshold-db", -1, -6, 0, tr("Limiter threshold (dB)"), tr("Peak level that normalization and the limiter keep the output below."), true)
    add_float_with_range("upmix3d-gain-db", 0, -12, 6, tr("Overall gain (dB)"), nullptr, true)
    add_float_with_range("upmix3d-lfe-db", 0, -12, 6, tr("LFE level (dB)"), nullptr, true)
    add_integer_with_range("upmix3d-spatial-offset-ms", 0, -500, 500, tr("Latency correction for spatial sound (ms)"), tr("Set this to the delay of your AV receiver or TV if the sound lags behind the picture."), true)
    add_integer_with_range("upmix3d-shared-offset-ms", 0, -500, 500, tr("Latency correction for shared output (ms)"), tr("Set this to the delay of your AV receiver or TV if the sound lags behind the picture."), true)
    add_integer_with_range("upmix3d-exclusive-offset-ms", 0, -500, 500, tr("Latency correction for exclusive output (ms)"), tr("Set this to the delay of your AV receiver or TV if the sound lags behind the picture."), true)
vlc_module_end()
