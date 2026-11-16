#include "upmix3d/core.hpp"

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
void apply_preset_values(Settings& s, const PresetValues& v) {
    s.center_extract = v[0]/100; s.center_width = v[1]; s.side_db = v[2]; s.back_db = v[3];
    s.surround_ms = v[4]; s.height_db = v[5]; s.height_hz = v[6]; s.dialogue_protect = v[7] >= .5f;
}
}
