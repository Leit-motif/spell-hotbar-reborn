#pragma once

// Spell Hotbar 2's own input hook. As an SKSE Menu Framework guest, SH2 had SMF run
// `Input::process_event` once per event from its own hook and unlink the events SH2 captured;
// without SMF, SH2 hooks the same dispatch call site itself -- the wheeler / IED site at
// RELOCATION_ID(67315, 68617)+0x7B, which FLICK also hooks. Two `write_call<5>` writes on one
// site chain: the later install runs first and calls the earlier thunk as its original, so SH2
// and FLICK both see every frame's events in either load order.
namespace SpellHotbar::Input {
    // Install the dispatch hook. Needs the SKSE trampoline allocated (plugin.cpp does that once,
    // before every hook install). Call once, from SKSEPluginLoad.
    void install_hook();
}
