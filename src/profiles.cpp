#include "upmix3d/core.hpp"
#include <cmath>

namespace upmix3d {
std::string_view input_layout_id(Mask input) {
    input = normalize(input);
    constexpr std::array entries{
        Layout{"1.0", "", bit(Channel::FC)}, Layout{"2.0", "", fronts}, Layout{"2.1", "", fronts | bit(Channel::LFE)},
        Layout{"3.0", "", fronts | bit(Channel::FC)}, Layout{"3.1", "", fronts | bit(Channel::FC) | bit(Channel::LFE)},
        Layout{"4.0", "", fronts | sides}, Layout{"4.1", "", fronts | sides | bit(Channel::LFE)},
        Layout{"5.0", "", bed_51 & ~bit(Channel::LFE)}, Layout{"5.1", "", bed_51},
        Layout{"6.0", "", fronts | sides | backs}, Layout{"6.1", "", bed_51 | bit(Channel::BC)},
        Layout{"7.0", "", bed_71 & ~bit(Channel::LFE)}, Layout{"7.1", "", bed_71}, Layout{"8.1", "", bed_71 | bit(Channel::BC)}};
    for (const auto& entry : entries) if (input == entry.mask) return entry.id;
    return "other";
}
PresetValues preset_values(const Settings& s) {
    return {s.center_extract*100, s.center_width, s.dimension, s.side_db, s.back_db,
        s.surround_ms, s.decorrelation*100, s.height_db, s.height_hz, s.height_ms, s.height_balance, s.dialogue_protect ? 1.f : 0.f};
}
void apply_preset_values(Settings& s, const PresetValues& v) {
    s.center_extract = v[0]/100; s.center_width = v[1]; s.dimension = v[2]; s.side_db = v[3];
    s.back_db = v[4]; s.surround_ms = v[5]; s.decorrelation = v[6]/100;
    s.height_db = v[7]; s.height_hz = v[8]; s.height_ms = v[9]; s.height_balance = v[10]; s.dialogue_protect = v[11] >= .5f;
}
bool apply_preset_overrides(Settings& s, const PresetValues& values, const PresetValues& baseline) {
    auto result = preset_values(s);
    bool changed = false;
    for (std::size_t i = 0; i < result.size(); ++i) if (std::abs(values[i] - baseline[i]) > 1e-5f) { result[i] = values[i]; changed = true; }
    apply_preset_values(s, result); return changed;
}
}
