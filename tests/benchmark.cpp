#include "upmix3d/core.hpp"
#include <chrono>
#include <iostream>
#include <random>
#include <cmath>

int main(int argc, char** argv) {
    using namespace upmix3d;
    constexpr unsigned rate = 48000;
    const auto seconds = argc > 1 ? std::stoull(argv[1]) : 10;
    if (!seconds || seconds > 86400) return 2;
    const std::uint64_t frames = rate * seconds;
    std::mt19937 random(4);
    std::uniform_real_distribution<float> distribution(-.2f, .2f);
    std::vector<std::array<float, 2>> input(rate);
    for (auto& frame : input) frame = {distribution(random), distribution(random)};
    auto settings = preset("music");
    Processor processor({Channel::FL, Channel::FR}, bed_71 | heights, rate, settings);
    double energy = 0;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < frames; ++i) {
        auto frame = input[i % input.size()];
        if (seconds >= 60 && (i / rate) % 2 == 0) for (auto& value : frame) value *= 8;
        const auto output = processor.process(frame);
        for (float value : output) {
            if (!std::isfinite(value) || std::abs(value) > 1) { std::cerr << "Invalid DSP output at frame " << i << '\n'; return 1; }
            energy += static_cast<double>(value) * value;
        }
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "{\"audio_seconds\":" << seconds << ",\"wall_seconds\":" << elapsed
              << ",\"single_core_percent\":" << elapsed * 100 / static_cast<double>(seconds)
              << ",\"energy\":" << energy << "}\n";
}
