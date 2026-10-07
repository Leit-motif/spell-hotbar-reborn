#include "art_pack_gen.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include <rapidjson/document.h>

#include "../logger/logger.h"
#include "game_data.h"

namespace SpellHotbar::ArtPackGen {

	namespace {
		namespace fs = std::filesystem;

		constexpr const char* kOarRoot =
			".\\data\\meshes\\actors\\character\\animations\\OpenAnimationReplacer";
		constexpr const char* kAablClip = "aabl_attack_a.hkx";
		// Our own pack, and the probe pack we author elsewhere. Scanning our own output back in
		// would compound ids on every launch.
		constexpr const char* kOwnPackPrefix = "SpellHotbar2";

		std::vector<ScannedArt> g_cached;

		std::string lowered(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		// The pre-generated pack an install may already ship, and the core mod's own drop-in
		// folders that live inside it.
		constexpr const char* kShippedPackName = "SpellHotbar2Arts";
		constexpr const char* kCustomAbilityPrefix = "Custom_Ability_";

		// Is a ready-made art pack already installed?
		//
		// Generating beside one buys nothing and costs something: two replacer mods would target
		// the same clips at the same priorities, and OAR does not define a winner between equal
		// priorities. The `Custom_Ability_*` folders do not count -- the core mod always ships
		// those, so counting them would disable generation on every install.
		bool shipped_pack_is_populated(const fs::path& oar_root)
		{
			std::error_code ec;
			const fs::path shipped = oar_root / kShippedPackName;
			if (!fs::is_directory(shipped, ec)) {
				return false;
			}
			for (const auto& sub : fs::directory_iterator(shipped, ec)) {
				if (!sub.is_directory(ec)) {
					continue;
				}
				if (sub.path().filename().string().rfind(kCustomAbilityPrefix, 0) == 0) {
					continue;
				}
				if (fs::exists(sub.path() / "config.json", ec)) {
					return true;
				}
			}
			return false;
		}

		bool folder_holds_aabl(const fs::path& folder)
		{
			std::error_code ec;
			for (const auto& entry : fs::recursive_directory_iterator(folder, ec)) {
				if (ec) {
					break;
				}
				if (entry.is_regular_file(ec) && lowered(entry.path().filename().string()) == kAablClip) {
					return true;
				}
			}
			return false;
		}

		// Recursively collect `IsEquippedType` values out of the author's condition tree. The
		// conditions nest inside AND/OR/XOR arrays, so this cannot just walk the top level.
		void collect_equipped_types(
			const rapidjson::Value& node, std::set<double>& right, std::set<double>& left)
		{
			if (node.IsObject()) {
				const auto cond = node.FindMember("condition");
				if (cond != node.MemberEnd() && cond->value.IsString() &&
					std::string_view{ cond->value.GetString() } == "IsEquippedType") {
					const auto type = node.FindMember("Type");
					if (type != node.MemberEnd() && type->value.IsObject()) {
						const auto value = type->value.FindMember("value");
						if (value != type->value.MemberEnd() && value->value.IsNumber()) {
							const auto lh = node.FindMember("Left hand");
							const bool is_left =
								lh != node.MemberEnd() && lh->value.IsBool() && lh->value.GetBool();
							(is_left ? left : right).insert(value->value.GetDouble());
						}
					}
				}
				for (auto it = node.MemberBegin(); it != node.MemberEnd(); ++it) {
					collect_equipped_types(it->value, right, left);
				}
			} else if (node.IsArray()) {
				for (const auto& item : node.GetArray()) {
					collect_equipped_types(item, right, left);
				}
			}
		}

		struct SourceConfig {
			bool is_named_art{ false };
			ArtClass art_class{ ArtClass::Generic };
		};

		// What separates a bindable weapon art from a stance default, and why an AABL clip alone
		// is not enough.
		//
		// The Ashes of War framework ships BOTH under one OAR mod folder. A named art -- "Aimed
		// Blow", "Blood Seeker" -- is triggered by equipping a weapon-art item, so its config
		// gates on `IsWornHasKeyword` against the mod's ITEMS plugin. A stance default -- "Ashes
		// of War Dagger High" -- is the moveset for a stance you are standing in, and gates on
		// `HasPerk` plus a weapon type, never mentioning that plugin. Both carry an
		// `AABL_Attack_A.hkx`, so a has-a-clip test alone drags every stance moveset into the
		// Ability catalogue: measured, 120 entries where 57 are real arts.
		//
		// The items-plugin reference is the mod's own marker for "this is a discrete art the
		// player equips", which is exactly the thing worth binding to a hotbar slot.
		SourceConfig inspect_source_config(const fs::path& config_path)
		{
			SourceConfig result;
			std::ifstream in(config_path);
			if (!in) {
				return result;
			}
			std::ostringstream buf;
			buf << in.rdbuf();
			const auto text = buf.str();
			// Matched on the plugin NAME as it appears in any `pluginName` the author used, rather
			// than on a FormID: the keyword's id is theirs to change, the plugin name is stable.
			result.is_named_art = text.find("Ashes of War Additional Attack") != std::string::npos;
			rapidjson::Document doc;
			// The author's file, not ours: a malformed one is their problem, and Generic is the
			// safe answer because it gates on nothing.
			if (doc.Parse(text.c_str()).HasParseError()) {
				logger::warn("SH2 art pack: could not parse '{}', classing it Generic",
					config_path.string());
				return result;
			}
			std::set<double> right;
			std::set<double> left;
			collect_equipped_types(doc, right, left);
			result.art_class = classify_art_class(right, left);
			return result;
		}

		bool write_if_changed(const fs::path& path, const std::string& text)
		{
			std::ifstream in(path);
			if (in) {
				std::ostringstream buf;
				buf << in.rdbuf();
				if (buf.str() == text) {
					return false;  // byte-identical; leave the timestamp alone
				}
				in.close();
			}
			std::error_code ec;
			fs::create_directories(path.parent_path(), ec);
			std::ofstream out(path, std::ofstream::trunc);
			if (!out) {
				logger::error("SH2 art pack: could not write '{}'", path.string());
				return false;
			}
			out << text;
			return true;
		}
	}  // namespace

