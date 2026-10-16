#pragma once
#include "input.h"
#include "gamepad_binding_io.h"

namespace SpellHotbar::Input {

	enum keybind_id : int {
		spell_1 = 0,
		spell_2 = 1,
		spell_3 = 2,
		spell_4 = 3,
		spell_5 = 4,
		spell_6 = 5,
		spell_7 = 6,
		spell_8 = 7,
		spell_9 = 8,
		spell_10 = 9,
		spell_11 = 10,
		spell_12 = 11,
		ui_next = 12,
		ui_prev = 13,
		modifier_1 = 14,
		modifier_2 = 15,
		modifier_3 = 16,
		dual_casting_mod = 17,
		show_bar_mod = 18,
		oblivion_cast = 19,
		oblivion_potion = 20,
		oblivion_show_bar_mod = 21,
		open_advanced_bind_menu = 22,
		num_keys,

		dummy_key_vanilla_shout = 99 //dummy value used for oblivion mode shout slot
	};


	extern std::array<KeyBind,12> key_spells;
	extern KeyBind key_prev;
	extern KeyBind key_next;
	extern KeyBind key_open_advanced_bind_menu;

	extern KeyModifier mod_1;
	extern KeyModifier mod_2;
	extern KeyModifier mod_3;

	extern KeyModifier mod_dual_cast;
	extern KeyModifier mod_show_bar;

	extern KeyModifier mod_alt;
	extern KeyModifier mod_shift;

	extern KeyBind key_oblivion_cast;
	extern KeyBind key_oblivion_potion;
	extern KeyModifier mod_oblivion_show_bar;

	int rebind_key(int slot, int code, bool check_conflicts = true);
	int get_keybind(int slot);
	std::uint16_t get_gamepad_modifiers(int slot);
	bool rebind_gamepad_combination(int slot, GamepadBinding binding, bool check_conflicts = true);
	void begin_gamepad_frame();
	bool observe_gamepad_button(RE::InputEvent* event, int code, bool down, bool pressed, bool allow_start);
	void filter_gamepad_button(RE::InputEvent* event, bool down, bool pressed);
	bool gamepad_event_matches(const KeyBind* binding);
	bool gamepad_event_consumed();
	void consume_gamepad_final();
	std::uint16_t held_gamepad_buttons();
	std::vector<GamepadBindingRecord> collect_gamepad_combinations();
	void reset_keybinds();
}
