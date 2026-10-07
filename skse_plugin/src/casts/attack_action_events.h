#pragma once

#include <string_view>

#include "../game_data/action_definition.h"

namespace SpellHotbar::casts {

// The graph names an attack Action sends, and the direction rule that picks one.
//
// WHY THE GRAPH PATH. An Action of kind `physical_scancode` injects a ButtonEvent, and BFCO reads
// the raw `idCode` off the input device rather than the control-map user event, so an Action bound
// to BFCO's power-attack key fires nothing at all. The player's animation graph is the seam every
// combat mod does share: sending `attackStart` / `attackPowerStartInPlace` plays BFCO_Attack1 /
// BFCO_PowerAttack1 and the combo chains (measured live), and the same names are what vanilla's
// AttackBlockHandler and MCO send.
//
// WHY THE DIRECTION IS READ AT PRESS TIME. Vanilla decides a power attack's direction from the
// movement input at the instant of the press, and BFCO ships one clip per direction, so reading
// `PlayerControlsData::moveInputVec` when the Action is pressed is exactly parity with the key.
// Its Nemesis patch accepts InPlace / Forward / Backward / Left / Right. The bare
// `attackPowerStart` is inert -- never send it.
//
// KNOWN CAVEAT. Going straight to the graph bypasses BFCO.dll's own press-time checks: its stamina
// gate and its input buffer never run, so a power attack with no stamina still plays the clip.
// That is deliberate -- SH2's own Action cost is the player's lever, and the editor's help text
// says so.

enum class AttackDirection {
	in_place,
	forward,
	backward,
	left,
	right,
};

/**
 * Vanilla AttackBlockHandler's rule. `x` is strafe (+right) and `y` is forward (+forward), as in
 * `RE::PlayerControlsData::moveInputVec`. Below the dead zone on both axes the attack is in place;
 * otherwise the larger axis wins, and a tie goes to forward/backward the way the engine's `>=`
 * comparison does.
 */
[[nodiscard]] constexpr AttackDirection attack_direction_from_move_input(
	float x, float y, float dead_zone = 0.1f) noexcept
{
	const float ax = x < 0.0f ? -x : x;
	const float ay = y < 0.0f ? -y : y;
	if (ax < dead_zone && ay < dead_zone) {
		return AttackDirection::in_place;
	}
	if (ay >= ax) {
		return y > 0.0f ? AttackDirection::forward : AttackDirection::backward;
	}
	return x > 0.0f ? AttackDirection::right : AttackDirection::left;
}

[[nodiscard]] constexpr std::string_view power_attack_event_name(AttackDirection dir) noexcept
{
	switch (dir) {
	case AttackDirection::in_place:
		return "attackPowerStartInPlace";
	case AttackDirection::forward:
		return "attackPowerStartForward";
	case AttackDirection::backward:
		return "attackPowerStartBackward";
	case AttackDirection::left:
		return "attackPowerStartLeft";
	case AttackDirection::right:
		return "attackPowerStartRight";
	}
	return "attackPowerStartInPlace";
}

/**
 * The right-hand light attack. The left-hand (`attackStartLeftHand`) and dual-wield
 * (`attackStartDualWield`) variants are NOT chosen here: choosing between them needs the equipped
 * hands read at press time, which is a separate decision from the direction rule above.
 */
[[nodiscard]] constexpr std::string_view light_attack_event_name() noexcept
{
	return "attackStart";
}

/**
 * The event a graph-kind Action sends. Empty for `physical_scancode`, which has no graph event --
 * it goes through the input queue instead.
 */
[[nodiscard]] constexpr std::string_view attack_action_event_name(
	ActionKind kind, AttackDirection dir) noexcept
{
	switch (kind) {
	case ActionKind::light_attack:
		return light_attack_event_name();
	case ActionKind::power_attack:
		return power_attack_event_name(dir);
	case ActionKind::physical_scancode:
		break;
	}
	return {};
}

[[nodiscard]] constexpr const char* attack_direction_label(AttackDirection dir) noexcept
{
	switch (dir) {
	case AttackDirection::in_place:
		return "in place";
	case AttackDirection::forward:
		return "forward";
	case AttackDirection::backward:
		return "backward";
	case AttackDirection::left:
		return "left";
	case AttackDirection::right:
		return "right";
	}
	return "in place";
}

}  // namespace SpellHotbar::casts
