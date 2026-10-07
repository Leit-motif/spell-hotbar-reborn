#pragma once

// FLICK-loaded copies of Spell Hotbar 2's atlases, shared by the dock and the windows.
//
// FLICK only draws images it loaded itself (its handle is opaque, and handing it a texture loaded
// elsewhere draws nothing, measured), and its loader reads PNG. SH2's atlases ship as PNG, so the
// sibling lookup here is a no-op rename for them; it still strips a `.dds` spelling for any path
// that carries one.
//
// FLICK-side only: this header names FUCK::Image and is included from sh2_flick TUs.
#include <string>

namespace SpellHotbar::FlickImages {
    // The texture for the atlas SH2 loaded from `a_sh2_path` (as SH2 spelled it, e.g.
    // ".\data\SKSE\Plugins\SpellHotbar\images\icons_vanilla.png"), or nullptr when FLICK could
    // not load it. Leaked on purpose: FUCK::Image's destructor calls back into FUCK.dll, and a
    // static destroyed at process exit can land in an unloaded host (Menu Studio and Fitting
    // Room both leak theirs for the same reason). A path that fails to load is remembered as
    // null so it is tried, and logged, once.
    FUCK::Image* image_for(const std::string& a_sh2_path);

    // A glyph from images/dock/<stem>.png.
    FUCK::Image* glyph(const char* a_stem);

    // ImGui's packed ABGR (IM_COL32) to a float colour.
    ImVec4 unpack(unsigned a_col);
}
