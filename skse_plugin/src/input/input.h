#pragma once

#include <string>
#include <string_view>

#include "action_inject.h"
#include "../game_data/action_definition.h"

namespace SpellHotbar::Input {
	static inline const char* sound_UISkillsBackward = "UISkillsBackward";
	static inline const char* sound_UISkillsForward = "UISkillsForward";
	static inline const char* sound_MagFail = "MAGFail";
	static inline const char* sound_UIFavorite = "UIFavorite";
	static inline const char* sound_UISkillsFocus = "UISkillsFocus";
	static inline const char* sound_UIMenuCancel = "UIMenuCancel";

	/**
	 * SKSE Menu Framework input callback. Returns true when the framework should
	 * remove this event from Skyrim's input queue.
	 */
	bool __stdcall process_event(RE::InputEvent* event);

	bool in_binding_menu();

	bool in_ingame_state();

	std::tuple<RE::INPUT_DEVICE, uint8_t> dx_scan_code_to_input(int dx_scancode);
	int input_to_dx_scancode(RE::INPUT_DEVICE device, uint8_t code);

	std::tuple<RE::INPUT_DEVICE, uint8_t> get_shout_key_and_device();
	int get_shout_key_dxcode();

	/** Queue one physical Action event. `user_event` is the engine's controlmap name for the
	 *  target key, or empty -- the ordinary case, since a mod hotkey has no controlmap entry. */
	bool queue_action_event(const ActionInput& input, float value, float held_duration,
		std::string_view user_event);
	/** Name the gameplay ControlMap event for an Action target, or empty when the target key is
	 *  not mapped in the engine controlmap. Empty is the normal mod-hotkey answer. */
	std::string resolve_action_user_event(const ActionInput& input);

	/**
	* allowed to use powers, less restrictive than regular casts
	*/
	bool allowed_to_instantcast(RE::FormID skill);

	/**
	* Assumes to be called after in_ingame_state, checks for additional player based restrictions (mounted, swimming, ...)
	*/
	bool allowed_to_cast(RE::FormID skill, bool allow_sprint = false);

	RE::TESForm* get_current_selected_spell_in_menu();

	bool slot_spell(RE::TESForm* form, size_t index);

	class KeyModifier
	{
    public:
        KeyModifier(RE::INPUT_DEVICE device, uint8_t code1, uint8_t code2 = 0Ui8);
		~KeyModifier() = default;

		void update(uint32_t key_code, RE::INPUT_DEVICE key_device, bool is_pressed);

		void rebind(int dx_scancode);
		int get_dx_scancode();
		int get_dx_scancode2();

		bool isValidBound();

		inline bool isDown();
    private:
		RE::INPUT_DEVICE input_device;

        uint8_t keycode;
        uint8_t keycode2;

		bool isDown1;
        bool isDown2;
	};

	inline bool KeyModifier::isDown()
	{
		return isDown1 || isDown2;
	}

	class KeyBind {
	public:
		KeyBind(RE::INPUT_DEVICE device, uint8_t code);
		~KeyBind() = default;

		bool matches(uint32_t key_code, RE::INPUT_DEVICE key_device) const;
		/*
		* device independant code used by game
		*/
		int get_dx_scancode() const;

		void assign_from_dx_scancode(int code);

		void update(uint32_t key_code, RE::INPUT_DEVICE key_device, bool is_pressed);

		inline bool isDown() const
		{
			return m_isDown;
		}
		/**
		 * Test seam: force the held state so a channel can be sustained
		 * headlessly. Real input events keep writing m_isDown as usual afterwards.
		 */
		inline void set_synthetic_down(bool held)
		{
			m_isDown = held;
		}
		void unbind();

		inline bool isValidBound() const {
			return input_device == RE::INPUT_DEVICE::kKeyboard ||
				   input_device == RE::INPUT_DEVICE::kMouse ||
				   input_device == RE::INPUT_DEVICE::kGamepad;
		};

	private:
		RE::INPUT_DEVICE input_device;
		uint8_t keycode;
		bool m_isDown;
	};
}
