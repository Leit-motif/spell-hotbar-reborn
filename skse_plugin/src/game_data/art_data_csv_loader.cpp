#include "art_data_csv_loader.h"
#include "art_definition.h"
#include "custom_ability_config.h"
#include "custom_ability_runtime.h"
#include "csv_loader.h"
#include "game_data.h"
#include "../logger/logger.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace SpellHotbar::ArtDataCSVLoader {

	namespace {

	std::string read_text(const std::filesystem::path& path)
	{
		std::ifstream in(path);
		if (!in) {
			return {};
		}
		std::ostringstream buf;
		buf << in.rdbuf();
		return buf.str();
	}

	bool is_aabl_clip(const std::filesystem::path& path)
	{
		auto name = path.filename().string();
		std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return name == "aabl_attack_a.hkx";
	}

	bool folder_has_aabl(const std::filesystem::path& folder)
	{
		std::error_code ec;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(folder, ec)) {
			if (ec) {
				break;
			}
			if (entry.is_regular_file() && is_aabl_clip(entry.path())) {
				return true;
			}
		}
		return false;
	}

	}  // namespace

	void load_art_file(const std::string& path)
	{
		std::ifstream in(path);
		if (!in) {
			logger::error("Could not open art data '{}'", path);
			return;
		}
		std::ostringstream buf;
		buf << in.rdbuf();
		auto arts = parse_art_tsv(buf.str());
		if (arts.empty()) {
			logger::warn("Could not parse '{}', skipping", path);
			return;
		}
		for (auto& art : arts) {
			logger::info("Loaded ability {} '{}' selector={} class={}", art.id, art.display_name, art.selector,
				art_class_label(art.art_class));
			GameData::set_art(std::move(art));
		}
	}

	void load_art_data(std::filesystem::path folder)
	{
		if (!std::filesystem::exists(folder)) {
			logger::warn("Ability data folder '{}' is missing; no Abilities loaded", folder.string());
			return;
		}
		csv::load_folder(folder, "artdata", load_art_file);
	}

	void load_custom_art_folders(std::filesystem::path pack_root)
	{
		std::error_code ec;
		if (!std::filesystem::exists(pack_root, ec) || !std::filesystem::is_directory(pack_root, ec)) {
			logger::warn("Custom Art Folder pack '{}' is missing", pack_root.string());
			return;
		}
		for (const auto& entry : std::filesystem::directory_iterator(pack_root, ec)) {
			if (!entry.is_directory()) {
				continue;
			}
			const auto name = entry.path().filename().string();
			const auto number = parse_custom_art_folder_number(name);
			if (!number) {
				continue;
			}
			auto art = custom_art_from_folder(*number, name, read_text(entry.path() / "name.txt"),
				read_text(entry.path() / "icon.txt"), folder_has_aabl(entry.path()),
				read_text(entry.path() / custom_ability_sidecar_filename));
			art.folder_path = entry.path().string();
			if (!art.has_clip) {
				logger::warn("SH2 art: empty Custom Ability {} (no AABL_Attack_A.hkx)", name);
			}
			logger::info("Loaded custom art {} '{}' selector={} clip={}", art.id, art.display_name, art.selector,
				art.has_clip);
			GameData::set_art(art);
			if (art.has_clip) {
				stamp_custom_ability_clip(art);
			}
		}
		emit_custom_ability_pi_config();
	}
}
