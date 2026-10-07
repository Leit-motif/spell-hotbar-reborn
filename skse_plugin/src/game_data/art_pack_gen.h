#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "art_definition.h"

// Build the pointer-art OAR pack in process, at load, from whatever Ashes of War content this
// machine actually has.
//
// WHY THIS IS NOT A SCRIPT. The pack is a set of `config.json` files whose
// `overrideAnimationsFolder` points at another mod's clip folder. Those pointers can only be
// written on the machine that owns the clips: the OAR replacer-mod folder name is not fixed
// across installs (Nolvus repackages Ashes of War under `Nolvus Ashes of War Stance Framework`;
// a straight Nexus install does not), so a pre-generated pack aims at folders that may not exist.
// A Python generator solved that and made "run this script" an install step, which is not a thing
// to ask a mod user to do. The scan is pure filesystem work -- no forms, no ESP, no game data --
// so the DLL can do it itself.
//
// WHY IT RUNS AT `kPostLoad` AND NOT `kDataLoaded`. OAR parses `config.json` once, when it builds
// its replacer mods, and that is far later than plugin load but EARLIER than our own data load.
// Measured on one large modlist: plugin load 21:00:43, OAR "Parsing data\meshes for
// replacer mods" 21:07:39, our `kDataLoaded` art catalogue 21:08:35. Generating at `kDataLoaded`
// therefore misses OAR's parse by about a minute and the arts only appear on the NEXT launch --
// the restart this module exists to remove. `kPostLoad` precedes OAR's parse structurally, not by
// luck: SKSE fires it when plugins finish loading, while OAR's directory cache is kicked off by
// game data load. The seven-minute gap here is this modlist's size; the ORDER is what matters.
//
// The scan result is cached because the ART IDS MUST AGREE between the configs written at
// `kPostLoad` and the catalogue registered at `kDataLoaded`. Ids come from sorted scan order, so
// two independent scans of a tree that changed in between would disagree and every art would
// point at the wrong clip. Scan once, reuse.
namespace SpellHotbar::ArtPackGen {

	// One Ashes-of-War-style submod found on disk: a folder holding an `AABL_Attack_A.hkx`.
	struct ScannedArt {
		std::string name;          // submod folder name, and the display name
		std::string replacer_mod;  // the OAR mod folder it lives in, for the override path
		int id{ 0 };               // assigned from sorted order; also the selector value
		ArtClass art_class{ ArtClass::Generic };
	};

	// Ids start at 2. Selector 0 means "no art", and 1 is left clear, matching the Python
	// generator this replaces (`FIRST_ART_ID`).
	inline constexpr int kFirstArtId = 2;
	// A reserved band above the stance packs' own priorities (Ashes of War Sword Neutral is
	// 1001002544), so an SH2 art wins without depending on their numbers.
	inline constexpr int kPriorityBase = 2000000000;
	// A folder of our own, deliberately NOT `SpellHotbar2Arts`.
	//
	// `SpellHotbar2Arts` is already provided by the core mod, which keeps the `Custom_Ability_*`
	// submods there. Generating into it puts one OAR replacer-mod directory in two places at once
	// -- the mod folder and the mod manager's write sink -- and OAR then enumerates only one of
	// them. Measured: with the arts written to the sink and Custom_Ability_* in the mod
	// folder, every generated art was invisible (`SH2_Art_Clip activated with no animmotion keys`)
	// while the SAME bytes shipped inside the mod folder played correctly (200 animmotion keys).
	// Our own scan never saw the problem because it opens each config BY PATH, which resolves
	// across the merged view; enumeration is what splits.
	inline constexpr const char* kPackName = "SpellHotbar2ArtsGenerated";

	// OAR's `IsEquippedType` values, which are NOT the vanilla equipped-type enum.
	inline const std::set<double> kOar1hTypes{ 1.0, 2.0, 3.0, 4.0 };
	inline const std::set<double> kOar2hTypes{ 5.0, 6.0 };

	// Everything from here to `generate_and_cache` is deliberately header-inline and free of
	// CommonLibSSE, so the JSON this ships and the id/class rules can be asserted by a standalone
	// test exe rather than only in a running game.

	namespace detail {
		[[nodiscard]] inline std::string lowered(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}
	}

	// Sort by lowercased name, then assign ids from kFirstArtId. Deterministic and independent of
	// directory-iteration order, which is not guaranteed stable across machines or runs -- and the
	// ids ARE the selector values baked into the configs, so an unstable order would silently
	// repoint every art at a different clip on the next launch.
	inline void assign_ids(std::vector<ScannedArt>& arts)
	{
		std::sort(arts.begin(), arts.end(), [](const ScannedArt& a, const ScannedArt& b) {
			const auto an = detail::lowered(a.name);
			const auto bn = detail::lowered(b.name);
			if (an != bn) {
				return an < bn;
			}
			return detail::lowered(a.replacer_mod) < detail::lowered(b.replacer_mod);
		});
		int next = kFirstArtId;
		for (auto& art : arts) {
			art.id = next++;
		}
	}

