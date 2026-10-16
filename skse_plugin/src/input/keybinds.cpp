#include "keybinds.h"
#include "modes.h"
#include "../logger/logger.h"

namespace SpellHotbar::Input {

	std::array<KeyBind, 12> key_spells{
		KeyBind{RE::INPUT_DEVICE::kNone, 0},  //2
		KeyBind{RE::INPUT_DEVICE::kNone, 0},  //3
		KeyBind{RE::INPUT_DEVICE::kNone, 0},  //4
		KeyBind{RE::INPUT_DEVICE::kNone, 0},  //5
		KeyBind{RE::INPUT_DEVICE::kNone, 0},  //6
		KeyBind{RE::INPUT_DEVICE::kNone, 0},  //7
		KeyBind{RE::INPUT_DEVICE::kNone, 0},  //8
		KeyBind{RE::INPUT_DEVICE::kNone, 0},  //9
		KeyBind{RE::INPUT_DEVICE::kNone, 0}, //10
		KeyBind{RE::INPUT_DEVICE::kNone, 0}, //11
		KeyBind{RE::INPUT_DEVICE::kNone, 0}, //12
		KeyBind{RE::INPUT_DEVICE::kNone, 0}  //13
	};

	KeyBind key_prev {RE::INPUT_DEVICE::kNone, 0}; //75
	KeyBind key_next {RE::INPUT_DEVICE::kNone, 0};  //77

	KeyBind key_oblivion_cast{ RE::INPUT_DEVICE::kNone, 0 }; // 47 V
	KeyBind key_oblivion_potion{ RE::INPUT_DEVICE::kNone, 0 }; //45 B

	//Unbound in the C++ default. On save load, storage.cpp assigns H to a save that carries this
	//key unbound or on C (DIK 46), so in practice the bind menu opens on H.
	KeyBind key_open_advanced_bind_menu{ RE::INPUT_DEVICE::kNone, 0 };

	KeyModifier mod_1(RE::INPUT_DEVICE::kNone, 0, 0); //ctrl 29, 157
	KeyModifier mod_2(RE::INPUT_DEVICE::kNone, 0, 0); //shift 42, 54
	KeyModifier mod_3(RE::INPUT_DEVICE::kNone, 0, 0);  //alt 56, 184

	KeyModifier mod_dual_cast(RE::INPUT_DEVICE::kNone, 0, 0);
	KeyModifier mod_show_bar(RE::INPUT_DEVICE::kNone, 0, 0);
	KeyModifier mod_oblivion_show_bar(RE::INPUT_DEVICE::kNone, 0, 0);

	KeyModifier mod_alt(RE::INPUT_DEVICE::kKeyboard, 56, 184);  //fixed modifier, used for gui
	//KeyModifier mod_shift(RE::INPUT_DEVICE::kKeyboard, 42, 54);  //fixed modifier, used for gui

	namespace {
		GamepadPresses gamepad_presses;

		KeyBind* binding_for_id(int id)
		{
			if (id >= spell_1 && id <= spell_12) { return &key_spells[id]; }
			switch (id) {
			case ui_next: return &key_next;
			case ui_prev: return &key_prev;
			case oblivion_cast: return &key_oblivion_cast;
			case oblivion_potion: return &key_oblivion_potion;
			case open_advanced_bind_menu: return &key_open_advanced_bind_menu;
			default: return nullptr; // Bar modifiers themselves remain single buttons.
			}
		}
	}

	std::uint16_t get_gamepad_modifiers(int slot)
	{
		const auto* binding = binding_for_id(slot);
		return binding ? binding->gamepad_modifiers() : 0;
	}

	void begin_gamepad_frame() { gamepad_presses.begin_frame(); }

	void update_gamepad_holds()
	{
		for (int id = 0; id < num_keys; ++id) {
			if (auto* binding = binding_for_id(id); binding && is_gamepad_button(binding->get_dx_scancode())) {
				binding->set_synthetic_down(gamepad_presses.binding_held(id));
			}
		}
	}

