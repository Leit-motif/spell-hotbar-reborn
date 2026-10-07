#include "art_pack_gen.h"

#include <cstdlib>
#include <iostream>
#include <string>

using SpellHotbar::ArtClass;
using SpellHotbar::ArtPackGen::assign_ids;
using SpellHotbar::ArtPackGen::build_config_json;
using SpellHotbar::ArtPackGen::build_pack_config_json;
using SpellHotbar::ArtPackGen::classify_art_class;
using SpellHotbar::ArtPackGen::curated_row_wins;
using SpellHotbar::ArtPackGen::kFirstArtId;
using SpellHotbar::ArtPackGen::kPackName;
using SpellHotbar::ArtPackGen::kPriorityBase;
using SpellHotbar::ArtPackGen::override_animations_folder;
using SpellHotbar::ArtPackGen::ScannedArt;

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& what)
{
	if (!condition) {
		++g_failures;
		std::cerr << "FAIL: " << what << '\n';
	}
}

bool contains(const std::string& haystack, const std::string& needle)
{
	return haystack.find(needle) != std::string::npos;
}

ScannedArt art(const std::string& name, const std::string& mod = "AoW Pack")
{
	ScannedArt a;
	a.name = name;
	a.replacer_mod = mod;
	return a;
}

void ids_start_at_two_and_follow_sorted_name_order()
{
	std::vector<ScannedArt> arts{ art("Zephyr Cut"), art("aimed blow"), art("Blood Seeker") };
	assign_ids(arts);
	expect(arts[0].name == "aimed blow" && arts[0].id == kFirstArtId,
		"sorting is case-insensitive, so a lowercase name still sorts first and takes id 2");
	expect(arts[1].name == "Blood Seeker" && arts[1].id == 3, "the second name takes the next id");
	expect(arts[2].name == "Zephyr Cut" && arts[2].id == 4, "and so on, with no gaps");
}

void ids_do_not_depend_on_discovery_order()
{
	// Directory iteration order is not guaranteed. If it leaked into the ids, every art would
	// silently repoint at a different clip on some other machine.
	std::vector<ScannedArt> one{ art("Alpha"), art("Beta"), art("Gamma") };
	std::vector<ScannedArt> other{ art("Gamma"), art("Alpha"), art("Beta") };
	assign_ids(one);
	assign_ids(other);
	for (size_t i = 0; i < one.size(); ++i) {
		expect(one[i].name == other[i].name && one[i].id == other[i].id,
			"the same set of folders yields the same ids whatever order they were found in");
	}
}

void a_name_collision_across_mods_is_still_deterministic()
{
	std::vector<ScannedArt> arts{ art("Aimed Blow", "Zeta Pack"), art("Aimed Blow", "Alpha Pack") };
	assign_ids(arts);
	expect(arts[0].replacer_mod == "Alpha Pack" && arts[0].id == kFirstArtId,
		"two mods shipping the same art name break the tie on the mod name, not on luck");
	expect(arts[1].replacer_mod == "Zeta Pack" && arts[1].id == 3, "the other still gets its own id");
}

void one_handed_types_in_both_hands_are_dual()
{
	expect(classify_art_class({ 1.0 }, { 2.0 }) == ArtClass::Dual,
		"a one-hander in each hand is the dual-wield case");
}

void a_one_handed_only_art_is_one_hand()
{
	expect(classify_art_class({ 1.0, 3.0 }, {}) == ArtClass::OneHand,
		"every melee type inside the 1H group claims 1H");
}

void a_two_handed_only_art_is_two_hand()
{
	expect(classify_art_class({ 5.0, 6.0 }, {}) == ArtClass::TwoHand,
		"every melee type inside the 2H group claims 2H");
}

void a_mixed_art_is_generic()
{
	expect(classify_art_class({ 1.0, 5.0 }, {}) == ArtClass::Generic,
		"an art the author gated for both 1H and 2H cannot claim either, so it stays Generic");
}

void an_art_with_no_melee_gate_is_generic()
{
	expect(classify_art_class({}, {}) == ArtClass::Generic,
		"no IsEquippedType at all means the author gated on something else; Generic gates on nothing");
	expect(classify_art_class({ 0.0, 7.0 }, {}) == ArtClass::Generic,
		"unarmed and bow are not melee groups this classifier claims");
}

