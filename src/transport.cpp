#include "upmix3d/transport.hpp"
#include <algorithm>
#include <stdexcept>

namespace upmix3d {
bool permits(Passthrough policy, EncodedCodec codec) {
    return policy == Passthrough::All || (policy == Passthrough::Core &&
        (codec == EncodedCodec::Ac3 || codec == EncodedCodec::Dts || codec == EncodedCodec::Spdif));
}
EncodedSpec encoded_spec(EncodedCodec codec, unsigned rate, unsigned channels) {
    if (rate < 8000 || rate > 768000 || !channels || channels > 32) throw std::invalid_argument("Invalid encoded source format");
    EncodedSpec spec{codec, rate, channels, rate, 2, rate};
    switch (codec) {
    case EncodedCodec::Eac3:
        if (rate != 16000 && rate != 22050 && rate != 24000 && rate != 32000 && rate != 44100 && rate != 48000)
            throw std::invalid_argument("Unsupported E-AC-3 sample rate");
        spec.carrier_rate = spec.converter_rate = rate * 4; break;
    case EncodedCodec::DtsHd:
    case EncodedCodec::TrueHd:
        if (rate == 44100 || rate == 88200 || rate == 176400) spec.carrier_rate = 176400;
        else if (rate == 48000 || rate == 96000 || rate == 192000) spec.carrier_rate = 192000;
        else throw std::invalid_argument("Unsupported high-bit-rate sample-rate family");
        spec.carrier_channels = 8;
        // VLC's DTS-HD converter expresses its IEC repetition period in stereo
        // (four-byte) frames. WASAPI uses eight-channel carrier frames.
        spec.converter_rate = spec.carrier_rate * (codec == EncodedCodec::DtsHd ? 4 : 1); break;
    default: break;
    }
    return spec;
}
std::string_view codec_name(EncodedCodec codec) {
    switch (codec) {
    case EncodedCodec::Ac3: return "AC-3";
    case EncodedCodec::Dts: return "DTS";
    case EncodedCodec::Eac3: return "E-AC-3";
    case EncodedCodec::DtsHd: return "DTS-HD";
    case EncodedCodec::TrueHd: return "TrueHD/MLP";
    default: return "IEC61937";
    }
}
ByteRing::ByteRing(std::size_t capacity) : data_(capacity) { if (!capacity) throw std::invalid_argument("Empty encoded ring"); }
bool ByteRing::push(std::span<const std::uint8_t> bytes, bool swap_words) {
    if (bytes.size() > available() || (swap_words && bytes.size() % 2)) return false;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        data_[write_] = bytes[swap_words ? (i ^ 1) : i]; write_ = (write_ + 1) % capacity();
    }
    size_ += bytes.size(); return true;
}
std::size_t ByteRing::pop(std::span<std::uint8_t> destination) {
    const auto count = std::min(destination.size(), size_);
    for (std::size_t i = 0; i < count; ++i) { destination[i] = data_[read_]; read_ = (read_ + 1) % capacity(); }
    size_ -= count; return count;
}
void ByteRing::clear() { read_ = write_ = size_ = 0; }
}
