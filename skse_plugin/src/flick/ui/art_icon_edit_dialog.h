#pragma once

#include <cstdint>

// The ability icon dialog, opened from the bind menu and drawn in its place. FLICK-side only.
namespace SpellHotbar::FlickUi::ArtIconEditor {
    bool is_open();
    void open(std::uint32_t a_art_id);
    void close();
    void draw();
}
