#pragma once
#include <string_view>

namespace SpellHotbar::GameData {
    constexpr std::string_view unambiguous_gamepad_key_name(int code, std::string_view name)
    {
        // Upstream uses LS/RS for the shoulder buttons, although those abbreviations
        // conventionally identify stick clicks. The shipped icons already say LB/RB.
        if (code == 274 && name == "LS") { return "LB"; }
        if (code == 275 && name == "RS") { return "RB"; }
        return name;
    }
}
