#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace upmix3d {
enum class EncodedCodec { Ac3, Dts, Eac3, DtsHd, TrueHd, Spdif };
enum class Passthrough { Disabled, Core, All };
struct EncodedSpec {
    EncodedCodec codec = EncodedCodec::Ac3;
    unsigned source_rate = 48000, source_channels = 2;
    unsigned carrier_rate = 48000, carrier_channels = 2, converter_rate = 48000;
    unsigned block_align() const { return carrier_channels * 2; }
    unsigned bytes_per_second() const { return carrier_rate * block_align(); }
    double seconds(std::size_t bytes) const { return static_cast<double>(bytes) / bytes_per_second(); }
};
bool permits(Passthrough policy, EncodedCodec codec);
EncodedSpec encoded_spec(EncodedCodec codec, unsigned source_rate, unsigned channels);
std::string_view codec_name(EncodedCodec codec);

// Byte-preserving ring for IEC 61937. Synchronization is supplied by the owner.
class ByteRing {
public:
    explicit ByteRing(std::size_t capacity);
    std::size_t size() const { return size_; }
    std::size_t capacity() const { return data_.size(); }
    std::size_t available() const { return capacity() - size_; }
    bool push(std::span<const std::uint8_t> bytes, bool swap_words = false);
    std::size_t pop(std::span<std::uint8_t> destination);
    void clear();
private:
    std::vector<std::uint8_t> data_;
    std::size_t read_ = 0, write_ = 0, size_ = 0;
};
}