	bool observe_gamepad_button(RE::InputEvent* event, int code, bool down, bool pressed, bool allow_start)
	{
		std::array<GamepadBinding, num_keys> bindings{};
		for (int id = 0; id < num_keys; ++id) {
			if (const auto* binding = binding_for_id(id)) {
				bindings[id] = { binding->get_dx_scancode(), binding->gamepad_modifiers() };
			}
		}
		const bool capture = gamepad_presses.raw(reinterpret_cast<std::uintptr_t>(event), code, down, pressed, bindings, allow_start);
		update_gamepad_holds();
		return capture;
	}

	void filter_gamepad_button(RE::InputEvent* event, bool down, bool pressed)
	{
		gamepad_presses.filtered(reinterpret_cast<std::uintptr_t>(event), down, pressed);
		update_gamepad_holds();
	}

	bool gamepad_event_matches(const KeyBind* binding)
	{
		return gamepad_presses.binding_id() >= 0 && binding_for_id(gamepad_presses.binding_id()) == binding;
	}
	bool gamepad_event_consumed() { return gamepad_presses.consumed(); }
	void consume_gamepad_final() { gamepad_presses.consume(); }
	std::uint16_t held_gamepad_buttons() { return gamepad_presses.held(); }

	std::vector<GamepadBindingRecord> collect_gamepad_combinations()
	{
		std::vector<GamepadBindingRecord> rows;
		for (int id = 0; id < num_keys; ++id) {
			if (const auto* binding = binding_for_id(id); binding && binding->gamepad_modifiers()) {
				rows.push_back({ static_cast<std::uint8_t>(id), { binding->get_dx_scancode(), binding->gamepad_modifiers() } });
			}
		}
		return rows;
	}

	inline void _check_unbind(KeyBind & bind, int code, std::uint16_t modifiers) {
		if (bind.get_dx_scancode() == code && bind.gamepad_modifiers() == modifiers) {
			bind.unbind();
		}
	}

	inline void _check_unbind(KeyModifier& mod, int code) {
		if (mod.get_dx_scancode() == code || mod.get_dx_scancode2() == code) {
			mod.rebind(-1);
		}
	}

	void unbind_if_already_used(int code, std::uint16_t modifiers = 0) {
		//check remove binds if already used
		for (size_t i = 0Ui64; i < key_spells.size(); ++i) {
			if (key_spells[i].get_dx_scancode() == code && key_spells[i].gamepad_modifiers() == modifiers) { gamepad_presses.cancel_binding(static_cast<int>(i)); }
			_check_unbind(key_spells[i], code, modifiers);
		}
		_check_unbind(key_next, code, modifiers);
		_check_unbind(key_prev, code, modifiers);
		_check_unbind(key_oblivion_cast, code, modifiers);
		_check_unbind(key_oblivion_potion, code, modifiers);
		_check_unbind(key_open_advanced_bind_menu, code, modifiers);
		if (modifiers != 0) { return; }
		_check_unbind(mod_1, code);
		_check_unbind(mod_2, code);
		_check_unbind(mod_3, code);
		_check_unbind(mod_show_bar, code);
	}

	bool rebind_gamepad_combination(int slot, GamepadBinding binding, bool check_conflicts)
	{
		auto* target = binding_for_id(slot);
		if (!target || !binding.valid()) { return false; }
		gamepad_presses.cancel_binding(slot);
		if (check_conflicts) { unbind_if_already_used(binding.button, binding.modifiers); }
		target->assign_from_dx_scancode(binding.button);
		target->set_gamepad_modifiers(binding.modifiers);
		logger::info("SH2 input: binding {} gamepad final={} modifiers={:04x}", slot, binding.button, binding.modifiers);
		return true;
	}

