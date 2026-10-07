#include "art_definition.h"
#include "../bar/equipped_bar_route.h"
#include "art_icon_resolve.h"
#include "custom_ability_config.h"

#include <cstdlib>
#include <iostream>
#include <string>

using SpellHotbar::ArtClass;
using SpellHotbar::ArtDefinition;
using SpellHotbar::ArtIconDrawKind;
using SpellHotbar::GameData::EquippedType;
using SpellHotbar::art_class_is_live;
using SpellHotbar::art_class_is_direct_on_bar;
using SpellHotbar::art_class_is_live_on_bar;
using SpellHotbar::art_bar_tint;
using SpellHotbar::art_bar_tint_for_player;
using SpellHotbar::art_equipped_tint;
using SpellHotbar::art_bar_pins_no_stance;
using SpellHotbar::ArtBarTint;
using SpellHotbar::parse_art_duration_days;
using SpellHotbar::parse_art_tsv;
using SpellHotbar::resolve_art_icon_draw_kind;

namespace {

int g_failures = 0;

void expect(bool cond, const char* msg)
{
	if (!cond) {
		std::cerr << "FAIL: " << msg << '\n';
		++g_failures;
	}
}

void a_row_loads_every_art_field()
{
	const char* tsv =
		"ArtID\tDisplayName\tIcon\tSelector\tArtClass\tStaminaCost\tCooldown\tGlobalCooldown\n"
		"1\tTest Ability\tGREATER_POWER\t1\tGeneric\t25\t8s\t1.0\n";
	const auto arts = parse_art_tsv(tsv);
	expect(arts.size() == 1, "one data row loads");
	expect(arts.size() == 1 && arts[0].id == 1, "ArtID is 1");
	expect(arts.size() == 1 && arts[0].display_name == "Test Ability", "DisplayName is Test Ability");
	expect(arts.size() == 1 && arts[0].icon == "GREATER_POWER", "Icon is GREATER_POWER");
	expect(arts.size() == 1 && arts[0].selector == 1, "Selector is 1");
	expect(arts.size() == 1 && arts[0].art_class == ArtClass::Generic, "ArtClass Generic is Generic");
	expect(arts.size() == 1 && arts[0].stamina_cost == 25.0f, "StaminaCost is 25");
	expect(arts.size() == 1 && arts[0].gcd == 1.0f, "GlobalCooldown is 1.0");
	expect(arts.size() == 1 && arts[0].cooldown_text == "8s", "Cooldown cell is kept as text");
	expect(arts.size() == 1 && arts[0].cooldown_days > 0.0f, "8s cooldown is positive days");
	expect(arts.size() == 1 && arts[0].magicka_cost == 0.0f, "missing MagickaCost is 0");
	expect(arts.size() == 1 && arts[0].health_cost == 0.0f, "missing HealthCost is 0");
}

void art_class_column_loads_each_legal_value()
{
	const char* tsv =
		"ArtID\tDisplayName\tIcon\tSelector\tArtClass\tStaminaCost\tCooldown\tGlobalCooldown\n"
		"2\tBlood Flurry\tGREATER_POWER\t2\t2H\t25\t8s\t1.0\n"
		"3\tIai Slash\tGREATER_POWER\t3\t1H\t25\t8s\t1.0\n"
		"4\tDual Flurry\tGREATER_POWER\t4\tDual\t25\t8s\t1.0\n"
		"5\tDisengage\tGREATER_POWER\t5\tGeneric\t25\t8s\t1.0\n"
		"901\tArcane Shot\tGREATER_POWER\t901\tBow\t25\t8s\t1.0\n";
	const auto arts = parse_art_tsv(tsv);
	expect(arts.size() == 5, "five class rows load");
	expect(arts.size() >= 1 && arts[0].art_class == ArtClass::TwoHand, "2H is TwoHand");
	expect(arts.size() >= 2 && arts[1].art_class == ArtClass::OneHand, "1H is OneHand");
	expect(arts.size() >= 3 && arts[2].art_class == ArtClass::Dual, "Dual is Dual");
	expect(arts.size() >= 4 && arts[3].art_class == ArtClass::Generic, "Generic is Generic");
	expect(arts.size() >= 5 && arts[4].art_class == ArtClass::Bow, "Bow is Bow");
}

void empty_art_class_defaults_generic()
{
	const char* tsv =
		"ArtID\tDisplayName\tIcon\tSelector\tArtClass\tStaminaCost\tCooldown\tGlobalCooldown\n"
		"8\tCustom Art\tGREATER_POWER\t8\t\t25\t8s\t1.0\n";
	const auto arts = parse_art_tsv(tsv);
	expect(arts.size() == 1 && arts[0].art_class == ArtClass::Generic,
		"an empty ArtClass cell is Generic");
}

void eight_seconds_is_spell_data_days()
{
	const auto days = parse_art_duration_days("8s");
	expect(days.has_value(), "8s parses");
	const float expected = 8.0f / (24.0f * 60.0f * 60.0f);
	expect(days.has_value() && *days > expected * 0.99f && *days < expected * 1.01f,
		"8s is 8 / 86400 days, same grammar as spell cooldowns");
}

void non_positive_cooldown_means_none()
{
	const auto zero = parse_art_duration_days("0");
	const auto neg = parse_art_duration_days("-1");
	expect(zero.has_value() && *zero < 0.0f, "0 cooldown is none");
	expect(neg.has_value() && *neg < 0.0f, "negative cooldown is none");
}

void missing_header_loads_nothing()
{
	const auto arts = parse_art_tsv("1\tTest Ability\tGREATER_POWER\t1\tGeneric\t25\t8s\t1.0\n");
	expect(arts.empty(), "a file without the required header is not an art catalogue");
}

void header_without_art_class_loads_nothing()
{
	const auto arts = parse_art_tsv(
		"ArtID\tDisplayName\tIcon\tSelector\tStaminaCost\tCooldown\tGlobalCooldown\n"
		"1\tTest Ability\tGREATER_POWER\t1\t25\t8s\t1.0\n");
	expect(arts.empty(), "a catalogue without an ArtClass column is not loaded");
}

void zero_id_and_empty_name_are_skipped()
{
	const char* tsv =
		"ArtID\tDisplayName\tIcon\tSelector\tArtClass\tStaminaCost\tCooldown\tGlobalCooldown\n"
		"0\tGhost\tGREATER_POWER\t1\tGeneric\t0\t-1\t1.0\n"
		"2\t\tGREATER_POWER\t2\tGeneric\t0\t-1\t1.0\n"
		"3\tReal\tGREATER_POWER\t3\tGeneric\t0\t-1\t1.0\n";
	const auto arts = parse_art_tsv(tsv);
	expect(arts.size() == 1, "only the named non-zero row remains");
	expect(arts.size() == 1 && arts[0].id == 3, "surviving row is ArtID 3");
}

void two_hand_art_is_live_only_on_two_hand()
{
	expect(art_class_is_live(ArtClass::TwoHand, EquippedType::TWOHAND), "2H live on greatsword");
	expect(!art_class_is_live(ArtClass::TwoHand, EquippedType::ONEHAND_EMPTY), "2H dead on rapier");
	expect(!art_class_is_live(ArtClass::TwoHand, EquippedType::DUAL_WIELD), "2H dead on dual 1H");
	expect(!art_class_is_live(ArtClass::TwoHand, EquippedType::FIST), "2H dead on fists");
	expect(!art_class_is_live(ArtClass::TwoHand, EquippedType::BOW), "2H dead on bow");
	expect(!art_class_is_live(ArtClass::TwoHand, EquippedType::SPELL), "2H dead on magic");
	expect(!art_class_is_live(ArtClass::TwoHand, EquippedType::STAFF_SHIELD), "2H dead on staff");
}

void one_hand_art_is_live_on_one_hand_variants()
{
	expect(art_class_is_live(ArtClass::OneHand, EquippedType::ONEHAND_EMPTY), "1H live on rapier");
	expect(art_class_is_live(ArtClass::OneHand, EquippedType::ONEHAND_SHIELD), "1H live with shield");
	expect(art_class_is_live(ArtClass::OneHand, EquippedType::ONEHAND_SPELL), "1H live with spell");
	expect(!art_class_is_live(ArtClass::OneHand, EquippedType::TWOHAND), "1H dead on greatsword");
	expect(!art_class_is_live(ArtClass::OneHand, EquippedType::DUAL_WIELD), "1H dead on dual 1H");
	expect(!art_class_is_live(ArtClass::OneHand, EquippedType::BOW), "1H dead on bow");
}

void dual_art_is_live_only_on_dual_wield()
{
	expect(art_class_is_live(ArtClass::Dual, EquippedType::DUAL_WIELD), "Dual live on two melee 1H");
	expect(!art_class_is_live(ArtClass::Dual, EquippedType::ONEHAND_EMPTY), "Dual dead on rapier");
	expect(!art_class_is_live(ArtClass::Dual, EquippedType::TWOHAND), "Dual dead on greatsword");
}

void generic_art_is_live_on_melee_and_fists_only()
{
	expect(art_class_is_live(ArtClass::Generic, EquippedType::ONEHAND_EMPTY), "Generic live on rapier");
	expect(art_class_is_live(ArtClass::Generic, EquippedType::TWOHAND), "Generic live on greatsword");
	expect(art_class_is_live(ArtClass::Generic, EquippedType::DUAL_WIELD), "Generic live on dual 1H");
	expect(art_class_is_live(ArtClass::Generic, EquippedType::FIST), "Generic live on fists");
	expect(!art_class_is_live(ArtClass::Generic, EquippedType::BOW), "Generic dead on bow");
	expect(!art_class_is_live(ArtClass::Generic, EquippedType::CROSSBOW), "Generic dead on crossbow");
	expect(!art_class_is_live(ArtClass::Generic, EquippedType::SPELL), "Generic dead on magic");
	expect(!art_class_is_live(ArtClass::Generic, EquippedType::STAFF_SHIELD), "Generic dead on staff");
}

void bow_art_requires_a_bow()
{
	expect(art_class_is_live(ArtClass::Bow, EquippedType::BOW), "Bow art is live with a bow");
	expect(!art_class_is_live(ArtClass::Bow, EquippedType::CROSSBOW), "Bow art is dead with a crossbow");
	expect(!art_class_is_live(ArtClass::Bow, EquippedType::TWOHAND), "Bow art is dead with a greatsword");
	expect(!art_class_is_live(ArtClass::Bow, EquippedType::FIST), "Bow art is dead unarmed");
	expect(std::string{SpellHotbar::art_class_label(ArtClass::Bow)} == "Bow", "Bow label is exposed in editor");
}

void only_bow_arts_need_ammo()
{
	using SpellHotbar::art_class_needs_ammo;
	expect(art_class_needs_ammo(ArtClass::Bow), "a Bow art needs arrows equipped");
	expect(!art_class_needs_ammo(ArtClass::OneHand), "1H arts need no ammo");
	expect(!art_class_needs_ammo(ArtClass::TwoHand), "2H arts need no ammo");
	expect(!art_class_needs_ammo(ArtClass::Dual), "Dual arts need no ammo");
	expect(!art_class_needs_ammo(ArtClass::Generic), "Generic arts need no ammo");
}

void one_hand_empty_routes_to_melee_and_dual_wield_stays_distinct()
{
	using SpellHotbar::Bars::stance_bar_for_equipped_type;
	using SpellHotbar::GameData::EquippedType;
	constexpr std::uint32_t melee_bar = static_cast<std::uint32_t>('MELE');
	constexpr std::uint32_t dual_wield_bar = static_cast<std::uint32_t>('1HDW');
	constexpr std::uint32_t dual_wield_sneak = dual_wield_bar + 1;
	expect(stance_bar_for_equipped_type(EquippedType::ONEHAND_EMPTY) == melee_bar,
		"one weapon with an empty offhand selects Melee");
	expect(stance_bar_for_equipped_type(EquippedType::DUAL_WIELD) == dual_wield_bar,
		"two melee weapons select Dual Wield");
	expect(!art_class_is_live_on_bar(ArtClass::OneHand, dual_wield_bar), "1H dead on Dual Wield bar");
	expect(art_class_is_live_on_bar(ArtClass::Dual, dual_wield_bar), "Dual live on Dual Wield bar");
	expect(art_class_is_live_on_bar(ArtClass::Generic, dual_wield_bar), "Generic live on Dual Wield bar");
	expect(!art_class_is_live_on_bar(ArtClass::TwoHand, dual_wield_bar), "2H dead on Dual Wield bar");
	expect(!art_class_is_live_on_bar(ArtClass::OneHand, dual_wield_sneak), "sneak Dual Wield matches parent");
}

void two_hand_bar_is_two_hand_and_generic_only()
{
	constexpr std::uint32_t two_hand_bar = static_cast<std::uint32_t>('2HND');
	expect(art_class_is_live_on_bar(ArtClass::TwoHand, two_hand_bar), "2H live on Two-Handed bar");
	expect(art_class_is_live_on_bar(ArtClass::Generic, two_hand_bar), "Generic live on Two-Handed bar");
	expect(!art_class_is_live_on_bar(ArtClass::OneHand, two_hand_bar), "1H dead on Two-Handed bar");
	expect(!art_class_is_live_on_bar(ArtClass::Dual, two_hand_bar), "Dual dead on Two-Handed bar");
}

void melee_and_main_are_union_of_children()
{
	constexpr std::uint32_t melee = static_cast<std::uint32_t>('MELE');
	constexpr std::uint32_t main = static_cast<std::uint32_t>('MAIN');
	expect(art_class_is_live_on_bar(ArtClass::OneHand, melee), "1H live on Melee parent");
	expect(art_class_is_live_on_bar(ArtClass::TwoHand, melee), "2H live on Melee parent");
	expect(art_class_is_live_on_bar(ArtClass::Dual, melee), "Dual live on Melee parent");
	expect(art_class_is_live_on_bar(ArtClass::Generic, melee), "Generic live on Melee parent");
	expect(art_class_is_live_on_bar(ArtClass::TwoHand, main), "2H live on Default (child union)");
	expect(art_class_is_live_on_bar(ArtClass::Generic, main), "Generic live on Default (fists)");
}

void ranged_bow_and_other_non_melee_bars()
{
	expect(!art_class_is_live_on_bar(ArtClass::Generic, static_cast<std::uint32_t>('MAGC')),
		"Generic dead on Magic bar");
	expect(!art_class_is_live_on_bar(ArtClass::OneHand, static_cast<std::uint32_t>('RNGD')),
		"1H dead on Ranged bar");
	expect(art_class_is_live_on_bar(ArtClass::Bow, static_cast<std::uint32_t>('RNGD')),
		"Bow live on Ranged bar");
	expect(art_class_is_direct_on_bar(ArtClass::Bow, static_cast<std::uint32_t>('RNGD') + 1),
		"Bow direct on sneak Ranged bar");
	expect(!art_class_is_live_on_bar(ArtClass::Bow, static_cast<std::uint32_t>('MAIN')),
		"Bow class is not a melee child on Default");
	expect(art_bar_tint_for_player(ArtClass::Bow, static_cast<std::uint32_t>('RNGD'), EquippedType::BOW) == ArtBarTint::direct,
		"Bow direct with a bow equipped on Ranged");
	expect(art_bar_tint_for_player(ArtClass::Bow, static_cast<std::uint32_t>('RNGD'), EquippedType::CROSSBOW) == ArtBarTint::dead,
		"Bow dead with a crossbow equipped on Ranged");
	expect(art_bar_tint_for_player(ArtClass::Bow, static_cast<std::uint32_t>('MAIN'), EquippedType::BOW) == ArtBarTint::direct,
		"Bow direct on Default with a bow equipped");
	expect(!art_class_is_live_on_bar(ArtClass::Generic, static_cast<std::uint32_t>('VMPL')),
		"Generic dead on Vampire Lord");
	expect(!art_class_is_live_on_bar(ArtClass::Generic, static_cast<std::uint32_t>('WWOL')),
		"Generic dead on Werewolf");
}

void direct_class_match_is_yellow_only_on_its_own_stance_bar()
{
	constexpr std::uint32_t two_hand = static_cast<std::uint32_t>('2HND');
	constexpr std::uint32_t dual_wield = static_cast<std::uint32_t>('1HDW');
	constexpr std::uint32_t one_hand_shield = static_cast<std::uint32_t>('1HSD');
	constexpr std::uint32_t one_hand_spell = static_cast<std::uint32_t>('1HSP');
	expect(art_class_is_direct_on_bar(ArtClass::TwoHand, two_hand), "2H is direct on Two-Handed");
	expect(art_class_is_direct_on_bar(ArtClass::Dual, dual_wield), "Dual is direct on Dual Wield");
	expect(art_class_is_direct_on_bar(ArtClass::OneHand, one_hand_shield), "1H is direct on 1H Shield");
	expect(art_class_is_direct_on_bar(ArtClass::OneHand, one_hand_spell), "1H is direct on 1H Spell");
	expect(!art_class_is_direct_on_bar(ArtClass::OneHand, dual_wield), "1H is dead on Dual Wield");
	expect(!art_class_is_direct_on_bar(ArtClass::Dual, two_hand), "Dual is dead, not direct, on Two-Handed");
	expect(art_class_is_direct_on_bar(ArtClass::TwoHand, two_hand + 1), "sneak Two-Handed matches parent");
}

void generic_is_never_a_direct_match()
{
	constexpr std::uint32_t bars[] = {
		static_cast<std::uint32_t>('2HND'), static_cast<std::uint32_t>('1HDW'),
		static_cast<std::uint32_t>('1HSD'), static_cast<std::uint32_t>('1HSP'),
		static_cast<std::uint32_t>('MELE'), static_cast<std::uint32_t>('MAIN'),
	};
	for (const auto bar : bars) {
		expect(!art_class_is_direct_on_bar(ArtClass::Generic, bar), "Generic runs everywhere, so it is never specific");
	}
}

void parent_magic_and_transform_bars_have_no_direct_match()
{
	constexpr std::uint32_t bars[] = {
		static_cast<std::uint32_t>('MELE'), static_cast<std::uint32_t>('MAIN'),
		static_cast<std::uint32_t>('MAGC'),
		static_cast<std::uint32_t>('VMPL'), static_cast<std::uint32_t>('WWOL'),
	};
	constexpr ArtClass classes[] = { ArtClass::OneHand, ArtClass::TwoHand, ArtClass::Dual, ArtClass::Bow, ArtClass::Generic };
	for (const auto bar : bars) {
		for (const auto art_class : classes) {
			expect(!art_class_is_direct_on_bar(art_class, bar), "parent, magic, and transform bars never match directly");
		}
	}
}

void every_direct_match_is_also_live()
{
	constexpr std::uint32_t bars[] = {
		static_cast<std::uint32_t>('2HND'), static_cast<std::uint32_t>('1HDW'),
		static_cast<std::uint32_t>('1HSD'), static_cast<std::uint32_t>('1HSP'),
		static_cast<std::uint32_t>('MELE'), static_cast<std::uint32_t>('MAIN'),
		static_cast<std::uint32_t>('MAGC'), static_cast<std::uint32_t>('RNGD'),
	};
	constexpr ArtClass classes[] = { ArtClass::OneHand, ArtClass::TwoHand, ArtClass::Dual, ArtClass::Bow, ArtClass::Generic };
	for (const auto bar : bars) {
		for (const auto art_class : classes) {
			if (art_class_is_direct_on_bar(art_class, bar)) {
				expect(art_class_is_live_on_bar(art_class, bar), "yellow can never outrank gray");
			}
		}
	}
}

// One rule, two panes: the Abilities list and the bound-slot strip both read the tier
// from `art_bar_tint`, so the tier itself is what gets pinned down here.
void the_tint_tier_agrees_with_the_two_predicates_it_composes()
{
	constexpr std::uint32_t bars[] = {
		static_cast<std::uint32_t>('2HND'), static_cast<std::uint32_t>('1HDW'),
		static_cast<std::uint32_t>('1HSD'), static_cast<std::uint32_t>('1HSP'),
		static_cast<std::uint32_t>('MELE'), static_cast<std::uint32_t>('MAIN'),
		static_cast<std::uint32_t>('MAGC'), static_cast<std::uint32_t>('RNGD'),
		static_cast<std::uint32_t>('2HND') + 1, static_cast<std::uint32_t>('1HDW') + 1,
	};
	constexpr ArtClass classes[] = { ArtClass::OneHand, ArtClass::TwoHand, ArtClass::Dual, ArtClass::Bow, ArtClass::Generic };
	for (const auto bar : bars) {
		for (const auto art_class : classes) {
			const auto tint = art_bar_tint(art_class, bar);
			if (!art_class_is_live_on_bar(art_class, bar)) {
				expect(tint == ArtBarTint::dead, "dead whenever the class cannot play on the bar");
			}
			else if (art_class_is_direct_on_bar(art_class, bar)) {
				expect(tint == ArtBarTint::direct, "direct whenever the bar owns the class");
			}
			else {
				expect(tint == ArtBarTint::generic, "generic whenever live but not owned");
			}
		}
	}
}

void default_bar_reads_the_equipped_weapon()
{
	constexpr std::uint32_t main_bar = static_cast<std::uint32_t>('MAIN');
	// Two-handed out: the 2H art owns the stance, 1H and Dual cannot play, Generic still can.
	expect(art_bar_tint_for_player(ArtClass::TwoHand, main_bar, EquippedType::TWOHAND) == ArtBarTint::direct,
		"2H is direct on Default while a greatsword is out");
	expect(art_bar_tint_for_player(ArtClass::OneHand, main_bar, EquippedType::TWOHAND) == ArtBarTint::dead,
		"1H is dead on Default while a greatsword is out");
	expect(art_bar_tint_for_player(ArtClass::Dual, main_bar, EquippedType::TWOHAND) == ArtBarTint::dead,
		"Dual is dead on Default while a greatsword is out");
	expect(art_bar_tint_for_player(ArtClass::Generic, main_bar, EquippedType::TWOHAND) == ArtBarTint::generic,
		"Generic stays generic on Default -- it says nothing about this stance");

	// Dual wield: Dual owns it and a lone one-hander does not.
	expect(art_bar_tint_for_player(ArtClass::Dual, main_bar, EquippedType::DUAL_WIELD) == ArtBarTint::direct,
		"Dual is direct on Default while dual wielding");
	expect(art_bar_tint_for_player(ArtClass::Dual, main_bar, EquippedType::ONEHAND_EMPTY) == ArtBarTint::dead,
		"Dual is dead on Default with one hand full and the other empty");
	expect(art_bar_tint_for_player(ArtClass::OneHand, main_bar, EquippedType::ONEHAND_SHIELD) == ArtBarTint::direct,
		"1H is direct on Default behind a shield");
}

void default_bar_grays_everything_the_runtime_would_refuse()
{
	constexpr std::uint32_t main_bar = static_cast<std::uint32_t>('MAIN');
	// Crossbows and spell stances still reject every class. A bow now accepts Bow only.
	constexpr EquippedType no_art_stances[] = {
		EquippedType::CROSSBOW, EquippedType::SPELL,
		EquippedType::STAFF_SHIELD, EquippedType::STAFF_LEFT
	};
	constexpr ArtClass classes[] = { ArtClass::OneHand, ArtClass::TwoHand, ArtClass::Dual, ArtClass::Bow, ArtClass::Generic };
	for (const auto equipped : no_art_stances) {
		for (const auto art_class : classes) {
			expect(art_bar_tint_for_player(art_class, main_bar, equipped) == ArtBarTint::dead,
				"Default grays every art when the equipment opens no melee stance");
		}
	}
	expect(art_bar_tint_for_player(ArtClass::Bow, main_bar, EquippedType::BOW) == ArtBarTint::direct,
		"Default makes Bow direct when a bow is equipped");
	expect(art_bar_tint_for_player(ArtClass::Generic, main_bar, EquippedType::BOW) == ArtBarTint::dead,
		"Default rejects Generic melee arts when a bow is equipped");
	// Barehanded only Generic survives.
	expect(art_bar_tint_for_player(ArtClass::Generic, main_bar, EquippedType::FIST) == ArtBarTint::generic,
		"Generic is live barehanded");
	expect(art_bar_tint_for_player(ArtClass::TwoHand, main_bar, EquippedType::FIST) == ArtBarTint::dead,
		"2H is dead barehanded");
}

void both_parent_bars_defer_to_equipment()
{
	// Default and Melee are the only two bars that stand for a set of stances rather than one, so
	// they are the only two with no answer of their own to give. Both are covered, in full.
	constexpr std::uint32_t parents[] = {
		static_cast<std::uint32_t>('MAIN'), static_cast<std::uint32_t>('MELE'),
		static_cast<std::uint32_t>('MAIN') + 1, static_cast<std::uint32_t>('MELE') + 1,
	};
	constexpr ArtClass classes[] = { ArtClass::OneHand, ArtClass::TwoHand, ArtClass::Dual, ArtClass::Bow, ArtClass::Generic };
	for (const auto bar : parents) {
		expect(art_bar_pins_no_stance(bar), "Default and Melee pin no stance, sneak twins included");
		for (const auto art_class : classes) {
			expect(art_bar_tint_for_player(art_class, bar, EquippedType::DUAL_WIELD) ==
					   art_equipped_tint(art_class, EquippedType::DUAL_WIELD),
				"a parent bar answers with the equipment");
		}
	}
	// Melee specifically: it used to union its children and tint nothing.
	expect(art_bar_tint_for_player(ArtClass::TwoHand, static_cast<std::uint32_t>('MELE'),
			   EquippedType::TWOHAND) == ArtBarTint::direct,
		"2H is direct on Melee with a greatsword out");
	expect(art_bar_tint_for_player(ArtClass::Dual, static_cast<std::uint32_t>('MELE'),
			   EquippedType::TWOHAND) == ArtBarTint::dead,
		"Dual is dead on Melee with a greatsword out");
}

void only_the_parent_bars_defer()
{
	constexpr std::uint32_t stance_bars[] = {
		static_cast<std::uint32_t>('1HSD'), static_cast<std::uint32_t>('1HSP'),
		static_cast<std::uint32_t>('1HDW'), static_cast<std::uint32_t>('2HND'),
		static_cast<std::uint32_t>('RNGD'), static_cast<std::uint32_t>('MAGC'),
		static_cast<std::uint32_t>('VMPL'), static_cast<std::uint32_t>('WWOL'),
	};
	for (const auto bar : stance_bars) {
		expect(!art_bar_pins_no_stance(bar), "a bar that names one stance keeps its own answer");
	}
}

void the_default_tint_never_disagrees_with_the_runtime_gate()
{
	// One rule, two readers. The bind menu must not color an art the cast would refuse, nor gray
	// one it would allow -- `art_class_is_live` is what hotbar.cpp and casting_controller.cpp ask.
	constexpr EquippedType equips[] = {
		EquippedType::FIST, EquippedType::ONEHAND_EMPTY, EquippedType::ONEHAND_SHIELD,
		EquippedType::ONEHAND_SPELL, EquippedType::DUAL_WIELD, EquippedType::TWOHAND,
		EquippedType::BOW, EquippedType::SPELL, EquippedType::CROSSBOW,
		EquippedType::STAFF_SHIELD, EquippedType::STAFF_LEFT
	};
	constexpr ArtClass classes[] = { ArtClass::OneHand, ArtClass::TwoHand, ArtClass::Dual, ArtClass::Bow, ArtClass::Generic };
	for (const auto equipped : equips) {
		for (const auto art_class : classes) {
			const bool dead = art_equipped_tint(art_class, equipped) == ArtBarTint::dead;
			expect(dead != art_class_is_live(art_class, equipped),
				"dead on Default is exactly what the runtime would refuse");
		}
	}
}

void every_other_bar_keeps_its_stance_answer()
{
	// A stance bar keeps its own answer when the current equipment is outside that stance.
	// Ranged has a bow/crossbow exception, tested above.
	constexpr std::uint32_t bars[] = {
		static_cast<std::uint32_t>('2HND'), static_cast<std::uint32_t>('1HDW'),
		static_cast<std::uint32_t>('1HSD'), static_cast<std::uint32_t>('1HSP'),
		static_cast<std::uint32_t>('MAGC'), static_cast<std::uint32_t>('RNGD'),
		static_cast<std::uint32_t>('VMPL'), static_cast<std::uint32_t>('WWOL'),
	};
	constexpr ArtClass classes[] = { ArtClass::OneHand, ArtClass::TwoHand, ArtClass::Dual, ArtClass::Bow, ArtClass::Generic };
	for (const auto bar : bars) {
		for (const auto art_class : classes) {
			expect(art_bar_tint_for_player(art_class, bar, EquippedType::TWOHAND) == art_bar_tint(art_class, bar),
				"a stance bar ignores what is equipped");
		}
	}
	// The sneak twin of Default resolves by equipment too -- it shares Default's stance root.
	expect(art_bar_tint_for_player(ArtClass::TwoHand, static_cast<std::uint32_t>('MAIN') + 1,
			   EquippedType::TWOHAND) == ArtBarTint::direct,
		"Sneak follows Default and resolves by equipment");
}

void a_dead_class_is_never_tinted_direct()
{
	// Gray outranks yellow. The strip paints the icon from `dead` and the text from the tier, so
	// a tier that could be both would gray the icon and yellow the name on the same row.
	constexpr std::uint32_t bars[] = {
		static_cast<std::uint32_t>('2HND'), static_cast<std::uint32_t>('1HDW'),
		static_cast<std::uint32_t>('1HSD'), static_cast<std::uint32_t>('1HSP'),
		static_cast<std::uint32_t>('MELE'), static_cast<std::uint32_t>('MAIN'),
		static_cast<std::uint32_t>('MAGC'), static_cast<std::uint32_t>('RNGD'),
	};
	constexpr ArtClass classes[] = { ArtClass::OneHand, ArtClass::TwoHand, ArtClass::Dual, ArtClass::Bow, ArtClass::Generic };
	for (const auto bar : bars) {
		for (const auto art_class : classes) {
			if (art_bar_tint(art_class, bar) == ArtBarTint::direct) {
				expect(art_class_is_live_on_bar(art_class, bar), "a direct tier is always live");
			}
		}
	}
}

void two_handed_and_dual_wield_cases()
{
	constexpr std::uint32_t two_handed = static_cast<std::uint32_t>('2HND');
	constexpr std::uint32_t dual_wield = static_cast<std::uint32_t>('1HDW');
	// On a Two-Handed weapon a 1H art (Blood Seeker) is dead, a 2H art (Blood Flurry) is the
	// direct match, and a Generic ash stays plain.
	expect(art_bar_tint(ArtClass::OneHand, two_handed) == ArtBarTint::dead, "1H dead on Two-Handed");
	expect(art_bar_tint(ArtClass::TwoHand, two_handed) == ArtBarTint::direct, "2H direct on Two-Handed");
	expect(art_bar_tint(ArtClass::Generic, two_handed) == ArtBarTint::generic, "Generic plain on Two-Handed");
	// Dual Wield is now an exact equipment stance: a lone one-hander belongs to Melee.
	expect(art_bar_tint(ArtClass::Dual, dual_wield) == ArtBarTint::direct, "Dual direct on Dual Wield");
	expect(art_bar_tint(ArtClass::OneHand, dual_wield) == ArtBarTint::dead, "1H dead on Dual Wield");
	expect(art_bar_tint(ArtClass::Generic, dual_wield) == ArtBarTint::generic, "Generic live on Dual Wield");
	expect(art_bar_tint(ArtClass::TwoHand, dual_wield) == ArtBarTint::dead, "2H dead on Dual Wield");
}

void custom_folder_number_is_not_a_slot_index()
{
	const auto n = SpellHotbar::parse_custom_art_folder_number("Custom_Ability_3");
	expect(n.has_value() && *n == 3, "Custom_Ability_3 is folder 3");
	expect(!SpellHotbar::parse_custom_art_folder_number("Ashes of War Sword Neutral").has_value(),
		"stance-default folders are not custom templates");
	const auto art = SpellHotbar::custom_art_from_folder(3, "Custom_Ability_3", "", "", false);
	expect(art.id == 1003, "ArtID is 1000 + folder number");
	expect(art.selector == 1003, "selector is not hotbar slot 3");
	expect(art.display_name == "Custom Ability 3", "empty name file uses Custom Ability N");
	expect(SpellHotbar::is_custom_ability(art.id), "Custom Ability ids sit above the ash band");
	expect(!SpellHotbar::is_custom_ability(1), "Test Ability is not a Custom Ability");
	expect(art.icon == "GREATER_POWER", "empty icon file uses GREATER_POWER");
	expect(art.art_class == ArtClass::Generic, "custom folders are Generic");
	expect(!art.has_clip, "empty template has no clip");
	expect(art.stamina_cost == 25.0f, "unedited Custom Ability stamina is 25");
	expect(art.magicka_cost == 0.0f, "unedited Custom Ability magicka is 0");
	expect(art.health_cost == 0.0f, "unedited Custom Ability health is 0");
	expect(art.gcd == 1.0f, "unedited Custom Ability GCD is 1s");
	expect(art.damage_mult == 1.0f, "unedited Custom Ability damage multiplier is 1x");
	expect(art.spell_local_form == SpellHotbar::vanilla_firebolt_local_form,
		"unedited Custom Ability spell is vanilla Firebolt");
	expect(art.spell_plugin == SpellHotbar::vanilla_firebolt_plugin, "Firebolt lives in Skyrim.esm");
}

void custom_folder_files_override_name_and_icon()
{
	const auto art = SpellHotbar::custom_art_from_folder(
		13, "Custom_Ability_13", "Rapier Lunge\n", "FLAMES\n", true);
	expect(art.id == 1013, "extra numbered folders still get an id");
	expect(art.display_name == "Rapier Lunge", "name.txt first line is the display name");
	expect(art.icon == "FLAMES", "icon.txt first line is the icon key");
	expect(art.has_clip, "a dropped AABL clip marks the folder as playable");
}

void icon_form_wins_over_string_keys()
{
	expect(resolve_art_icon_draw_kind(0x123, "GREATER_POWER", true, true) == ArtIconDrawKind::Form,
		"icon_form selects Form before atlas keys");
}

void extra_atlas_beats_default_icon_name()
{
	expect(resolve_art_icon_draw_kind(0, "icons_skills_FLAMES", true, true) == ArtIconDrawKind::ExtraAtlas,
		"extra atlas wins when both maps contain the key");
}

void default_icon_name_when_not_in_extra_atlas()
{
	expect(resolve_art_icon_draw_kind(0, "GREATER_POWER", false, true) == ArtIconDrawKind::DefaultIcon,
		"default icon names resolve without extra atlas");
}

void player_overlay_replaces_catalogue_tuning_and_round_trips()
{
	ArtDefinition catalogue;
	catalogue.id = 2;
	catalogue.display_name = "Aimed Blow";
	catalogue.icon = "GREATER_POWER";
	catalogue.art_class = ArtClass::Generic;
	catalogue.stamina_cost = 25.0f;
	catalogue.cooldown_text = "8s";
	catalogue.gcd = 1.0f;
	ArtDefinition live = catalogue;
	auto overlay = SpellHotbar::art_player_overlay_from(live);
	overlay.display_name = "Cheap Aimed Blow";
	overlay.stamina_cost = 5.0f;
	overlay.magicka_cost = 10.0f;
	overlay.art_class = ArtClass::OneHand;
	overlay.damage_mult = 3.0f;
	SpellHotbar::apply_art_player_overlay(live, overlay);
	expect(live.display_name == "Cheap Aimed Blow", "overlay name wins");
	expect(live.stamina_cost == 5.0f, "overlay stamina wins");
	expect(live.magicka_cost == 10.0f, "overlay magicka wins");
	expect(live.art_class == ArtClass::OneHand, "overlay class wins");
	expect(live.damage_mult == 3.0f, "overlay damage multiplier wins");
	expect(!SpellHotbar::art_matches_catalogue_tuning(live, catalogue), "edited ash is not catalogue");
	expect(SpellHotbar::art_matches_catalogue_tuning(catalogue, catalogue), "catalogue matches itself");
	expect(catalogue.damage_mult == 1.0f, "catalogue default damage multiplier is 1x");
	ArtDefinition reset = live;
	SpellHotbar::apply_art_player_overlay(reset, SpellHotbar::art_player_overlay_from(catalogue));
	expect(reset.damage_mult == 1.0f, "Reset restores 1x");
}

void unknown_icon_degrades_to_unknown_kind()
{
	expect(resolve_art_icon_draw_kind(0, "NOT_A_REAL_ICON", false, false) == ArtIconDrawKind::Unknown,
		"missing keys resolve to Unknown");
}

void sidecar_is_the_folder_source_of_truth()
{
	const char* text =
		"name=Rapier Lunge\n"
		"icon=FLAMES\n"
		"icon_form=0\n"
		"spell_form=00012FD0\n"
		"spell_plugin=Skyrim.esm\n"
		"self_target=0\n"
		"art_class=1H\n"
		"stamina=10\n"
		"magicka=40\n"
		"health=5\n"
		"cooldown=8s\n"
		"gcd=1.5\n"
		"damage_mult=3\n";
	const auto art = SpellHotbar::custom_art_from_folder(
		3, "Custom_Ability_3", "ignored.txt\n", "GREATER_POWER\n", true, text);
	expect(art.display_name == "Rapier Lunge", "sidecar name wins over name.txt");
	expect(art.icon == "FLAMES", "sidecar icon wins over icon.txt");
	expect(art.art_class == ArtClass::OneHand, "sidecar Art Class is 1H");
	expect(art.stamina_cost == 10.0f, "sidecar stamina is 10");
	expect(art.magicka_cost == 40.0f, "sidecar magicka is 40");
	expect(art.health_cost == 5.0f, "sidecar health is 5");
	expect(art.gcd == 1.5f, "sidecar GCD is 1.5");
	expect(art.damage_mult == 3.0f, "sidecar damage multiplier is 3x");
	expect(art.spell_local_form == 0x12FD0, "sidecar keeps Firebolt local form");
}

void missing_sidecar_damage_mult_defaults_to_identity()
{
	const auto sidecar = SpellHotbar::parse_custom_ability_sidecar("name=Old Sidecar\ngcd=1\n", 3);
	expect(sidecar.damage_mult == 1.0f, "an older sidecar without damage_mult is 1x");
	const auto text = SpellHotbar::serialize_custom_ability_sidecar(sidecar);
	expect(text.find("damage_mult=1") != std::string::npos, "serialize writes damage_mult");
}

void extra_folder_uses_the_same_sidecar_and_pi_name()
{
	const auto sidecar = SpellHotbar::parse_custom_ability_sidecar("name=Extra\nart_class=2H\n", 13);
	expect(sidecar.name == "Extra", "Custom_Ability_13 reads the same sidecar");
	expect(sidecar.art_class == ArtClass::TwoHand, "extras can set Art Class");
	expect(SpellHotbar::custom_ability_pi_name(13) == "custom_ability_13",
		"$custom_ability_N uses the folder number");
	expect(SpellHotbar::custom_ability_pie_annotation(13) == "PIE.$custom_ability_13",
		"the clip marker is PIE.$custom_ability_N");
}

void pi_instruction_has_zero_resource_lines()
{
	auto sidecar = SpellHotbar::default_custom_ability_sidecar(3);
	sidecar.stamina_cost = 25.0f;
	sidecar.magicka_cost = 40.0f;
	sidecar.health_cost = 5.0f;
	sidecar.self_target = true;
	const auto inst = SpellHotbar::custom_ability_castspell_instruction(sidecar);
	expect(inst == "@CASTSPELL|0x12FD0|Skyrim.esm|1|1|1|0|0|0|0|0|0",
		"PIE costs stay zero; selfTarget follows delivery");
	const auto line = SpellHotbar::custom_ability_pi_line(3, sidecar);
	expect(line == "$custom_ability_3 = @CASTSPELL|0x12FD0|Skyrim.esm|1|1|1|0|0|0|0|0|0",
		"PI config aliases the instruction");
}

void default_assignment_is_vanilla_firebolt()
{
	const auto sidecar = SpellHotbar::default_custom_ability_sidecar(1);
	expect(sidecar.spell_local_form == 0x12FD0, "placeholder spell is Firebolt");
	expect(sidecar.spell_plugin == "Skyrim.esm", "Firebolt plugin is Skyrim.esm");
	expect(sidecar.stamina_cost == 25.0f && sidecar.magicka_cost == 0.0f && sidecar.health_cost == 0.0f,
		"Ability Costs default 25/0/0");
}

void only_fire_and_forget_spells_are_assignable()
{
	using SpellHotbar::AbilitySpellCasting;
	using SpellHotbar::AbilitySpellFormKind;
	using SpellHotbar::is_assignable_custom_ability_spell;
	expect(is_assignable_custom_ability_spell(AbilitySpellFormKind::Spell, AbilitySpellCasting::FireAndForget),
		"ritual F&F, powers, and voice spells are Spell + fire-and-forget");
	expect(!is_assignable_custom_ability_spell(AbilitySpellFormKind::Spell, AbilitySpellCasting::Concentration),
		"concentration is excluded");
	expect(!is_assignable_custom_ability_spell(AbilitySpellFormKind::Scroll, AbilitySpellCasting::FireAndForget),
		"scrolls are excluded");
	expect(!is_assignable_custom_ability_spell(AbilitySpellFormKind::Alchemy, AbilitySpellCasting::FireAndForget),
		"potions are excluded");
	expect(!is_assignable_custom_ability_spell(AbilitySpellFormKind::Shout, AbilitySpellCasting::FireAndForget),
		"TESShout is excluded");
}

void magicka_and_health_refuse_the_same_way_as_stamina()
{
	using SpellHotbar::ArtMeter;
	using SpellHotbar::unaffordable_art_meter;
	expect(unaffordable_art_meter(25, 0, 0, 10, 100, 100) == ArtMeter::Stamina,
		"short stamina refuses");
	expect(unaffordable_art_meter(0, 40, 0, 100, 10, 100) == ArtMeter::Magicka,
		"short magicka refuses");
	expect(unaffordable_art_meter(0, 0, 5, 100, 100, 1) == ArtMeter::Health, "short health refuses");
	expect(unaffordable_art_meter(25, 0, 0, 25, 0, 0) == ArtMeter::None, "exact stamina is affordable");
	expect(unaffordable_art_meter(0, 0, 0, 0, 0, 0) == ArtMeter::None, "zero costs never refuse");
}

void pie_stamps_at_hit_frame_when_present()
{
	std::vector<SpellHotbar::ClipAnnotation> anns{
		{ 0.0f, "animmotion 0 0 0" },
		{ 0.42f, "HitFrame" },
		{ 0.9f, "MCO_WinOpen" },
	};
	expect(SpellHotbar::custom_ability_pie_stamp_time(anns, 1.0f) == 0.42f,
		"HitFrame time is the v1 fire time");
	expect(!SpellHotbar::clip_has_custom_ability_pie(anns, 3), "a new clip is missing the marker");
}

void pie_stamps_at_five_percent_without_hit_frame()
{
	std::vector<SpellHotbar::ClipAnnotation> anns{ { 0.0f, "animmotion 0 0 0" } };
	expect(SpellHotbar::custom_ability_pie_stamp_time(anns, 2.0f) == 0.10f,
		"missing HitFrame uses 5% of duration, not mid-clip");
}

void pie_stamp_replaces_author_casts_and_is_idempotent()
{
	const char* txt =
		"# numOriginalFrames: 10\n"
		"# duration: 2.000000\n"
		"# numAnnotationTracks: 1\n"
		"# numAnnotations: 4\n"
		"0.000000 animmotion 0 0 0\n"
		"0.346300 PIE.@CAST|0x812|SpellscribeStances.esp|1|1|0|0|0|0|0|0|0\n"
		"0.366630 HitFrame\n"
		"0.866580 PIE.@CAST|0x812|SpellscribeStances.esp|1|1|0|0|0|0|0|0|0\n";
	const auto once = SpellHotbar::ensure_custom_ability_pie_in_annotation_txt(txt, 3);
	expect(once.find("PIE.@CAST|") == std::string::npos, "author CAST payloads are stripped");
	expect(once.find("HitFrame") != std::string::npos, "HitFrame stays");
	const auto first = once.find("PIE.$custom_ability_3");
	const auto second = once.find("PIE.$custom_ability_3", first + 1);
	expect(first != std::string::npos && second == std::string::npos, "exactly one Custom Ability marker");
	expect(once.find("0.366630 PIE.$custom_ability_3") != std::string::npos,
		"the marker is stamped at the first HitFrame");

	const char* with_whoosh =
		"# duration: 2.000000\n"
		"# numAnnotations: 5\n"
		"0.000000 animmotion 0 0 0\n"
		"0.346300 PIE.@CAST|0x812|SpellscribeStances.esp|1|1|0|0|0|0|0|0|0\n"
		"0.346300 SoundPlay.MAGFirebolt03FireSD\n"
		"0.366630 HitFrame\n"
		"0.800000 SoundPlay.WPNSwingBladeMedium\n";
	const auto stripped = SpellHotbar::ensure_custom_ability_pie_in_annotation_txt(with_whoosh, 3, "MAGFrostbolt01FireSD");
	expect(stripped.find("SoundPlay.MAGFirebolt03FireSD") == std::string::npos,
		"the author CAST's whoosh is stripped");
	expect(stripped.find("SoundPlay.WPNSwingBladeMedium") != std::string::npos,
		"weapon swing sounds stay");
	expect(stripped.find("0.366630 SoundPlay.MAGFrostbolt01FireSD") != std::string::npos,
		"the assigned spell's Release sound is stamped at fire time");
	expect(stripped.find("PIE.$custom_ability_3") != std::string::npos, "the Custom Ability marker is still added");

	const auto again = SpellHotbar::ensure_custom_ability_pie_in_annotation_txt(stripped, 3, "MAGFrostbolt01FireSD");
	const auto first_sound = again.find("SoundPlay.MAGFrostbolt01FireSD");
	const auto second_sound = again.find("SoundPlay.MAGFrostbolt01FireSD", first_sound + 1);
	expect(first_sound != std::string::npos && second_sound == std::string::npos,
		"a second save does not add another Release whoosh");

	const auto with_hitframe_payload = SpellHotbar::ensure_custom_ability_pie_in_annotation_txt(
		"# duration: 1.000000\n"
		"# numAnnotations: 1\n"
		"0.500000 HitFrame.@CASTSPELL|0x1|Author.esp|1|1|0|0|0|0|0|0|0\n",
		3);
	expect(with_hitframe_payload.find("HitFrame.@CASTSPELL") == std::string::npos,
		"HitFrame CASTSPELL is not left as a second fire");
	expect(with_hitframe_payload.find("0.500000 HitFrame\n") != std::string::npos,
		"the HitFrame event itself is kept");
	expect(with_hitframe_payload.find("PIE.$custom_ability_3") != std::string::npos, "the Custom Ability marker is added");

	const auto twice = SpellHotbar::ensure_custom_ability_pie_in_annotation_txt(once, 3);
	const auto first_again = twice.find("PIE.$custom_ability_3");
	const auto second_again = twice.find("PIE.$custom_ability_3", first_again + 1);
	expect(first_again != std::string::npos && second_again == std::string::npos,
		"a second save does not add another marker");
}

void author_cast_detection_ignores_cast_ok_and_custom_marker()
{
	using SpellHotbar::is_author_spell_cast_annotation;
	expect(!is_author_spell_cast_annotation("CastOKStart"), "CastOKStart is not a spell payload");
	expect(!is_author_spell_cast_annotation("PIE.$custom_ability_3"), "the SH2 marker is not an author cast");
	expect(is_author_spell_cast_annotation("PIE.@CAST|0x812|SpellscribeStances.esp|1|1|0|0|0|0|0|0|0"),
		"Spellscribe CAST is an author fire");
	expect(is_author_spell_cast_annotation("PIE.@CASTSPELL|0x12FD0|Skyrim.esm|1|1|0|0|0|0|0|0|0"),
		"expanded CASTSPELL from another mod is still an author fire");
}

}  // namespace

