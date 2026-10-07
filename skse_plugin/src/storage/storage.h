#pragma once


//#define DEBUG_LOG_SERIALIZATION

namespace SpellHotbar::Storage {
    /*
    * 2 - SpellHotbar2, alpha until 0.0.4
    * 3 - SpellHotbar2, alpha 0.0.5
    * 4 - SpellHotbar2, alpha 0.0.7
    * 5 - SpellHotbar2, alpha 0.0.13
    * 6 - Ability slots (kind byte + art_id payload)
    */
    // V7: appends GameData::spell_gcd to the HOTB record. Older saves stop at V6 and
    // keep the 1.5s default.
    // V8: Hotbar Action slots use kind 2. Format 7 remains readable; Action-shaped records in
    // that legacy format are ignored rather than interpreted as FormIDs.
    // V9: appends the in-menu dock's slot_scale, offset_x/y, slot_spacing and anchor
    // point after spell_gcd. Older saves keep the bottom-anchored defaults from Bars::init().
    // V10: Ability hyperarmor appends the stagger-immunity bool and the damage-reduction percent
    // after the dock (casts/hyper_armor.h owns the layout). Older saves get on and 50 %.
    constexpr uint32_t save_format = 10U;

    extern std::array<RE::FormID, 12> hotbar_main;

    enum class menu_slot_type {
        magic_menu = 0,
        vampire_lord = 1,
        werewolf = 2,
        custom_favmenu = 3,
    };

    /**
    * Store variables on game save
    */
    void SaveCallback(SKSE::SerializationInterface* a_intfc);

    /**
     * Load variables on game load
     */
    void LoadCallback(SKSE::SerializationInterface* a_intfc);

    void RevertCallback(SKSE::SerializationInterface* a_intfc);

    /** True when the currently loaded save contained the existing HOTB settings record. */
    bool loaded_existing_settings();

    /** Restore first-install defaults before a new game or save overlays its records. */
    void reset_all_runtime_state();

    bool slotSpell(RE::FormID form, size_t index, menu_slot_type slot_type);

}