void the_config_gates_on_the_graph_variable_never_on_a_form()
{
	auto a = art("Aimed Blow", "Nolvus Ashes of War Stance Framework");
	a.id = 2;
	const auto json = build_config_json(a);
	expect(contains(json, "\"graphVariable\": \"SH2_ArtSelector\""),
		"the selector is read from the behavior graph");
	expect(contains(json, "\"graphVariableType\": \"Int\""), "declared INT32 by the shtb patch");
	expect(!contains(json, "SpellHotbar.esp") && !contains(json, "D63"),
		"never an ESP form: that record is not in stock Spell Hotbar 2 and shipping it broke every clean install");
	expect(contains(json, "\"Value B\": {\n                \"value\": 2.0\n            }"),
		"the compared value is this art's own id");
}

void the_config_points_at_the_authors_folder_rather_than_copying_it()
{
	auto a = art("Blood Seeker", "Nolvus Ashes of War Stance Framework");
	a.id = 5;
	const auto json = build_config_json(a);
	expect(override_animations_folder(a) == "../Nolvus Ashes of War Stance Framework/Blood Seeker",
		"the override path is relative to our pack folder, one level up into the author's mod");
	expect(contains(json, "\"overrideAnimationsFolder\": \"../Nolvus Ashes of War Stance Framework/Blood Seeker\""),
		"so no .hkx is ever copied and nobody's animations are redistributed");
}

void priority_clears_the_stance_packs()
{
	auto a = art("Aimed Blow");
	a.id = 2;
	const auto json = build_config_json(a);
	expect(contains(json, "\"priority\": 2000000002"), "priority is the reserved base plus the id");
	expect(kPriorityBase + 2 > 1001002544,
		"and that base clears Ashes of War Sword Neutral without depending on their number");
}

void the_player_gate_is_present()
{
	auto a = art("Aimed Blow");
	a.id = 2;
	const auto json = build_config_json(a);
	expect(contains(json, "\"condition\": \"IsActorBase\"") && contains(json, "\"formID\": \"7\""),
		"arts are the player's, so the pack does not replace every NPC's attack animation");
}

void the_pack_declares_itself_to_oar()
{
	// Without the pack's own config.json, OAR never registers the folder, however correct the
	// submod configs inside it are. OAR says nothing about it -- no warning in its log -- so the
	// only symptom is arts that quietly do nothing.
	const auto json = build_pack_config_json();
	expect(contains(json, "\"name\":"), "a replacer mod folder is only registered if it names itself");
	expect(contains(json, "\"author\":"), "and OAR shows the author in its in-game list");
	expect(std::string{ kPackName } != "SpellHotbar2Arts",
		"the generated pack must not share a directory with the core mod's Custom_Ability_* submods: "
		"OAR enumerates one replacer-mod folder, so a split across two mods loses half of it");
}

void a_curated_catalogue_row_is_never_overwritten()
{
	// The regression this pins: every Ashes of War art lost its hand-drawn icon and its
	// hand-checked class because the scan registered over rows arts_ashes.csv already owned.
	const auto a = art("Blood Seeker");
	expect(curated_row_wins(a, "Blood Seeker"),
		"the catalogue already describes this art, so its icon and class stay");
	expect(curated_row_wins(a, "blood seeker"),
		"and the match is case-insensitive, like every other name comparison here");
	expect(!curated_row_wins(a, "Aimed Blow"),
		"a row about a DIFFERENT art is not curation for this one -- ids drift between machines "
		"whose installed Ashes of War content differs, and there the scan's own row is correct");
}

}  // namespace

int main()
{
	ids_start_at_two_and_follow_sorted_name_order();
	ids_do_not_depend_on_discovery_order();
	a_name_collision_across_mods_is_still_deterministic();
	one_handed_types_in_both_hands_are_dual();
	a_one_handed_only_art_is_one_hand();
	a_two_handed_only_art_is_two_hand();
	a_mixed_art_is_generic();
	an_art_with_no_melee_gate_is_generic();
	the_config_gates_on_the_graph_variable_never_on_a_form();
	the_config_points_at_the_authors_folder_rather_than_copying_it();
	priority_clears_the_stance_packs();
	the_player_gate_is_present();
	the_pack_declares_itself_to_oar();
	a_curated_catalogue_row_is_never_overwritten();

	if (g_failures != 0) {
		std::cerr << g_failures << " failure(s)\n";
		return EXIT_FAILURE;
	}
	std::cout << "ok\n";
	return EXIT_SUCCESS;
}
