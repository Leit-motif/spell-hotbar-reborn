#include "attack_seam.h"

#include <cstdlib>
#include <iostream>

#include "attack_action_events.h"

using SpellHotbar::ActionKind;
using SpellHotbar::casts::AttackDirection;
using SpellHotbar::casts::attack_action_event_name;
using SpellHotbar::casts::attack_direction_from_move_input;
using SpellHotbar::casts::light_attack_event_name;
using SpellHotbar::casts::power_attack_event_name;
using SpellHotbar::casts::AttackSeamKind;
using SpellHotbar::casts::attack_seam_kind_label;
using SpellHotbar::casts::classify_attack_seam_event;

namespace {

int g_failures = 0;

void expect(bool cond, const char* msg)
{
	if (!cond) {
		std::cerr << "FAIL: " << msg << '\n';
		++g_failures;
	}
}

void every_start_prefix_is_matched()
{
	expect(classify_attack_seam_event("attackStart") == AttackSeamKind::attack,
		"attackStart is a light attack");
	expect(classify_attack_seam_event("bashStart") == AttackSeamKind::bash, "bashStart is a bash");
	expect(classify_attack_seam_event("blockStart") == AttackSeamKind::block,
		"blockStart is a block");
	expect(classify_attack_seam_event("attackPowerStart") == AttackSeamKind::power_attack,
		"attackPowerStart is a power attack");
	expect(classify_attack_seam_event("TKDodge") == AttackSeamKind::dodge, "TKDodge is a dodge");
	expect(classify_attack_seam_event("Dodge") == AttackSeamKind::dodge, "Dodge is a dodge");
}

void the_suffixed_names_the_sources_actually_send_are_matched()
{
	expect(classify_attack_seam_event("attackStartLeftHand") == AttackSeamKind::attack,
		"the left-hand swing is still an attack");
	expect(classify_attack_seam_event("attackStartSprint") == AttackSeamKind::attack,
		"the sprint attack is still an attack");
	expect(classify_attack_seam_event("attackStartDualWield") == AttackSeamKind::attack,
		"the dual-wield swing is still an attack");
	expect(classify_attack_seam_event("attackPowerStartInPlace") == AttackSeamKind::power_attack,
		"OCPA's standing power attack is a power attack");
	expect(classify_attack_seam_event("attackPowerStartForward") == AttackSeamKind::power_attack,
		"a directional power attack is a power attack");
	expect(classify_attack_seam_event("attackPowerStartLeftHand") == AttackSeamKind::power_attack,
		"a left-hand power attack is a power attack");
	expect(classify_attack_seam_event("bashStartLeft") == AttackSeamKind::bash,
		"a suffixed bash is a bash");
	expect(classify_attack_seam_event("TKDodgeForward") == AttackSeamKind::dodge,
		"TK Dodge RE's directional dodges are dodges");
	expect(classify_attack_seam_event("DodgeBack") == AttackSeamKind::dodge,
		"a bare Dodge prefix is a dodge");
}

void the_names_bfco_sends_are_matched()
{
	// Measured on a live profile: one light attack, one power attack, one block.
	expect(classify_attack_seam_event("BFCOAttackstart_1") == AttackSeamKind::attack,
		"BFCO's combo step is a light attack");
	expect(classify_attack_seam_event("BFCOAttackStart_Comb") == AttackSeamKind::attack,
		"BFCO's other spelling of the combo step is a light attack");
	expect(classify_attack_seam_event("attackstart") == AttackSeamKind::attack,
		"BFCO's lowercase typeMCO leaf is a light attack");
	expect(classify_attack_seam_event("attackpowerstartinplace") == AttackSeamKind::power_attack,
		"the engine stems match regardless of case");
	expect(classify_attack_seam_event("BFCO_MoveStart") == AttackSeamKind::none,
		"BFCO's movement chatter is not an attack");
	expect(classify_attack_seam_event("BFCO_NextNormal") == AttackSeamKind::none,
		"a BFCO graph variable name is not an attack");
	expect(classify_attack_seam_event("CastOKStart") == AttackSeamKind::none,
		"CastOKStart is not a start the seam owns");
}

void power_is_tested_before_the_plain_attack_prefix()
{
	// The whole point of the ordering: `attackPowerStart*` also begins with `attackStart` only
	// if the plain prefix is tested first, which would report every power attack as a light one.
	expect(classify_attack_seam_event("attackPowerStartInPlace") != AttackSeamKind::attack,
		"a power attack is never reported as a light attack");
	expect(attack_seam_kind_label(classify_attack_seam_event("attackPowerStartInPlace")) ==
			std::string_view{ "power attack" },
		"the label follows the classification");
}

void the_excluded_names_are_not_the_seam()
{
	expect(classify_attack_seam_event("attackStop") == AttackSeamKind::none,
		"attackStop ends a swing the seam already saw");
	expect(classify_attack_seam_event("attackRelease") == AttackSeamKind::none,
		"attackRelease is not a start");
	expect(classify_attack_seam_event("bashRelease") == AttackSeamKind::none,
		"bashRelease is not a start");
	expect(classify_attack_seam_event("blockStop") == AttackSeamKind::none,
		"blockStop is not a start");
	expect(classify_attack_seam_event("SH2_CastExit") == AttackSeamKind::none,
		"SH2's own teardown notify is never an attack");
	expect(classify_attack_seam_event("SH2_ArtStart") == AttackSeamKind::none,
		"SH2_ArtStart is never an attack");
	expect(classify_attack_seam_event("MCO_AttackInitiate") == AttackSeamKind::none,
		"an MCO annotation is not a send-side start");
	expect(classify_attack_seam_event("IdleForceDefaultState") == AttackSeamKind::none,
		"an unrelated notify passes through");
	// Case matters: the graph names are exact, and a lowercase `dodge` is not TK Dodge RE's.
	expect(classify_attack_seam_event("dodgeStart") == AttackSeamKind::none,
		"the dodge match is case-sensitive on the shipped names");
}

void an_empty_name_is_none()
{
	expect(classify_attack_seam_event("") == AttackSeamKind::none, "an empty event name is none");
	expect(classify_attack_seam_event(std::string_view{}) == AttackSeamKind::none,
		"a default-constructed view is none");
}

// The graph kinds an Action can send travel this very seam, so every name the sender can produce
// has to classify as the attack it is -- otherwise a graph-kind Action would deliver a swing that
// never cuts the cast.
void every_name_an_attack_action_sends_is_seen_by_the_seam()
{
	constexpr AttackDirection directions[]{
		AttackDirection::in_place, AttackDirection::forward, AttackDirection::backward,
		AttackDirection::left, AttackDirection::right,
	};
	for (const auto dir : directions) {
		const auto name = power_attack_event_name(dir);
		expect(classify_attack_seam_event(name) == AttackSeamKind::power_attack,
			"every direction's power-attack name classifies as a power attack");
		// The bare stem is inert in BFCO's graph. Sending it would look like a delivered attack
		// and play nothing, so no direction may ever produce it.
		expect(name != std::string_view{ "attackPowerStart" },
			"no sent power name is the inert bare attackPowerStart");
		expect(attack_action_event_name(ActionKind::power_attack, dir) == name,
			"the power_attack kind sends that direction's name");
	}
	expect(classify_attack_seam_event(light_attack_event_name()) == AttackSeamKind::attack,
		"the light-attack name classifies as a light attack");
	expect(attack_action_event_name(ActionKind::light_attack, AttackDirection::forward) ==
			light_attack_event_name(),
		"the light attack ignores direction");
	expect(attack_action_event_name(ActionKind::physical_scancode, AttackDirection::in_place)
			.empty(),
		"the key bridge has no graph event");
}

void the_power_direction_follows_the_movement_input()
{
	expect(attack_direction_from_move_input(0.0f, 0.0f) == AttackDirection::in_place,
		"a still player attacks in place");
	expect(attack_direction_from_move_input(0.0f, 1.0f) == AttackDirection::forward,
		"full forward is a forward power attack");
	expect(attack_direction_from_move_input(0.0f, -1.0f) == AttackDirection::backward,
		"full back is a backward power attack");
	expect(attack_direction_from_move_input(1.0f, 0.0f) == AttackDirection::right,
		"full right strafe is a right power attack");
	expect(attack_direction_from_move_input(-1.0f, 0.0f) == AttackDirection::left,
		"full left strafe is a left power attack");
	expect(attack_direction_from_move_input(0.05f, 0.05f) == AttackDirection::in_place,
		"stick drift inside the dead zone is still in place");
	expect(attack_direction_from_move_input(0.7f, 0.7f) == AttackDirection::forward,
		"a diagonal tie goes forward, matching the engine's >= comparison");
	expect(attack_direction_from_move_input(0.3f, -0.9f) == AttackDirection::backward,
		"the larger axis wins a mixed diagonal");
}

}  // namespace

int main()
{
	every_start_prefix_is_matched();
	every_name_an_attack_action_sends_is_seen_by_the_seam();
	the_power_direction_follows_the_movement_input();
	the_suffixed_names_the_sources_actually_send_are_matched();
	the_names_bfco_sends_are_matched();
	power_is_tested_before_the_plain_attack_prefix();
	the_excluded_names_are_not_the_seam();
	an_empty_name_is_none();

	if (g_failures != 0) {
		std::cerr << g_failures << " failure(s)\n";
		return EXIT_FAILURE;
	}
	std::cout << "ok\n";
	return EXIT_SUCCESS;
}
