#include <initguid.h>
#include "upmix3d/windows_audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ks.h>
#include <ksmedia.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <sstream>

namespace upmix3d {
using Microsoft::WRL::ComPtr;
namespace {
constexpr unsigned idx(Channel c) { return static_cast<unsigned>(c); }
constexpr std::array types{
    AudioObjectType_FrontLeft, AudioObjectType_FrontRight, AudioObjectType_FrontCenter,
    AudioObjectType_LowFrequency, AudioObjectType_SideLeft, AudioObjectType_SideRight,
    AudioObjectType_BackLeft, AudioObjectType_BackRight, AudioObjectType_BackCenter,
    AudioObjectType_TopFrontLeft, AudioObjectType_TopFrontRight, AudioObjectType_TopBackLeft, AudioObjectType_TopBackRight};
constexpr std::array<DWORD, channel_count> speakers{
    SPEAKER_FRONT_LEFT, SPEAKER_FRONT_RIGHT, SPEAKER_FRONT_CENTER, SPEAKER_LOW_FREQUENCY,
    SPEAKER_SIDE_LEFT, SPEAKER_SIDE_RIGHT, SPEAKER_BACK_LEFT, SPEAKER_BACK_RIGHT, SPEAKER_BACK_CENTER,
    SPEAKER_TOP_FRONT_LEFT, SPEAKER_TOP_FRONT_RIGHT, SPEAKER_TOP_BACK_LEFT, SPEAKER_TOP_BACK_RIGHT};
constexpr std::array wave_channels{Channel::FL, Channel::FR, Channel::FC, Channel::LFE,
    Channel::BL, Channel::BR, Channel::BC, Channel::SL, Channel::SR};
constexpr std::array<std::array<float, 3>, channel_count> positions{{
    {-.7071f,0,-.7071f},{.7071f,0,-.7071f},{0,0,-1},{0,0,0},
    {-1,0,0},{1,0,0},{-.7071f,0,.7071f},{.7071f,0,.7071f},{0,0,1},
    {-.5f,.7071f,-.5f},{.5f,.7071f,-.5f},{-.5f,.7071f,.5f},{.5f,.7071f,.5f}}};
std::string error_text(const char* what, HRESULT hr) {
    std::ostringstream s; s << what << " (HRESULT 0x" << std::hex << static_cast<unsigned long>(hr) << ')'; return s.str();
}
WAVEFORMATEXTENSIBLE float_format(Mask mask, unsigned rate) {
    WAVEFORMATEXTENSIBLE f{};
    f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    f.Format.nChannels = static_cast<WORD>(channels(mask).size());
    f.Format.nSamplesPerSec = rate; f.Format.wBitsPerSample = 32;
    f.Format.nBlockAlign = f.Format.nChannels * 4;
    f.Format.nAvgBytesPerSec = rate * f.Format.nBlockAlign;
    f.Format.cbSize = sizeof(f) - sizeof(WAVEFORMATEX);
    f.Samples.wValidBitsPerSample = 32; f.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    for (auto c : channels(mask)) f.dwChannelMask |= speakers[idx(c)];
    return f;
}
Mask wave_mask(const WAVEFORMATEX* f) {
    if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE && f->cbSize >= 22) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f);
        Mask mask = 0;
        for (unsigned i = 0; i < 9; ++i) if (ext->dwChannelMask & speakers[i]) mask |= Mask{1} << i;
        if (channels(mask).size() == f->nChannels && f->nChannels <= 8) return mask;
    }
    if (f->nChannels == 1) return bit(Channel::FC);
    if (f->nChannels == 2) return fronts;
    return 0;
}
}
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n) return {};
    std::wstring out(n, L'\0'); MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), n); return out;
}
std::string narrow(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr); return out;
}
std::vector<DeviceInfo> enumerate_devices() {
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) return {};
    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection))) return {};
    UINT count = 0; collection->GetCount(&count);
    std::vector<DeviceInfo> result;
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device; if (FAILED(collection->Item(i, &device))) continue;
        LPWSTR id = nullptr; if (FAILED(device->GetId(&id))) continue;
        DeviceInfo info{narrow(id), narrow(id)}; CoTaskMemFree(id);
        ComPtr<IPropertyStore> properties;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
            PROPVARIANT value; PropVariantInit(&value);
            if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR) info.name = narrow(value.pwszVal);
            PropVariantClear(&value);
        }
        result.push_back(std::move(info));
    }
    return result;
}
AudioSink::AudioSink(Log log) : log_(std::move(log)), event_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}
AudioSink::~AudioSink() { clear(); if (event_) CloseHandle(event_); }
void AudioSink::clear() {
    if (updating_ && stream_) stream_->EndUpdatingAudioObjects();
    updating_ = false;
    if (stream_) stream_->Stop(); if (client_) client_->Stop();
    for (auto& o : objects_) o.Reset();
    clock_.Reset(); volume_.Reset(); render_.Reset(); client_.Reset(); stream_.Reset(); spatial_.Reset();
    buffers_.fill(nullptr); interleaved_ = nullptr; routing_ = {}; written_ = 0; clock_frequency_ = 0; previous_failed_ = 0;
    encoded_ = false; encoded_align_ = 0; running_ = false;
    ResetEvent(event_);
}
HRESULT AudioSink::open(const std::string& id, Mask requested, bool top_middle, unsigned buffer_ms, bool fold_back_center) {
    if (!event_) return HRESULT_FROM_WIN32(GetLastError());
    clear(); device_.Reset(); buffer_ms_ = std::clamp(buffer_ms, 10u, 100u); fold_back_center_ = fold_back_center;
    HRESULT hr = select_device(id);
    if (FAILED(hr)) return hr;
    // Heights exist only in Windows spatial sound; the Windows speaker setup ends at 7.1.
    if (requested & heights) {
        hr = open_spatial(requested, top_middle);
        if (SUCCEEDED(hr)) return hr;
        log_(error_text("Spatial output unavailable; falling back to shared WASAPI", hr)); clear();
    }
    return open_wasapi(requested);
}
HRESULT AudioSink::select_device(const std::string& id) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) return hr;
    if (id.empty()) hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device_);
    else {
        const auto wide = widen(id); if (wide.empty()) return E_INVALIDARG;
        hr = enumerator->GetDevice(wide.c_str(), &device_);
    }
    return hr;
}
std::string AudioSink::device_id() const {
    LPWSTR id = nullptr;
    if (!device_ || FAILED(device_->GetId(&id))) return {};
    const auto result = narrow(id); CoTaskMemFree(id); return result;
}
HRESULT AudioSink::open_encoded(const std::string& id, const EncodedSpec& spec) {
    if (!event_) return E_OUTOFMEMORY;
    clear(); device_.Reset();
    HRESULT hr = select_device(id); if (FAILED(hr)) return hr;
    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client_.GetAddressOf()));
    if (FAILED(hr)) return hr;
    WAVEFORMATEXTENSIBLE_IEC61937 format{};
    auto& wave = format.FormatExt;
    wave.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wave.Format.nChannels = static_cast<WORD>(spec.carrier_channels);
    wave.Format.nSamplesPerSec = spec.carrier_rate;
    wave.Format.wBitsPerSample = wave.Samples.wValidBitsPerSample = 16;
    wave.Format.nBlockAlign = static_cast<WORD>(spec.block_align());
    wave.Format.nAvgBytesPerSec = spec.bytes_per_second();
    const bool hd = spec.codec == EncodedCodec::Eac3 || spec.codec == EncodedCodec::DtsHd || spec.codec == EncodedCodec::TrueHd;
    wave.Format.cbSize = static_cast<WORD>((hd ? sizeof(format) : sizeof(wave)) - sizeof(WAVEFORMATEX));
    wave.dwChannelMask = spec.carrier_channels == 8 ? KSAUDIO_SPEAKER_7POINT1 :
        spec.codec == EncodedCodec::Eac3 ? KSAUDIO_SPEAKER_5POINT1 : SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    switch (spec.codec) {
    case EncodedCodec::Ac3: case EncodedCodec::Spdif: wave.SubFormat = KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL; break;
    case EncodedCodec::Dts: wave.SubFormat = spec.carrier_rate < 48000 ? KSDATAFORMAT_SUBTYPE_PCM : KSDATAFORMAT_SUBTYPE_IEC61937_DTS; break;
    case EncodedCodec::Eac3: wave.SubFormat = KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL_PLUS; break;
    case EncodedCodec::DtsHd: wave.SubFormat = KSDATAFORMAT_SUBTYPE_IEC61937_DTS_HD; break;
    case EncodedCodec::TrueHd: wave.SubFormat = KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_MLP; break;
    }
    format.dwEncodedSamplesPerSec = spec.source_rate;
    format.dwEncodedChannelCount = spec.source_channels;
    hr = client_->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &wave.Format, nullptr);
    if (hr != S_OK) return FAILED(hr) ? hr : AUDCLNT_E_UNSUPPORTED_FORMAT;
    encoded_ = true; encoded_align_ = spec.block_align(); rate_ = spec.carrier_rate; mask_ = 0;
    REFERENCE_TIME period = 0, minimum = 0;
    hr = client_->GetDevicePeriod(&period, &minimum); if (FAILED(hr)) return hr;
    return initialize_client(&wave.Format, true, std::max<REFERENCE_TIME>(minimum, 200000), AUDCLNT_STREAMFLAGS_EVENTCALLBACK);
}
void AudioSink::set_fold_targets(Channel source, Mask available) {
    if (source == Channel::LFE) return;
    const unsigned s = idx(source);
    auto pair = [&](Channel l, Channel r) {
        if ((available & (bit(l) | bit(r))) == (bit(l) | bit(r))) {
            routing_[s][idx(l)] = routing_[s][idx(r)] = .70710678f; return true;
        }
        return false;
    };
    if (source == Channel::BC && (pair(Channel::BL, Channel::BR) || pair(Channel::SL, Channel::SR))) return;
    const bool right = source == Channel::FR || source == Channel::SR || source == Channel::BR || source == Channel::TFR || source == Channel::TBR;
    Channel preferred = right ? Channel::FR : Channel::FL;
    if (source == Channel::TBL || source == Channel::TBR || source == Channel::BL || source == Channel::BR)
        preferred = right ? Channel::SR : Channel::SL;
    if (available & bit(preferred)) routing_[s][idx(preferred)] = .70710678f;
    else if (!pair(Channel::FL, Channel::FR) && (available & bit(Channel::FC))) routing_[s][idx(Channel::FC)] = .70710678f;
}
HRESULT AudioSink::open_spatial(Mask requested, bool top_middle) {
    HRESULT hr = device_->Activate(__uuidof(ISpatialAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(spatial_.GetAddressOf()));
    if (FAILED(hr)) return hr;
    UINT32 maximum = 0; hr = spatial_->GetMaxDynamicObjectCount(&maximum);
    if (FAILED(hr) || !maximum) return FAILED(hr) ? hr : AUDCLNT_E_UNSUPPORTED_FORMAT;
    AudioObjectType native{}; hr = spatial_->GetNativeStaticObjectTypeMask(&native); if (FAILED(hr)) return hr;
    if (fold_back_center_ && !(native & AudioObjectType_BackCenter) && (requested & bit(Channel::BC))) {
        requested &= ~bit(Channel::BC);
        log_("Back center has no native static position; folding it into the bed by configuration");
    }
    ComPtr<IAudioFormatEnumerator> formats; hr = spatial_->GetSupportedAudioObjectFormatEnumerator(&formats); if (FAILED(hr)) return hr;
    UINT32 count = 0; hr = formats->GetCount(&count); if (FAILED(hr)) return hr;
    const WAVEFORMATEX* format = nullptr;
    for (UINT32 i = 0; i < count; ++i) {
        WAVEFORMATEX* candidate = nullptr; hr = formats->GetFormat(i, &candidate); if (FAILED(hr)) continue;
        const bool floating = candidate->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
            (candidate->wFormatTag == WAVE_FORMAT_EXTENSIBLE && candidate->cbSize >= 22 &&
             reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(candidate)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        if (floating && candidate->nChannels == 1 && candidate->wBitsPerSample == 32) { format = candidate; break; }
    }
    if (!format) return AUDCLNT_E_UNSUPPORTED_FORMAT;
    rate_ = format->nSamplesPerSec; mask_ = requested; route_ = Route::Spatial; top_middle_ = top_middle;
    active_channels_ = channels(mask_);
    AudioObjectType static_mask = AudioObjectType_None;
    UINT32 dynamic_count = 0;
    for (auto c : channels(requested)) {
        const unsigned i = idx(c);
        const bool force_dynamic = top_middle && !(requested & top_backs) && (bit(c) & top_fronts);
        object_types_[i] = !force_dynamic && (native & types[i]) ? types[i] : AudioObjectType_Dynamic;
        if (object_types_[i] == AudioObjectType_Dynamic) {
            if (dynamic_count < maximum) ++dynamic_count;
            else if (native & types[i]) object_types_[i] = types[i];
            else object_types_[i] = AudioObjectType_None;
        }
        if (object_types_[i] != AudioObjectType_Dynamic) static_mask |= object_types_[i];
        routing_[i][i] = 1.f;
    }
    SpatialAudioObjectRenderStreamActivationParams params{};
    params.ObjectFormat = format; params.StaticObjectTypeMask = static_mask;
    params.MinDynamicObjectCount = 0; params.MaxDynamicObjectCount = dynamic_count;
    params.Category = AudioCategory_Media; params.EventHandle = event_;
    PROPVARIANT activation; PropVariantInit(&activation); activation.vt = VT_BLOB;
    activation.blob.cbSize = sizeof(params); activation.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
    hr = spatial_->ActivateSpatialAudioStream(&activation, __uuidof(ISpatialAudioObjectRenderStream), reinterpret_cast<void**>(stream_.GetAddressOf()));
    if (FAILED(hr)) return hr;
    hr = stream_->GetService(IID_PPV_ARGS(&clock_)); if (FAILED(hr)) return hr;
    hr = clock_->GetFrequency(&clock_frequency_); if (FAILED(hr) || !clock_frequency_) return E_FAIL;
    stream_->GetService(IID_PPV_ARGS(&volume_));
    return S_OK;
}
HRESULT AudioSink::open_wasapi(Mask requested) {
    HRESULT hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client_.GetAddressOf()));
    if (FAILED(hr)) return hr;
    WAVEFORMATEX* mix = nullptr; hr = client_->GetMixFormat(&mix); if (FAILED(hr)) return hr;
    const Mask native = wave_mask(mix); rate_ = mix->nSamplesPerSec; CoTaskMemFree(mix);
    mask_ = requested & bed_71;
    if (!(mask_ & fronts)) mask_ |= fronts;
    auto format = float_format(mask_, rate_);
    WAVEFORMATEX* closest = nullptr;
    hr = client_->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &format.Format, &closest);
    CoTaskMemFree(closest);
    if (hr != S_OK) {
        if (!native) return AUDCLNT_E_UNSUPPORTED_FORMAT;
        mask_ = native; format = float_format(mask_, rate_);
        log_("Shared WASAPI uses the device channel layout; unavailable positions will be folded down");
    }
    const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    hr = initialize_client(&format.Format, false, static_cast<REFERENCE_TIME>(buffer_ms_) * 10000, flags);
    if (FAILED(hr)) return hr;
    wave_order_.clear(); for (auto c : wave_channels) if (mask_ & bit(c)) wave_order_.push_back(c);
    return S_OK;
}
HRESULT AudioSink::initialize_client(const WAVEFORMATEX* format, bool exclusive, REFERENCE_TIME duration, DWORD flags) {
    const auto mode = exclusive ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED;
    HRESULT hr = client_->Initialize(mode, flags, duration, exclusive ? duration : 0, format, nullptr);
    if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED && exclusive) {
        UINT32 aligned = 0; client_->GetBufferSize(&aligned); client_.Reset();
        hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client_.GetAddressOf()));
        if (FAILED(hr)) return hr;
        duration = static_cast<REFERENCE_TIME>(10000000.0 * aligned / rate_ + .5);
        hr = client_->Initialize(mode, flags, duration, duration, format, nullptr);
    }
    if (FAILED(hr)) return hr;
    hr = client_->SetEventHandle(event_); if (FAILED(hr)) return hr;
    hr = client_->GetBufferSize(&capacity_); if (FAILED(hr)) return hr;
    hr = client_->GetService(IID_PPV_ARGS(&render_)); if (FAILED(hr)) return hr;
    hr = client_->GetService(IID_PPV_ARGS(&clock_)); if (FAILED(hr)) return hr;
    hr = clock_->GetFrequency(&clock_frequency_); if (FAILED(hr) || !clock_frequency_) return E_FAIL;
    if (!encoded_) client_->GetService(IID_PPV_ARGS(&volume_));
    route_ = exclusive ? Route::Exclusive : Route::Shared;
    return S_OK;
}
HRESULT AudioSink::start() { HRESULT hr = stream_ ? stream_->Start() : client_->Start(); if (SUCCEEDED(hr)) running_ = true; return hr; }
HRESULT AudioSink::stop() { HRESULT hr = stream_ ? stream_->Stop() : client_->Stop(); if (SUCCEEDED(hr)) running_ = false; return hr; }
HRESULT AudioSink::reset() {
    stop(); HRESULT hr = stream_ ? stream_->Reset() : client_->Reset();
    written_ = 0;
    if (stream_) {
        for (auto& o : objects_) o.Reset();
        routing_ = {}; for (auto c : active_channels_) routing_[idx(c)][idx(c)] = 1;
    }
    return hr;
}
HRESULT AudioSink::begin(UINT32& frames) {
    frames = 0;
    if (!stream_) {
        UINT32 padding = 0;
        if (route_ != Route::Exclusive) {
            HRESULT hr = client_->GetCurrentPadding(&padding); if (FAILED(hr)) return hr;
        }
        frames = capacity_ - std::min(capacity_, padding);
        if (!frames) return S_OK;
        BYTE* data = nullptr; HRESULT hr = render_->GetBuffer(frames, &data);
        if (SUCCEEDED(hr)) interleaved_ = data;
        return hr;
    }
    UINT32 available = 0; HRESULT hr = stream_->BeginUpdatingAudioObjects(&available, &frames);
    if (FAILED(hr)) return hr;
    updating_ = true; buffers_.fill(nullptr);
    Mask active = 0, failed = 0;
    for (auto c : active_channels_) {
        const unsigned i = idx(c);
        if (object_types_[i] == AudioObjectType_None) { failed |= bit(c); continue; }
        if (objects_[i]) {
            BOOL active_object = FALSE;
            hr = objects_[i]->IsActive(&active_object);
            if (FAILED(hr) || !active_object) objects_[i].Reset();
        }
        if (!objects_[i]) {
            hr = stream_->ActivateSpatialAudioObject(object_types_[i], &objects_[i]);
            if (FAILED(hr)) { failed |= bit(c); continue; }
            if (object_types_[i] == AudioObjectType_Dynamic) {
                auto p = positions[i]; if (top_middle_ && !(mask_ & top_backs) && (bit(c) & top_fronts)) { p[1] = 1; p[2] = 0; }
                hr = objects_[i]->SetPosition(p[0], p[1], p[2]);
                if (FAILED(hr)) { stream_->EndUpdatingAudioObjects(); updating_ = false; return hr; }
            }
        }
        BYTE* data = nullptr; UINT32 bytes = 0; hr = objects_[i]->GetBuffer(&data, &bytes);
        if (FAILED(hr) || bytes < frames * sizeof(float)) { stream_->EndUpdatingAudioObjects(); updating_ = false; return FAILED(hr) ? hr : E_FAIL; }
        buffers_[i] = reinterpret_cast<float*>(data); std::memset(data, 0, bytes); active |= bit(c);
    }
    if (!active) { stream_->EndUpdatingAudioObjects(); updating_ = false; return E_FAIL; }
    if (failed && failed != previous_failed_) log_("Spatial objects are unavailable; folding their signals into the available bed");
    previous_failed_ = failed;
    for (auto c : active_channels_) {
        const unsigned i = idx(c); routing_[i] = {};
        if (failed & bit(c)) set_fold_targets(c, active);
        else routing_[i][i] = 1.f;
    }
    for (unsigned target = 0; target < channel_count; ++target) {
        float total = 0;
        for (unsigned source = 0; source < channel_count; ++source) total += std::abs(routing_[source][target]);
        routing_scale_[target] = 1.f / std::max(1.f, total);
    }
    return S_OK;
}
void AudioSink::write(UINT32 frame, const Frame& samples) {
    if (!stream_) {
        for (std::size_t c = 0; c < wave_order_.size(); ++c) {
            const float sample = samples[idx(wave_order_[c])];
            std::memcpy(interleaved_ + (frame * wave_order_.size() + c) * sizeof(float), &sample, sizeof(sample));
        }
    } else {
        for (unsigned target = 0; target < channel_count; ++target) if (buffers_[target]) {
            float value = 0; for (unsigned source = 0; source < channel_count; ++source) value += samples[source] * routing_[source][target];
            buffers_[target][frame] = std::clamp(value * routing_scale_[target], -1.f, 1.f);
        }
    }
}
std::span<std::uint8_t> AudioSink::encoded_buffer(UINT32 frames) {
    if (!encoded_ || !interleaved_) return {};
    return {interleaved_, static_cast<std::size_t>(frames) * encoded_align_};
}
HRESULT AudioSink::end(UINT32 frames) {
    if (!frames && !updating_) return S_OK;
    HRESULT hr = stream_ ? stream_->EndUpdatingAudioObjects() : render_->ReleaseBuffer(frames, 0);
    updating_ = false; interleaved_ = nullptr;
    if (SUCCEEDED(hr)) written_ += frames;
    return hr;
}
HRESULT AudioSink::volume(float value) {
    if (encoded_) return S_OK;
    if (!volume_) {
        // Per-object gain is the spatial API fallback when no stream volume service is exposed.
        if (!stream_) return E_NOINTERFACE;
        for (auto& o : objects_) if (o) { HRESULT hr = o->SetVolume(value); if (FAILED(hr)) return hr; }
        return S_OK;
    }
    UINT32 count = 0; HRESULT hr = volume_->GetChannelCount(&count); if (FAILED(hr)) return hr;
    // VLC supports amplification above unity. The service accepts only [0,1]; extra gain is handled in the producer.
    for (UINT32 i = 0; i < count; ++i) { hr = volume_->SetChannelVolume(i, std::clamp(value, 0.f, 1.f)); if (FAILED(hr)) return hr; }
    return S_OK;
}
double AudioSink::pending_seconds() {
    UINT64 position = 0, qpc = 0;
    if (!clock_ || FAILED(clock_->GetPosition(&position, &qpc))) return -1;
    double elapsed = 0;
    if (running_ && qpc) {
        LARGE_INTEGER now{}, frequency{}; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
        elapsed = std::max(0., static_cast<double>(now.QuadPart) / static_cast<double>(frequency.QuadPart) - static_cast<double>(qpc) / 10000000.);
    }
    return std::max(0., static_cast<double>(written_) / rate_ - static_cast<double>(position) / static_cast<double>(clock_frequency_) - elapsed);
}
}
