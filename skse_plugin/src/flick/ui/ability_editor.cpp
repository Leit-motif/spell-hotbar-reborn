#include "ability_editor.h"

#include "widgets.h"
#include "../../casts/ability_damage.h"
#include "../../game_data/art_definition.h"
#include "../../game_data/custom_ability_config.h"
#include "../../game_data/custom_ability_runtime.h"
#include "../../game_data/game_data.h"
#include "../../game_data/localization.h"

// The ability (weapon art) editor, drawn through FLICK: the drafts, what Save writes back into
// the ArtDefinition and what Reset restores. It draws IN PLACE of the bind menu's content
// (bind_menu.cpp dispatches here while is_open()), so there is no window frame here, and the
// host owns the chrome and the cursor.
namespace SpellHotbar::FlickUi::AbilityEditor {
    namespace {
        bool show_dialog{ false };
        std::uint32_t editing_art_id{ 0 };
        std::array<char, 128> name_buf{};
        std::array<char, 32> cooldown_buf{};
        std::string draft_icon;
        std::uint32_t draft_icon_form{ 0 };
        ArtClass draft_class{ ArtClass::Generic };
        float draft_stamina{ 25.0f };
        float draft_magicka{ 0.0f };
        float draft_health{ 0.0f };
        float draft_gcd{ 1.0f };
        float draft_damage_mult{ 1.0f };
        std::uint32_t draft_spell_local{ vanilla_firebolt_local_form };
        std::string draft_spell_plugin{ vanilla_firebolt_plugin };
        bool draft_self_target{ false };
        bool persist_failed{ false };

        constexpr std::array<ArtClass, 5> class_values{
            ArtClass::OneHand, ArtClass::TwoHand, ArtClass::Dual, ArtClass::Bow, ArtClass::Generic };

        void load_from_art(const ArtDefinition& art)
        {
            name_buf.fill('\0');
            const auto name = art.display_name.substr(0, name_buf.size() - 1);
            std::copy(name.begin(), name.end(), name_buf.begin());
            cooldown_buf.fill('\0');
            const auto cd = art.cooldown_text.empty() ? std::string{ "8s" } : art.cooldown_text;
            const auto cd_clip = cd.substr(0, cooldown_buf.size() - 1);
            std::copy(cd_clip.begin(), cd_clip.end(), cooldown_buf.begin());
            draft_icon = art.icon;
            draft_icon_form = art.icon_form;
            draft_class = art.art_class;
            draft_stamina = art.stamina_cost;
            draft_magicka = art.magicka_cost;
            draft_health = art.health_cost;
            draft_gcd = art.gcd;
            draft_damage_mult = art.damage_mult;
            draft_spell_local = art.spell_local_form == 0 ? vanilla_firebolt_local_form : art.spell_local_form;
            draft_spell_plugin = art.spell_plugin.empty() ? vanilla_firebolt_plugin : art.spell_plugin;
            draft_self_target = art.self_target;
        }

        void apply_to_art(ArtDefinition& art)
        {
            art.display_name = name_buf.data();
            if (art.display_name.empty()) {
                if (is_custom_ability(art.id)) {
                    art.display_name = "Custom Ability " + std::to_string(art.id - custom_art_id_base);
                } else if (const ArtDefinition* catalogue = GameData::get_art_catalogue(art.id)) {
                    art.display_name = catalogue->display_name;
                }
            }
            art.icon = draft_icon;
            art.icon_form = draft_icon_form;
            art.art_class = draft_class;
            art.stamina_cost = draft_stamina;
            art.magicka_cost = draft_magicka;
            art.health_cost = draft_health;
            art.gcd = draft_gcd;
            art.damage_mult = SpellHotbar::casts::ArtDriver::sanitize_ability_damage_mult(draft_damage_mult);
            art.cooldown_text = cooldown_buf.data();
            if (art.cooldown_text.empty()) {
                art.cooldown_text = "8s";
            }
            if (const auto days = parse_art_duration_days(art.cooldown_text)) {
                art.cooldown_days = *days;
            }
            art.spell_local_form = draft_spell_local;
            art.spell_plugin = draft_spell_plugin;
            art.self_target = draft_self_target;
        }

        // FUCK has no BeginCombo, so the class list goes through FlickUi::combo, which is an
        // index over strings. The labels are built once per frame from the same art_class_label.
        void draw_class_combo()
        {
            std::vector<std::string> labels;
            labels.reserve(class_values.size());
            int index = 0;
            for (int i = 0; i < static_cast<int>(class_values.size()); ++i) {
                labels.emplace_back(art_class_label(class_values[static_cast<std::size_t>(i)]));
                if (class_values[static_cast<std::size_t>(i)] == draft_class) {
                    index = i;
                }
            }
            if (combo("##art_class", &index, labels)) {
                draft_class = class_values[static_cast<std::size_t>(std::clamp(index, 0, static_cast<int>(class_values.size()) - 1))];
            }
        }
    }

    bool is_open() { return show_dialog; }

    void open(std::uint32_t a_art_id)
    {
        const ArtDefinition* art = GameData::get_art(a_art_id);
        if (art == nullptr) {
            return;
        }
        editing_art_id = a_art_id;
        load_from_art(*art);
        persist_failed = false;
        show_dialog = true;
    }

