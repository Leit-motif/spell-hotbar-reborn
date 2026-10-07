#include "hyper_armor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

using SpellHotbar::casts::HyperArmor::HyperArmorSettings;
using SpellHotbar::casts::HyperArmor::hyper_armor_swallows;
using SpellHotbar::casts::HyperArmor::incoming_damage_multiplier;
using SpellHotbar::casts::HyperArmor::incoming_stagger_multiplier;
using SpellHotbar::casts::HyperArmor::kDamageReductionDefault;
using SpellHotbar::casts::HyperArmor::kHyperArmorSaveFormat;
using SpellHotbar::casts::HyperArmor::kStaggerEvents;
using SpellHotbar::casts::HyperArmor::kStaggerImmunityDefault;
using SpellHotbar::casts::HyperArmor::read_hyper_armor_settings;
using SpellHotbar::casts::HyperArmor::sanitize_damage_reduction;
using SpellHotbar::casts::HyperArmor::write_hyper_armor_settings;

namespace {

int g_failures = 0;

void expect(bool cond, const char* msg)
{
	if (!cond) {
		std::cerr << "FAIL: " << msg << '\n';
		++g_failures;
	}
}

// The whole stagger set, spelled the way its senders spell it. Elden Parry's names are the
// strings in EldenParry.dll; For Honor's `StaggerStart` is covered by the case test below.
constexpr std::array<std::string_view, 9> k_stagger_set{
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

void the_header_set_is_the_tested_set()
{
	// This list is an independent oracle, so it is typed out rather than read from the header.
	// A name added to one and not the other fails here.
	expect(kStaggerEvents.size() == k_stagger_set.size(), "the header's stagger set matches this test's");
	for (const auto event : kStaggerEvents) {
		bool listed = false;
		for (const auto expected : k_stagger_set) {
			listed = listed || event == expected;
		}
		expect(listed, "every header stagger event is spelled as this test spells it");
	}
}

void every_stagger_event_is_swallowed_mid_ability()
{
	for (const auto event : k_stagger_set) {
		expect(hyper_armor_swallows(true, true, event), "a stagger-set event is swallowed mid-Ability");
	}
}

void the_compare_ignores_case()
{
	expect(hyper_armor_swallows(true, true, "StaggerStart"), "For Honor's StaggerStart is swallowed");
	expect(hyper_armor_swallows(true, true, "STAGGERSTART"), "upper case is swallowed");
	expect(hyper_armor_swallows(true, true, "Poise_Large_Start_Fwd"), "mixed-case poise is swallowed");
}

void recoil_passes_so_a_parry_still_breaks_the_ability()
{
	expect(!hyper_armor_swallows(true, true, "recoilStart"), "recoilStart passes through");
	expect(!hyper_armor_swallows(true, true, "recoilLargeStart"), "recoilLargeStart passes through");
}

void nothing_is_swallowed_while_idle_or_switched_off()
{
	for (const auto event : k_stagger_set) {
		expect(!hyper_armor_swallows(true, false, event), "idle: stagger reaches the graph");
		expect(!hyper_armor_swallows(false, true, event), "setting off: stagger reaches the graph");
		expect(!hyper_armor_swallows(false, false, event), "idle and off: stagger reaches the graph");
	}
}

void only_whole_names_match()
{
	expect(!hyper_armor_swallows(true, true, "staggerStop"), "staggerStop is not a stagger start");
	expect(!hyper_armor_swallows(true, true, "staggerStartX"), "a longer name is not in the set");
	expect(!hyper_armor_swallows(true, true, "stagger"), "a prefix is not in the set");
	expect(!hyper_armor_swallows(true, true, ""), "the empty name is not in the set");
	expect(!hyper_armor_swallows(true, true, "SH2_ArtExit"), "SH2's own exit is never swallowed");
	expect(!hyper_armor_swallows(true, true, "attackStart"), "an attack is never swallowed");
}

void damage_multiplier_is_one_minus_reduction_while_live()
{
	expect(incoming_damage_multiplier(true, 50.0f) == 0.5f, "50 % mid-Ability is x0.5");
	expect(incoming_damage_multiplier(true, 0.0f) == 1.0f, "0 % mid-Ability is x1.0");
	expect(incoming_damage_multiplier(true, 100.0f) == 0.0f, "100 % mid-Ability is x0.0");
	expect(incoming_damage_multiplier(true, 25.0f) == 0.75f, "25 % mid-Ability is x0.75");
}

void damage_multiplier_is_identity_while_idle()
{
	expect(incoming_damage_multiplier(false, 50.0f) == 1.0f, "idle is x1.0 at 50 %");
	expect(incoming_damage_multiplier(false, 100.0f) == 1.0f, "idle is x1.0 at 100 %");
}

void stagger_multiplier_is_zero_only_while_armored()
{
	expect(incoming_stagger_multiplier(true, true) == 0.0f, "immunity on mid-Ability zeroes incoming stagger");
	expect(incoming_stagger_multiplier(true, false) == 1.0f, "idle keeps stagger at identity");
	expect(incoming_stagger_multiplier(false, true) == 1.0f, "immunity off keeps stagger at identity");
	expect(incoming_stagger_multiplier(false, false) == 1.0f, "idle and off keeps stagger at identity");
}

void reduction_is_clamped_and_non_finite_falls_back()
{
	expect(sanitize_damage_reduction(-10.0f) == 0.0f, "below 0 % clamps to 0 %");
	expect(sanitize_damage_reduction(250.0f) == 100.0f, "above 100 % clamps to 100 %");
	expect(sanitize_damage_reduction(std::numeric_limits<float>::quiet_NaN()) == kDamageReductionDefault,
		"NaN falls back to the default");
	expect(sanitize_damage_reduction(std::numeric_limits<float>::infinity()) == kDamageReductionDefault,
		"infinity falls back to the default");
	expect(incoming_damage_multiplier(true, std::numeric_limits<float>::quiet_NaN()) == 0.5f,
		"a NaN setting cannot poison the perk value");
}

// Byte buffer standing in for SKSE's record stream.
class Bytes {
public:
	bool write(const void* source, std::size_t size)
	{
		const auto* p = static_cast<const std::uint8_t*>(source);
		data_.insert(data_.end(), p, p + size);
		return true;
	}

	bool read(void* destination, std::size_t size)
	{
		if (offset_ + size > data_.size()) {
			return false;
		}
		std::memcpy(destination, data_.data() + offset_, size);
		offset_ += size;
		return true;
	}

	[[nodiscard]] std::size_t size() const { return data_.size(); }
	[[nodiscard]] std::size_t offset() const { return offset_; }
	std::vector<std::uint8_t>& raw() { return data_; }

private:
	std::vector<std::uint8_t> data_;
	std::size_t offset_{ 0 };
};

void settings_round_trip_at_the_new_format()
{
	Bytes bytes;
	auto write = [&bytes](const void* p, std::size_t n) { return bytes.write(p, n); };
	auto read = [&bytes](void* p, std::size_t n) { return bytes.read(p, n); };

	const HyperArmorSettings saved{ .stagger_immunity = false, .damage_reduction = 35.0f };
	expect(write_hyper_armor_settings(write, saved), "the settings write");
	expect(bytes.size() == sizeof(bool) + sizeof(float), "the tail is one bool and one float");

	HyperArmorSettings loaded{};
	expect(read_hyper_armor_settings(read, kHyperArmorSaveFormat, loaded), "the settings read back");
	expect(!loaded.stagger_immunity, "the checkbox round-trips");
	expect(loaded.damage_reduction == 35.0f, "the slider round-trips");
	expect(bytes.offset() == bytes.size(), "the read consumes exactly what was written");
}

void an_old_format_save_loads_the_defaults_and_reads_nothing()
{
	// A format-9 HOTB record ends before this tail; the next bytes belong to the keybind count.
	Bytes bytes;
	const std::uint8_t keybind_count = 23;
	bytes.write(&keybind_count, sizeof(keybind_count));
	auto read = [&bytes](void* p, std::size_t n) { return bytes.read(p, n); };

	HyperArmorSettings loaded{ .stagger_immunity = false, .damage_reduction = 5.0f };
	expect(read_hyper_armor_settings(read, kHyperArmorSaveFormat - 1U, loaded), "format 9 is readable");
	expect(loaded.stagger_immunity == kStaggerImmunityDefault, "format 9 gets the default checkbox");
	expect(loaded.damage_reduction == kDamageReductionDefault, "format 9 gets the default slider");
	expect(bytes.offset() == 0, "format 9 leaves the keybind count unread");
}

void a_corrupt_value_is_clamped_on_load()
{
	Bytes bytes;
	auto write = [&bytes](const void* p, std::size_t n) { return bytes.write(p, n); };
	auto read = [&bytes](void* p, std::size_t n) { return bytes.read(p, n); };

	const HyperArmorSettings saved{ .stagger_immunity = true, .damage_reduction = 900.0f };
	write_hyper_armor_settings(write, saved);
	bytes.raw()[0] = 7;  // a bool byte that is neither 0 nor 1

	HyperArmorSettings loaded{};
	expect(read_hyper_armor_settings(read, kHyperArmorSaveFormat, loaded), "a corrupt tail still reads");
	expect(loaded.damage_reduction == 100.0f, "an out-of-range slider clamps on load");
	expect(loaded.stagger_immunity, "a non-zero bool byte reads as on");
}

void a_truncated_tail_fails()
{
	Bytes bytes;
	const bool immune = true;
	bytes.write(&immune, sizeof(immune));
	auto read = [&bytes](void* p, std::size_t n) { return bytes.read(p, n); };

	HyperArmorSettings loaded{};
	expect(!read_hyper_armor_settings(read, kHyperArmorSaveFormat, loaded), "a missing float fails the read");
}

}  // namespace

int main()
{
	the_header_set_is_the_tested_set();
	every_stagger_event_is_swallowed_mid_ability();
	the_compare_ignores_case();
	recoil_passes_so_a_parry_still_breaks_the_ability();
	nothing_is_swallowed_while_idle_or_switched_off();
	only_whole_names_match();
	damage_multiplier_is_one_minus_reduction_while_live();
	damage_multiplier_is_identity_while_idle();
	stagger_multiplier_is_zero_only_while_armored();
	reduction_is_clamped_and_non_finite_falls_back();
	settings_round_trip_at_the_new_format();
	an_old_format_save_loads_the_defaults_and_reads_nothing();
	a_corrupt_value_is_clamped_on_load();
	a_truncated_tail_fails();

	if (g_failures > 0) {
		std::cerr << g_failures << " failure(s)\n";
		return EXIT_FAILURE;
	}
	std::cout << "hyper_armor_test passed\n";
	return EXIT_SUCCESS;
}
