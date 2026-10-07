#pragma once

#include <cstddef>
#include <string_view>

namespace SpellHotbar::casts {

// What an outgoing player graph event means to the cast cut.
//
// The seam sits downstream of the key, where every attack source looks the same: vanilla's
// AttackBlockHandler, One Click Power Attack, MCO's directional power attacks and TK Dodge RE
// all reach the player's behaviour graph by sending one of these names. Reading a mod's config
// to guess which key was an attack is what this replaces.
//
// A kind other than `none` is reported, not policed: the rule is parity with the physical key,
// so the seam ends whatever SH2 state is holding the graph and lets the event through.
enum class AttackSeamKind {
	none,
	attack,
	power_attack,
	bash,
	block,
	dodge,
};

// Prefix rule, not a per-mod list. The names carry a suffix per weapon, hand, and direction
// (`attackStartLeftHand`, `attackStartSprint`, `attackStartDualWield`,
// `attackPowerStartInPlace`, `attackPowerStartForward`, `TKDodgeForward`), so matching the stem
// covers sources this build has never seen.
//
// The engine-side stems are matched WITHOUT regard to case. BFCO spells them inconsistently:
// its idle leaves carry `attackstart` (the `typeMCO` leaf) next to `attackStart`, and its own
// combo continuation is `BFCOAttackstart_1` beside a `BFCOAttackStart_Comb` in the DLL. The
// dodge stems stay exact, because a lowercase `dodge*` is not TK Dodge RE's and nothing has
// been seen to send one.
//
// BFCO needs one name of its own. Its attacks reach this virtual like everyone else's -- the
// send-side census saw a light attack arrive as `attackStart` immediately followed by
// `BFCOAttackstart_1`, the power attack as `attackPowerStartInPlace`, and the block as
// `blockStart`. The `BFCOAttackstart_N` family is the combo step, which a continuation can send
// on its own, so it is a light attack here. `BFCO_MoveStart` and the rest of the `BFCO_*` graph
// chatter are not attacks.
//
// Order matters in exactly one place: `attackPowerStart` is also an `attackStart` prefix match
// if `attackStart` is tested first, and a power attack must not be reported as a light one.
//
// Only the STARTS match. `attackStop`, `attackRelease`, `bashRelease` and `blockStop` are the
// end of a swing the seam already saw, and SH2's own `SH2_*` notifies -- including the ones the
// teardown itself sends -- are never an attack.

[[nodiscard]] constexpr bool ascii_istarts_with(std::string_view name, std::string_view prefix) noexcept
{
	if (name.size() < prefix.size()) {
		return false;
	}
	for (std::size_t i = 0; i < prefix.size(); ++i) {
		char a = name[i];
		char b = prefix[i];
		if (a >= 'A' && a <= 'Z') {
			a = static_cast<char>(a - 'A' + 'a');
		}
		if (b >= 'A' && b <= 'Z') {
			b = static_cast<char>(b - 'A' + 'a');
		}
		if (a != b) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] constexpr AttackSeamKind classify_attack_seam_event(std::string_view name) noexcept
{
	if (ascii_istarts_with(name, "attackPowerStart")) {
		return AttackSeamKind::power_attack;
	}
	if (ascii_istarts_with(name, "attackStart") || ascii_istarts_with(name, "BFCOAttackStart_")) {
		return AttackSeamKind::attack;
	}
	if (ascii_istarts_with(name, "bashStart")) {
		return AttackSeamKind::bash;
	}
	if (ascii_istarts_with(name, "blockStart")) {
		return AttackSeamKind::block;
	}
	if (name.starts_with("TKDodge") || name.starts_with("Dodge")) {
		return AttackSeamKind::dodge;
	}
	return AttackSeamKind::none;
}

[[nodiscard]] constexpr const char* attack_seam_kind_label(AttackSeamKind kind) noexcept
{
	switch (kind) {
	case AttackSeamKind::none:
		return "none";
	case AttackSeamKind::attack:
		return "attack";
	case AttackSeamKind::power_attack:
		return "power attack";
	case AttackSeamKind::bash:
		return "bash";
	case AttackSeamKind::block:
		return "block";
	case AttackSeamKind::dodge:
		return "dodge";
	}
	return "none";
}

}  // namespace SpellHotbar::casts
