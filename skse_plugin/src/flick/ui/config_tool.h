#pragma once

// Spell Hotbar 2's configuration as a FLICK sidebar tool. The configuration pages -- keybinds,
// settings, bars, perks, presets, the window openers and the utilities; seven tabs, the Spell
// Bind Menu and Spells pages fold into Windows -- are tabs of one FUCK::ITool named
// "Spell Hotbar 2", the shape Menu Studio and Fitting Room use for theirs. The player reaches it
// through FLICK's own menu (F7 by default).
namespace SpellHotbar::FlickUi::ConfigTool {
    // Register the tool with FLICK. Call at kPostLoadGame with the windows: anything FLICK is
    // handed during the initial load can crash on an nvwgf2umx.dll worker thread. Idempotent.
    void register_tool();
}
