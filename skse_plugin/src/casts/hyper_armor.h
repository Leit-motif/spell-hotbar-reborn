#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

#include "attack_seam.h"

// Ability hyperarmor: while `ArtDriver::is_active()`, the player shrugs off stagger and takes
// reduced weapon damage. Both halves read that flag live, so every exit path the driver has ends
// the armor without code of its own.
//
// This header is the pure part: which graph events count as a stagger, what the perk's Mod
// Incoming Damage value is, and the co-save tail for the two FLICK settings. The runtime half --
// the graph hook line, the perk write -- is `hyper_armor_sync.h` and `attack_seam_hook.cpp`.
namespace SpellHotbar::casts::HyperArmor {

inline constexpr bool kStaggerImmunityDefault = true;
// Percent, as the FLICK slider shows it. 0 turns damage reduction off.
inline constexpr float kDamageReductionDefault = 50.0f;
inline constexpr float kDamageReductionMin = 0.0f;
inline constexpr float kDamageReductionMax = 100.0f;

// Every stagger on this stack except ragdoll ends in a notify to the player's graph. The engine,
// Chocolate Poise and Parry for All send `staggerStart`; For Honor's Papyrus sends `StaggerStart`;
// Elden Parry sends the poise family (names read out of EldenParry.dll). `recoilStart` and
// `recoilLargeStart` are left out on purpose, so a parry still breaks an Ability.
inline constexpr std::array<std::string_view, 9> kStaggerEvents{
	"staggerStart",
	"poise_small_start",
	"poise_med_start",
	"poise_large_start",
	"poise_largest_start",
	"poise_small_start_fwd",
	"poise_med_start_fwd",
	"poise_large_start_fwd",
	"poise_largest_start_fwd",
};

[[nodiscard]] constexpr bool is_stagger_event(std::string_view event) noexcept
{
	for (const auto name : kStaggerEvents) {
		if (event.size() == name.size() && ascii_istarts_with(event, name)) {
			return true;
		}
	}
	return false;
}

// True when the player graph hook should drop `event` instead of forwarding it.
[[nodiscard]] constexpr bool hyper_armor_swallows(bool enabled, bool ability_live, std::string_view event) noexcept
{
	return enabled && ability_live && is_stagger_event(event);
}

[[nodiscard]] inline float sanitize_damage_reduction(float percent) noexcept
{
	if (!std::isfinite(percent)) {
		return kDamageReductionDefault;
	}
	if (percent < kDamageReductionMin) {
		return kDamageReductionMin;
	}
	if (percent > kDamageReductionMax) {
		return kDamageReductionMax;
	}
	return percent;
}

// The value the DLL writes into the perk's Mod Incoming Damage entry: `1 - reduction` while an
// Ability is live, identity otherwise.
[[nodiscard]] inline float incoming_damage_multiplier(bool ability_live, float reduction_percent) noexcept
{
	if (!ability_live) {
		return 1.0f;
	}
	return 1.0f - sanitize_damage_reduction(reduction_percent) / 100.0f;
}

// The value the DLL writes into the perk's Mod Incoming Stagger entry. Zero stops the engine from
// requesting the stagger at all, so there is nothing for a sender to retry; the graph hook alone
// only postpones it to the Ability's exit.
[[nodiscard]] constexpr float incoming_stagger_multiplier(bool enabled, bool ability_live) noexcept
{
	return enabled && ability_live ? 0.0f : 1.0f;
}

// HOTB record format 10 appends these two settings after the in-menu dock's format-9 fields.
inline constexpr std::uint32_t kHyperArmorSaveFormat = 10U;

struct HyperArmorSettings {
	bool stagger_immunity{ kStaggerImmunityDefault };
	float damage_reduction{ kDamageReductionDefault };
};

template <class Write>
bool write_hyper_armor_settings(Write&& write, const HyperArmorSettings& settings)
{
	return write(&settings.stagger_immunity, sizeof(settings.stagger_immunity)) &&
		write(&settings.damage_reduction, sizeof(settings.damage_reduction));
}

// A save older than format 10 reads nothing and gets the defaults, leaving the stream where the
// keybind count begins.
template <class Read>
bool read_hyper_armor_settings(Read&& read, std::uint32_t version, HyperArmorSettings& settings)
{
	settings = {};
	if (version < kHyperArmorSaveFormat) {
		return true;
	}
	std::uint8_t immune{ 0 };
	float reduction{ kDamageReductionDefault };
	if (!read(&immune, sizeof(immune)) || !read(&reduction, sizeof(reduction))) {
		return false;
	}
	settings.stagger_immunity = immune != 0;
	settings.damage_reduction = sanitize_damage_reduction(reduction);
	return true;
}

}  // namespace SpellHotbar::casts::HyperArmor
