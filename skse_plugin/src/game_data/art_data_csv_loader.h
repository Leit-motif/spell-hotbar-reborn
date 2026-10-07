#pragma once
#include <filesystem>

namespace SpellHotbar::ArtDataCSVLoader {
	void load_art_data(std::filesystem::path folder);
	void load_custom_art_folders(std::filesystem::path pack_root);
}