int main()
{
	a_row_loads_every_art_field();
	art_class_column_loads_each_legal_value();
	empty_art_class_defaults_generic();
	eight_seconds_is_spell_data_days();
	non_positive_cooldown_means_none();
	missing_header_loads_nothing();
	header_without_art_class_loads_nothing();
	zero_id_and_empty_name_are_skipped();
	two_hand_art_is_live_only_on_two_hand();
	one_hand_art_is_live_on_one_hand_variants();
	dual_art_is_live_only_on_dual_wield();
	generic_art_is_live_on_melee_and_fists_only();
	one_hand_empty_routes_to_melee_and_dual_wield_stays_distinct();
	two_hand_bar_is_two_hand_and_generic_only();
	melee_and_main_are_union_of_children();
	bow_art_requires_a_bow();
	only_bow_arts_need_ammo();
	ranged_bow_and_other_non_melee_bars();
	direct_class_match_is_yellow_only_on_its_own_stance_bar();
	generic_is_never_a_direct_match();
	parent_magic_and_transform_bars_have_no_direct_match();
	every_direct_match_is_also_live();
	the_tint_tier_agrees_with_the_two_predicates_it_composes();
	a_dead_class_is_never_tinted_direct();
	default_bar_reads_the_equipped_weapon();
	default_bar_grays_everything_the_runtime_would_refuse();
	the_default_tint_never_disagrees_with_the_runtime_gate();
	both_parent_bars_defer_to_equipment();
	only_the_parent_bars_defer();
	every_other_bar_keeps_its_stance_answer();
	two_handed_and_dual_wield_cases();
	custom_folder_number_is_not_a_slot_index();
	custom_folder_files_override_name_and_icon();
	sidecar_is_the_folder_source_of_truth();
	missing_sidecar_damage_mult_defaults_to_identity();
	extra_folder_uses_the_same_sidecar_and_pi_name();
	pi_instruction_has_zero_resource_lines();
	default_assignment_is_vanilla_firebolt();
	only_fire_and_forget_spells_are_assignable();
	magicka_and_health_refuse_the_same_way_as_stamina();
	pie_stamps_at_hit_frame_when_present();
	pie_stamps_at_five_percent_without_hit_frame();
	pie_stamp_replaces_author_casts_and_is_idempotent();
	author_cast_detection_ignores_cast_ok_and_custom_marker();
	icon_form_wins_over_string_keys();
	extra_atlas_beats_default_icon_name();
	default_icon_name_when_not_in_extra_atlas();
	unknown_icon_degrades_to_unknown_kind();
	player_overlay_replaces_catalogue_tuning_and_round_trips();

	if (g_failures != 0) {
		std::cerr << g_failures << " failure(s)\n";
		return EXIT_FAILURE;
	}
	std::cout << "ok\n";
	return EXIT_SUCCESS;
}
