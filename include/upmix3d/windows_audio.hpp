#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <spatialaudioclient.h>
#include <wrl/client.h>
#include <functional>
#include <memory>
#include <string>
#include "upmix3d/core.hpp"
#include "upmix3d/transport.hpp"

namespace upmix3d {
enum class Route { Spatial, Shared, Exclusive };
using Log = std::function<void(const std::string&)>;
struct DeviceInfo { std::string id, name; };
std::vector<DeviceInfo> enumerate_devices();
std::wstring widen(const std::string& value);
std::string narrow(const std::wstring& value);

// All methods, including destruction, run on the same COM-initialized render thread.
class AudioSink {
public:
    explicit AudioSink(Log log);
    ~AudioSink();
    HRESULT open(const std::string& device, Mask requested, bool top_middle, unsigned buffer_ms = 20, bool fold_back_center = false);
    HRESULT open_encoded(const std::string& device, const EncodedSpec& spec);
    HRESULT start();
    HRESULT stop();
    HRESULT reset();
    HRESULT begin(UINT32& frames);
    void write(UINT32 frame, const Frame& samples);
    std::span<std::uint8_t> encoded_buffer(UINT32 frames);
    HRESULT end(UINT32 frames);
    HRESULT volume(float value);
    double pending_seconds();
    unsigned rate() const { return rate_; }
    Mask mask() const { return mask_; }
    Route route() const { return route_; }
    HANDLE event() const { return event_; }
    UINT32 capacity() const { return capacity_; }
    bool encoded() const { return encoded_; }
    std::string device_id() const;
private:
    HRESULT select_device(const std::string& id);
    HRESULT initialize_client(const WAVEFORMATEX* format, bool exclusive, REFERENCE_TIME duration, DWORD flags);
    HRESULT open_spatial(Mask requested, bool top_middle);
    HRESULT open_wasapi(Mask requested);
    void clear();
    void set_fold_targets(Channel source, Mask available);
    Log log_;
    Microsoft::WRL::ComPtr<IMMDevice> device_;
    Microsoft::WRL::ComPtr<ISpatialAudioClient> spatial_;
    Microsoft::WRL::ComPtr<ISpatialAudioObjectRenderStream> stream_;
    Microsoft::WRL::ComPtr<IAudioClient> client_;
    Microsoft::WRL::ComPtr<IAudioRenderClient> render_;
    Microsoft::WRL::ComPtr<IAudioClock> clock_;
    Microsoft::WRL::ComPtr<IAudioStreamVolume> volume_;
    std::array<Microsoft::WRL::ComPtr<ISpatialAudioObject>, channel_count> objects_;
    std::array<AudioObjectType, channel_count> object_types_{};
    std::array<float*, channel_count> buffers_{};
    std::array<std::array<float, channel_count>, channel_count> routing_{};
    std::array<float, channel_count> routing_scale_{};
    std::vector<Channel> wave_order_;
    std::vector<Channel> active_channels_;
    HANDLE event_ = nullptr;
    BYTE* interleaved_ = nullptr;
    Mask mask_ = 0;
    Mask previous_failed_ = 0;
    Route route_ = Route::Shared;
    unsigned rate_ = 0;
    unsigned buffer_ms_ = 20;
    bool encoded_ = false, running_ = false;
    unsigned encoded_align_ = 0;
    UINT32 capacity_ = 0;
    UINT64 clock_frequency_ = 0, written_ = 0;
    bool updating_ = false, top_middle_ = false, fold_back_center_ = false;
};
}