	// Collapse the author's `IsEquippedType` conditions into one class. Mirrors the Python
	// generator: 1H in BOTH hands is Dual; otherwise the melee types must all fall inside one
	// group to claim it, and anything mixed or non-melee is Generic. This matters at runtime --
	// `art_class_is_live` gates both the hotbar and the cast, so a misclassified 2H art would
	// fire with a one-hander equipped.
	[[nodiscard]] inline ArtClass classify_art_class(
		const std::set<double>& right_types, const std::set<double>& left_types) noexcept
	{
		const auto any_in = [](const std::set<double>& values, const std::set<double>& group) {
			return std::any_of(values.begin(), values.end(),
				[&group](double v) { return group.count(v) > 0; });
		};
		if (any_in(right_types, kOar1hTypes) && any_in(left_types, kOar1hTypes)) {
			return ArtClass::Dual;
		}
		std::set<double> melee;
		for (const auto* side : { &right_types, &left_types }) {
			for (double v : *side) {
				if (kOar1hTypes.count(v) || kOar2hTypes.count(v)) {
					melee.insert(v);
				}
			}
		}
		if (melee.empty()) {
			return ArtClass::Generic;
		}
		const auto all_in = [&melee](const std::set<double>& group) {
			return std::all_of(melee.begin(), melee.end(),
				[&group](double v) { return group.count(v) > 0; });
		};
		if (all_in(kOar1hTypes)) {
			return ArtClass::OneHand;
		}
		if (all_in(kOar2hTypes)) {
			return ArtClass::TwoHand;
		}
		return ArtClass::Generic;
	}

	// `../<replacer mod>/<submod>` -- relative to our own pack folder, which is how OAR resolves
	// `overrideAnimationsFolder`, and the reason the pointer survives without copying a clip.
	[[nodiscard]] inline std::string override_animations_folder(const ScannedArt& art)
	{
		return "../" + art.replacer_mod + "/" + art.name;
	}

	// The `config.json` text for one art. Separated from the filesystem so the exact JSON this
	// ships can be asserted in a test.
	//
	// `Value A` is a graph variable, never an ESP form: this mod's own behavior patch declares the
	// variable, so the condition depends on no plugin record a user might not have.
	[[nodiscard]] inline std::string build_config_json(const ScannedArt& art)
	{
		std::ostringstream out;
		out << "{\n"
			<< "    \"name\": \"" << art.name << "\",\n"
			<< "    \"priority\": " << (kPriorityBase + art.id) << ",\n"
			<< "    \"ignoreNoTriggersFlag\": true,\n"
			<< "    \"overrideAnimationsFolder\": \"" << override_animations_folder(art) << "\",\n"
			<< "    \"conditions\": [\n"
			<< "        {\n"
			<< "            \"condition\": \"CompareValues\",\n"
			<< "            \"requiredVersion\": \"1.0.0.0\",\n"
			<< "            \"Value A\": {\n"
			<< "                \"graphVariable\": \"SH2_ArtSelector\",\n"
			<< "                \"graphVariableType\": \"Int\"\n"
			<< "            },\n"
			<< "            \"Comparison\": \"==\",\n"
			<< "            \"Value B\": {\n"
			<< "                \"value\": " << art.id << ".0\n"
			<< "            }\n"
			<< "        },\n"
			<< "        {\n"
			<< "            \"condition\": \"IsActorBase\",\n"
			<< "            \"requiredVersion\": \"1.0.0.0\",\n"
			<< "            \"Actor base\": {\n"
			<< "                \"pluginName\": \"Skyrim.esm\",\n"
			<< "                \"formID\": \"7\"\n"
			<< "            }\n"
			<< "        }\n"
			<< "    ]\n"
			<< "}\n";
		return out.str();
	}

	// The pack-level `config.json`, one level above the submods.
	//
	// OAR needs this to treat the folder as a replacer mod AT ALL. A directory holding nothing but
	// submod folders is skipped SILENTLY -- no warning, no error, no mention in
	// OpenAnimationReplacer.log -- so the arts simply never replace anything and every other
	// symptom points elsewhere. Measured: 57 submod configs written and parsed by
	// nobody; the cast set SH2_ArtSelector=2, read it back as 2, and still played the base clip
	// (`SH2_Art_Clip activated with no animmotion keys`).
	[[nodiscard]] inline std::string build_pack_config_json()
	{
		return "{\n"
			   "    \"name\": \"Spell Hotbar 2 Weapon Arts (Generated)\",\n"
			   "    \"author\": \"Spell Hotbar 2\",\n"
			   "    \"description\": \"Art Selector replacements. Animation files stay in the author's folders.\"\n"
			   "}\n";
	}

	// Does the catalogue row already sitting at this art's id describe the same art?
	//
	// The generated rows are a FALLBACK, never the truth. A curated `arts_ashes.csv` carries a
	// hand-drawn icon, a hand-checked art class, and tuned costs for every art; the scan can only
	// guess the icon and derive the class from the author's own equip conditions. Registering over
	// a row that already agrees throws all of that away -- observed as all 57 Ashes of War arts
	// reverting to the generic power icon and to derived classes.
	//
	// Agreement is checked on the NAME, not on the id alone. Ids come from sorted scan order, so on
	// a machine with different Ashes of War content installed, id 5 in the CSV and id 5 on disk can
	// be two different arts -- and there the curated row is about something that is not present, so
	// the scan's own row is the correct one.
	[[nodiscard]] inline bool curated_row_wins(
		const ScannedArt& scanned, std::string_view catalogue_name)
	{
		return detail::lowered(std::string{ catalogue_name }) == detail::lowered(scanned.name);
	}

	// Walk the OAR tree, write the pack, and cache the result. Safe to call when nothing is
	// installed: an empty scan writes nothing and removes nothing.
	void generate_and_cache();

	// The cached scan, for registering the catalogue once game data is up.
	[[nodiscard]] const std::vector<ScannedArt>& cached_arts();

	// Register the cached scan into the art catalogue. Called at data load, after the CSV and
	// custom-folder loaders, so a fresh scan wins over a stale generated CSV.
	void register_cached_arts();
}