    void close()
    {
        show_dialog = false;
        editing_art_id = 0;
    }

    void draw()
    {
        if (!show_dialog || editing_art_id == 0) {
            return;
        }

        ArtDefinition* art = GameData::get_art_mut(editing_art_id);
        if (art == nullptr) {
            close();
            return;
        }

        push_large_font();
        const float button_height = FUCK::CalcTextSize("Cancel").y + FUCK::GetStyleVarVec(ImGuiStyleVar_FramePadding).y * 2.0f +
                                    FUCK::GetStyleVarVec(ImGuiStyleVar_ItemSpacing).y * 2.0f;
        pop_font();
        const float child_window_height = FUCK::GetContentRegionAvail().y - button_height;

        FUCK::BeginChild("AbilityLeft", ImVec2(FUCK::GetContentRegionAvail().x * 0.48f, child_window_height), true);

        FUCK::TextUnformatted(translate_c("$COLUMN_NAME"));
        FUCK::InputText("##ability_name", name_buf.data(), name_buf.size());

        FUCK::TextUnformatted(translate_c("$COLUMN_ICON"));
        const ImVec2 iconpos = FUCK::GetCursorScreenPos();
        const float ic_size = std::round(scale(60.0f));
        FUCK::Dummy(ImVec2(ic_size, ic_size));
        draw_art_icon_at(draft_icon_form, draft_icon, iconpos, ic_size);

        FUCK::TextUnformatted(translate_c("$ART_CLASS"));
        draw_class_combo();

        // FUCK has no InputFloat. DragFloat with min == max is ImGui's unbounded drag, which is
        // what the stepper fields were: the old editor clamped none of these.
        FUCK::DragFloat(translate_id("$STAMINA_COST").c_str(), &draft_stamina, 1.0f, 0.0f, 0.0f, "%.0f");
        FUCK::DragFloat(translate_id("$MAGICKA_COST").c_str(), &draft_magicka, 1.0f, 0.0f, 0.0f, "%.0f");
        FUCK::DragFloat(translate_id("$HEALTH_COST").c_str(), &draft_health, 1.0f, 0.0f, 0.0f, "%.0f");
        FUCK::InputText(translate_id("$COOLDOWN").c_str(), cooldown_buf.data(), cooldown_buf.size());
        FUCK::DragFloat(translate_id("$GLOBAL_COOLDOWN").c_str(), &draft_gcd, 0.1f, 0.0f, 0.0f, "%.1f");
        if (FUCK::DragFloat(translate_id("$DAMAGE_MULTIPLIER").c_str(), &draft_damage_mult, 0.01f,
                SpellHotbar::casts::ArtDriver::kAbilityDamageMultMin,
                SpellHotbar::casts::ArtDriver::kAbilityDamageMultMax, "%.2fx")) {
            // The old 0..5 slider quantized through its pixel width, so values such as 1.50x
            // could fall between two mouse positions. Keep the stored/editor value on the same
            // hundredth displayed to the player; one horizontal pixel now advances exactly 0.01x.
            draft_damage_mult = std::clamp(std::round(draft_damage_mult * 100.0f) / 100.0f,
                SpellHotbar::casts::ArtDriver::kAbilityDamageMultMin,
                SpellHotbar::casts::ArtDriver::kAbilityDamageMultMax);
        }
        if (FUCK::IsItemHovered()) {
            FUCK::SetTooltip(translate_c("$DAMAGE_MULTIPLIER_HINT"));
        }

        FUCK::EndChild();
        FUCK::SameLine();

        FUCK::BeginChild("AbilityRight", ImVec2(0.0f, child_window_height), true, ImGuiWindowFlags_HorizontalScrollbar);
        icon_picker("ability_icons", draft_icon_form, draft_icon);
        FUCK::EndChild();

        push_large_font();
        if (FUCK::Button(translate_id("$SAVE").c_str())) {
            // Applied to the live definition before persisting, because a Custom Ability's save
            // regenerates the PI config from the live definitions. A failed write restores it, so
            // the edit cannot look saved for this session and vanish on reload; the draft stays.
            const ArtDefinition previous = *art;
            apply_to_art(*art);
            if (persist_ability(*art)) {
                close();
            } else {
                *art = previous;
                persist_failed = true;
            }
        }
        FUCK::SameLine();
        if (FUCK::Button(translate_id("$RESET").c_str())) {
            if (is_custom_ability(art->id)) {
                ArtDefinition fresh = custom_art_from_folder(static_cast<int>(art->id - custom_art_id_base),
                    "", "", "", art->has_clip);
                fresh.folder_path = art->folder_path;
                fresh.has_clip = art->has_clip;
                load_from_art(fresh);
            } else if (const ArtDefinition* catalogue = GameData::get_art_catalogue(art->id)) {
                load_from_art(*catalogue);
            }
        }
        FUCK::SameLine();
        if (FUCK::Button(translate_id("$CANCEL").c_str())) {
            close();
        }
        if (persist_failed) {
            FUCK::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", translate_c("$ABILITY_SAVE_FAILED"));
        }
        pop_font();
    }
}
