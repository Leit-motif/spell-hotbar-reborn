#include "keynames_csv_loader.h"
#include "gamepad_key_names.h"
#include "game_data.h"
#include "csv_loader.h"
#include "rendering/render_manager.h"

namespace SpellHotbar::KeyNamesCSVLoader {

bool is_keynames_file(rapidcsv::Document& doc) {
    std::vector<std::string> columns = doc.GetColumnNames();

    // check required column names
    return csv::has_column(columns, "Code") && csv::has_column(columns, "Text") && csv::has_column(columns, "Short") && csv::has_column(columns, "IconPath");
}

void load_keynames(const std::string & keynames_csv)
{
    GameData::key_names.clear();
    GameData::playstation_key_names.clear();
    GameData::steam_key_names.clear();
    GameData::key_icons_available = false;
    auto keynames_path = std::filesystem::path(keynames_csv);
    if (!std::filesystem::exists(keynames_path)) {
        logger::error("Missing '{}', this is an installation error!", keynames_csv);
        return;
    }

    rapidcsv::Document doc(keynames_csv, rapidcsv::LabelParams(0, -1), rapidcsv::SeparatorParams('\t'));

    // check the column names
    if (!is_keynames_file(doc)) {
        logger::error("Could not load '{}'!", keynames_csv);
    }
    else {
        GameData::key_icons_available = true;
        for (size_t i = 0; i < doc.GetRowCount(); i++) {
            try {

                int code = doc.GetCell<int>("Code", i);
                std::string text = doc.GetCell<std::string>("Text", i);
                std::string text_short = doc.GetCell<std::string>("Short", i);
                text = GameData::unambiguous_gamepad_key_name(code, text);
                text_short = GameData::unambiguous_gamepad_key_name(code, text_short);

                std::string icon_path = doc.GetCell<std::string>("IconPath", i);

                const auto* family = GameData::stock_controller_label(code, text, text_short, icon_path);
                if (family != nullptr) {
                    icon_path = "SKSE/Plugins/SpellHotbar/keynames/xbox/" + std::to_string(code) + ".png";
                }
                int tex_idx{ -1 };
                if (GameData::key_icons_available) {
                    tex_idx = RenderManager::load_texture_return_index("./data/" + icon_path);
                    if (tex_idx < 0) {
                        GameData::key_icons_available = false;
                        logger::warn("Loading ImGUI Icons failed, disabling.");
                    }
                }

                GameData::key_names.emplace(code, GameData::Key_Data(text_short, text, tex_idx));
                if (family != nullptr) {
                    const auto load_family = [code](auto& table, const char* directory, std::string_view name) {
                        const std::string path = "./data/SKSE/Plugins/SpellHotbar/keynames/" + std::string(directory) + "/" + std::to_string(code) + ".png";
                        const int texture = RenderManager::load_texture_return_index(path);
                        // Missing family glyphs fall back to text, never to a different family.
                        table.emplace(code, GameData::Key_Data(std::string(name), std::string(name), texture));
                    };
                    load_family(GameData::playstation_key_names, "playstation", family->playstation);
                    load_family(GameData::steam_key_names, "steam", family->steam);
                }
            }
            catch (const std::exception& e) {
                GameData::key_icons_available = false;
                std::string msg = e.what();
                logger::error("Error Loading csv: {}", msg);
            }
        }
    }
}
}
