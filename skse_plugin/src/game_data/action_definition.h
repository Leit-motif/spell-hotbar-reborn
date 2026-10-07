#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace SpellHotbar {

// The input seam an Action uses when it is pressed.
//
// `physical_scancode` mirrors the slot key onto an authored native ButtonEvent -- the key bridge.
// It is the only kind that resolves a target, and the only one that can recurse on its own bind.
//
// `light_attack` and `power_attack` do not touch the input queue at all. They send an attack
// event straight to the player's animation graph, which is the one path every combat mod reads:
// BFCO reads the raw device idCode, so an injected scancode never reaches it, while the graph
// event plays BFCO_Attack1 / BFCO_PowerAttack1 and chains the combo. See
// `casts/attack_action_events.h` for the event names and the direction rule.
enum class ActionKind : std::uint8_t {
	physical_scancode = 0,
	light_attack = 1,
	power_attack = 2,
};

// The persisted device is deliberately separate from the DX scancode.  The latter is the
// device-independent value SH2 already uses for ordinary keybinds, while the device tells the
// injection seam which native ButtonEvent device should receive the decoded code.
enum class ActionInputDevice : std::uint8_t {
	keyboard = 0,
	mouse,
	gamepad,
};

// One source, and its stored value stays 2. Values 0 and 1 were `ocpa_power` and
// `dodge_hotkey`, which resolved a key by reading One Click Power Attack's and TK Dodge RE's own
// ini files on every press. The send-side attack seam replaced that reader, so the sources are
// gone -- but a player's `action_overlays.json` may still carry a 0 or a 1, and renumbering
// `captured` would silently turn one of those rows into a bound captured Action. The loader maps
// a legacy value to an unbound captured row instead.
enum class ActionTargetSource : std::uint8_t {
	captured = 2,
};

// The last stored target value that is not a live source. Anything at or below it is a legacy
// row from before the send-side seam.
inline constexpr std::uint32_t last_legacy_action_target_value = 1;

struct ActionInput {
	ActionInputDevice device{ ActionInputDevice::keyboard };
	std::uint32_t dx_scancode{ 0 };

	[[nodiscard]] constexpr bool is_bound() const noexcept { return dx_scancode != 0; }
};

// Legacy Action overlays stored only the combined DX value.  Keep those overlays loadable while
// new saves record both pieces explicitly.
[[nodiscard]] constexpr ActionInputDevice action_input_device_from_dx_scancode(
	std::uint32_t dx_scancode) noexcept
{
	if (dx_scancode >= 266U) {
		return ActionInputDevice::gamepad;
	}
	if (dx_scancode >= 256U) {
		return ActionInputDevice::mouse;
	}
	return ActionInputDevice::keyboard;
}

struct ActionDefinition {
	std::uint32_t id{ 0 };
	std::string display_name;
	std::string icon;
	std::uint32_t icon_form{ 0 };
	ActionKind kind{ ActionKind::physical_scancode };
	ActionTargetSource target{ ActionTargetSource::captured };
	ActionInputDevice captured_device{ ActionInputDevice::keyboard };
	std::uint32_t captured_scancode{ 0 };
	float stamina_cost{ 0.0f };
	float magicka_cost{ 0.0f };
	float health_cost{ 0.0f };
	float cooldown_days{ 0.0f };
	float gcd{ 0.0f };

	[[nodiscard]] bool is_costed() const noexcept;
};

struct ActionPlayerOverlay {
	std::string display_name;
	std::string icon;
	std::uint32_t icon_form{ 0 };
	ActionKind kind{ ActionKind::physical_scancode };
	ActionTargetSource target{ ActionTargetSource::captured };
	ActionInputDevice captured_device{ ActionInputDevice::keyboard };
	std::uint32_t captured_scancode{ 0 };
	float stamina_cost{ 0.0f };
	float magicka_cost{ 0.0f };
	float health_cost{ 0.0f };
	float cooldown_days{ 0.0f };
	float gcd{ 0.0f };
};

inline constexpr std::uint32_t custom_action_id_base = 100;
inline constexpr std::uint32_t custom_action_count = 12;

[[nodiscard]] constexpr bool is_visible_action_id(std::uint32_t id) noexcept
{
	return id >= custom_action_id_base && id < custom_action_id_base + custom_action_count;
}

[[nodiscard]] std::vector<ActionDefinition> default_action_catalogue();

[[nodiscard]] ActionInput resolve_action_input(const ActionDefinition& action) noexcept;

[[nodiscard]] constexpr bool action_would_recurse(
	std::uint32_t target_scancode, int triggering_scancode) noexcept
{
	return target_scancode != 0 && triggering_scancode >= 0 &&
		target_scancode == static_cast<std::uint32_t>(triggering_scancode);
}

/**
 * Decide whether an Action press may enter dispatch before its ordinary cost, cooldown, and
 * resource checks. A live cast protects the graph until the committed cuttable span. Inside that
 * span any costless Action is admitted, and accepting it cuts the committed cast -- mirroring what
 * the physical key would do had the player pressed it directly. SH2 does not classify an Action as
 * an attack: policing which key events "should" cut is not this framework's job. A costed Action
 * never cuts and stays refused while any cast is live, so its own GCD can never stack on one.
 * Outside the cuttable span -- charging, pre-commit -- a live cast refuses every Action. A retired
 * follow-through has no live casting instance and therefore does not reopen the whole-instance
 * gate. An Ability whose graph has already returned to ready is the same: the instance may still
 * be waiting out GCD, but it no longer holds the graph, so a costless Action is admitted. A costed
 * Action still waits, so its own GCD cannot stack on the leftover instance. Keeping this pure
 * makes the safety rule testable without constructing the native casting controller.
 */
[[nodiscard]] constexpr bool action_press_is_admitted(
	bool action_costed,
	bool has_live_cast,
	bool committed_cuttable,
	bool ability_graph_released = false) noexcept
{
	return !has_live_cast || (!action_costed && (committed_cuttable || ability_graph_released));
}

void apply_action_player_overlay(ActionDefinition& action, const ActionPlayerOverlay& overlay);

[[nodiscard]] ActionPlayerOverlay action_player_overlay_from(const ActionDefinition& action);

[[nodiscard]] bool action_matches_catalogue(
	const ActionDefinition& live, const ActionDefinition& catalogue) noexcept;

[[nodiscard]] constexpr const char* action_target_label(ActionTargetSource target) noexcept
{
	switch (target) {
	case ActionTargetSource::captured:
		return "Captured scancode";
	}
	return "Captured scancode";
}

}  // namespace SpellHotbar