	int rebind_key(int slot, int code, bool check_conflicts)
	{
		gamepad_presses.cancel_binding(slot);
		if (check_conflicts) unbind_if_already_used(code);
		if (slot >= 0 && slot <= keybind_id::spell_12) {
			key_spells[slot].assign_from_dx_scancode(code);
			return key_spells[slot].get_dx_scancode();
		}
		else if (slot == keybind_id::ui_next) {
			key_next.assign_from_dx_scancode(code);
			return key_next.get_dx_scancode();
		}
		else if (slot == keybind_id::ui_prev) {
			key_prev.assign_from_dx_scancode(code);
			return key_prev.get_dx_scancode();
		}
		else if (slot == keybind_id::modifier_1) {
			mod_1.rebind(code);
			return mod_1.get_dx_scancode();
		}
		else if (slot == keybind_id::modifier_2) {
			mod_2.rebind(code);
			return mod_2.get_dx_scancode();
		}
		else if (slot == keybind_id::modifier_3) {
			mod_3.rebind(code);
			return mod_3.get_dx_scancode();
		}
		else if (slot == keybind_id::dual_casting_mod) {
			mod_dual_cast.rebind(code);
			return mod_dual_cast.get_dx_scancode();
		}
		else if (slot == keybind_id::show_bar_mod) {
			mod_show_bar.rebind(code);
			return mod_show_bar.get_dx_scancode();
		}
		else if (slot == keybind_id::oblivion_cast) {
			key_oblivion_cast.assign_from_dx_scancode(code);
			return key_oblivion_cast.get_dx_scancode();
		}
		else if (slot == keybind_id::oblivion_potion) {
			key_oblivion_potion.assign_from_dx_scancode(code);
			return key_oblivion_potion.get_dx_scancode();
		}
		else if (slot == keybind_id::oblivion_show_bar_mod) {
			mod_oblivion_show_bar.rebind(code);
			return mod_oblivion_show_bar.get_dx_scancode();
		}
		else if (slot == keybind_id::open_advanced_bind_menu) {
			key_open_advanced_bind_menu.assign_from_dx_scancode(code);
			return key_open_advanced_bind_menu.get_dx_scancode();
		}

		return 0;
	}

	void reset_keybinds()
	{
		gamepad_presses.reset();
		for (int slot = 0; slot < keybind_id::num_keys; ++slot) {
			rebind_key(slot, -1, false);
		}
		set_input_mode(0);
	}

	int get_keybind(int slot)
	{
		if (slot >= 0 && slot <= keybind_id::spell_12) {
			return key_spells[slot].get_dx_scancode();
		}
		else if (slot == keybind_id::ui_next) {
			return key_next.get_dx_scancode();
		}
		else if (slot == keybind_id::ui_prev) {
			return key_prev.get_dx_scancode();
		}
		else if (slot == keybind_id::modifier_1) {
			return mod_1.get_dx_scancode();
		}
		else if (slot == keybind_id::modifier_2) {
			return mod_2.get_dx_scancode();
		}
		else if (slot == keybind_id::modifier_3) {
			return mod_3.get_dx_scancode();
		}
		else if (slot == keybind_id::dual_casting_mod) {
			return mod_dual_cast.get_dx_scancode();
		}
		else if (slot == keybind_id::show_bar_mod) {
			return mod_show_bar.get_dx_scancode();
		}
		else if (slot == keybind_id::oblivion_cast) {
			return key_oblivion_cast.get_dx_scancode();
		}
		else if (slot == keybind_id::oblivion_potion) {
			return key_oblivion_potion.get_dx_scancode();
		}
		else if (slot == keybind_id::oblivion_show_bar_mod) {
			return mod_oblivion_show_bar.get_dx_scancode();
		}
		else if (slot == keybind_id::open_advanced_bind_menu) {
			return key_open_advanced_bind_menu.get_dx_scancode();
		}
		return 0;
	}

}
