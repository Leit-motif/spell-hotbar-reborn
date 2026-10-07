#pragma once

#include <cstdint>

// The ability (weapon art) editor, opened from the bind menu's Abilities tab and drawn in its
// place. FLICK-side only.
namespace SpellHotbar::FlickUi::AbilityEditor {
    bool is_open();
    void open(std::uint32_t a_art_id);
    void close();
    void draw();
}
