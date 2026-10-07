#pragma once

#include <cstdint>

// The action editor, opened from the bind menu's Actions tab and drawn in its place.
// FLICK-side only.
namespace SpellHotbar::FlickUi::ActionEditor {
    bool is_open();
    void open(std::uint32_t a_action_id);
    void close();
    void draw();
}