	void generate_and_cache()
	{
		g_cached.clear();
		std::error_code ec;
		const fs::path oar_root{ kOarRoot };
		if (!fs::is_directory(oar_root, ec)) {
			logger::info("SH2 art pack: no OAR directory; nothing to generate");
			return;
		}
		if (shipped_pack_is_populated(oar_root)) {
			logger::info("SH2 art pack: '{}' is already populated; generating nothing",
				kShippedPackName);
			return;
		}

		std::vector<ScannedArt> found;
		for (const auto& mod_dir : fs::directory_iterator(oar_root, ec)) {
			if (!mod_dir.is_directory(ec)) {
				continue;
			}
			const auto mod_name = mod_dir.path().filename().string();
			// Never scan our own output back in.
			if (mod_name.rfind(kOwnPackPrefix, 0) == 0) {
				continue;
			}
			for (const auto& sub_dir : fs::directory_iterator(mod_dir.path(), ec)) {
				if (!sub_dir.is_directory(ec) || !folder_holds_aabl(sub_dir.path())) {
					continue;
				}
				const auto source = inspect_source_config(sub_dir.path() / "config.json");
				if (!source.is_named_art) {
					continue;  // a stance default, not a bindable art -- see inspect_source_config
				}
				ScannedArt art;
				art.name = sub_dir.path().filename().string();
				art.replacer_mod = mod_name;
				art.art_class = source.art_class;
				found.push_back(std::move(art));
			}
		}

		if (found.empty()) {
			logger::info("SH2 art pack: no Ashes of War submods found; nothing to generate");
			return;
		}

		assign_ids(found);

		const fs::path pack_root = oar_root / kPackName;
		// The pack's own config.json first: without it OAR never registers the folder and every
		// submod below is dead weight. See build_pack_config_json.
		const bool pack_written = write_if_changed(pack_root / "config.json", build_pack_config_json());
		int written = 0;
		for (const auto& art : found) {
			if (write_if_changed(pack_root / art.name / "config.json", build_config_json(art))) {
				++written;
			}
		}
		g_cached = std::move(found);
		logger::info("SH2 art pack: {} arts scanned, {} config(s) written, pack config {}",
			g_cached.size(), written, pack_written ? "written" : "unchanged");
	}

	const std::vector<ScannedArt>& cached_arts()
	{
		return g_cached;
	}

	void register_cached_arts()
	{
		int kept = 0;
		int added = 0;
		for (const auto& scanned : g_cached) {
			// A curated catalogue row for this art beats anything the scan can infer.
			// See curated_row_wins.
			if (const ArtDefinition* existing = GameData::get_art(static_cast<std::uint32_t>(scanned.id));
				existing && curated_row_wins(scanned, existing->display_name)) {
				++kept;
				continue;
			}
			++added;
			ArtDefinition art;
			art.id = static_cast<std::uint32_t>(scanned.id);
			art.display_name = scanned.name;
			art.icon = "GREATER_POWER";
			art.selector = scanned.id;
			art.art_class = scanned.art_class;
			art.stamina_cost = 25.0f;
			// Both halves, or the cooldown silently never applies: `cooldown_days` is what the
			// runtime checks and the CSV path fills it by parsing the text.
			art.cooldown_text = "8s";
			if (const auto days = parse_art_duration_days(art.cooldown_text)) {
				art.cooldown_days = *days;
			}
			// The action-class number. This is the live default -- arts.csv ships with
			// a header and no rows, so every scanned art takes it.
			art.gcd = 1.5f;
			art.has_clip = true;
			GameData::set_art(std::move(art));
		}
		if (!g_cached.empty()) {
			logger::info("SH2 art pack: {} scanned arts -- {} kept from the catalogue, {} added",
				g_cached.size(), kept, added);
		}
	}
}
