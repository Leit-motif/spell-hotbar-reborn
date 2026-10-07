#pragma once

#include <cstdint>

namespace SpellHotbar::GameData {

// Which of a family's two ids the caller wants. The slot is named by the caller, not by the
// family: a one-handed spell asks for `variant` when it dual casts, a ritual asks for it when
// the cast is fast enough to use the short clip.
enum class CastAnimSlot {
	primary,
	variant,
};

// One animation family: the ordinary id and the id for the variant slot. Each id names an OAR
// submod under `meshes/actors/character/OpenAnimationReplacer/SpellHotbar2/`, matched on
// `SpellHotbar_SpellAnimationType` (`SpellHotbar.esp` form `815`).
struct CastAnimFamily {
	std::uint16_t primary;
	std::uint16_t variant;
};

[[nodiscard]] constexpr std::uint16_t anim_id(const CastAnimFamily& family, CastAnimSlot slot) noexcept
{
	return slot == CastAnimSlot::variant ? family.variant : family.primary;
}

// Which slot a fire-and-forget cast through the ritual path takes. The variant slot serves
// two different axes: for a ONE-HANDED spell it is the dual id, and dual is a hand value,
// not a speed — a dual cast presents dual at any cast time. Only a genuine two-handed ritual
// picks by speed, borrowing the dual id for the short clip.
[[nodiscard]] constexpr CastAnimSlot ritual_cast_slot(bool two_handed, bool dual_cast, bool is_fast_cast) noexcept
{
	if (dual_cast && !two_handed) {
		return CastAnimSlot::variant;
	}
	return is_fast_cast ? CastAnimSlot::variant : CastAnimSlot::primary;
}

// Fire-and-forget. Aimed and ritual share the fast id 10016, self and ritual-self share 10017.
// That is upstream's convention for the variant slot and it stays.
inline constexpr CastAnimFamily kAimed{ 1U, 10016U };
inline constexpr CastAnimFamily kSelf{ 2U, 10017U };
inline constexpr CastAnimFamily kRitual{ 10000U, 10016U };
inline constexpr CastAnimFamily kRitualSelf{ 10000U, 10017U };

// Concentration keeps its families apart in both slots. A channel loops because its submod
// replaces the idle and locomotion set for as long as `SpellHotbar_isCastingConcSpell` is
// raised, so an id borrowed from another family loops the wrong pose for the whole hold.
inline constexpr CastAnimFamily kAimedConc{ 1001U, 11003U };  // cast_1h_*_conc, cast_dual_conc
inline constexpr CastAnimFamily kSelfConc{ 1002U, 11004U };   // cast_1h_*_conc_self, cast_dual_conc_self

// Ritual concentration ships one submod, `cast_ritual_aimed_conc`, with no dual or fast
// partner. The variant slot therefore stays on the same id. It used to be 11003, which loops
// the plain dual concentration pose for a fast ritual channel.
inline constexpr CastAnimFamily kRitualConc{ 11001U, 11001U };

// Ward concentration ships `cast_1h_left_conc_ward` and `cast_1h_right_conc_ward`. Vanilla has
// no dual ward pose and this fork authors no animation assets, so both slots take the
// single-hand ward loop rather than borrowing another family's clip.
inline constexpr CastAnimFamily kWardConc{ 1003U, 1003U };

// Is this id one of the four structural dual ids? A spell row's Animation2 is honored for a
// dual cast ONLY when it names one of these — the column doubles as upstream's variant /
// first-person anim id (Firebolt carries 10001), and those must not reach a dual cell. This
// keeps the intentional override and drops the broken ones.
[[nodiscard]] constexpr bool is_dual_family_id(int id) noexcept
{
	return id == kAimed.variant || id == kSelf.variant ||
		   id == kAimedConc.variant || id == kSelfConc.variant;
}

[[nodiscard]] constexpr CastAnimFamily concentration_family(bool two_handed, bool self, bool ward) noexcept
{
	if (two_handed) {
		// Vanilla provides no ritual self concentration.
		return kRitualConc;
	}
	if (ward) {
		return kWardConc;
	}
	return self ? kSelfConc : kAimedConc;
}

[[nodiscard]] constexpr CastAnimFamily fire_and_forget_family(bool two_handed, bool self) noexcept
{
	if (two_handed) {
		return self ? kRitualSelf : kRitual;
	}
	return self ? kSelf : kAimed;
}

}  // namespace SpellHotbar::GameData
