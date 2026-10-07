#include "art_icon_edit_dialog.h"

#include "widgets.h"
#include "../../game_data/art_definition.h"
#include "../../game_data/game_data.h"
#include "../../game_data/localization.h"

// The ability icon dialog, drawn through FLICK: draft versus saved versus catalogue, and what
// Save persists. It draws IN PLACE of the bind menu's content, so there is no window frame here.
namespace SpellHotbar::FlickUi::ArtIconEditor {
    namespace {
        bool show_dialog{ false };
        std::uint32_t editing_art_id{ 0 };
        std::string draft_icon;
        std::uint32_t draft_icon_form{ 0 };
        std::string saved_icon;
        std::uint32_t saved_icon_form{ 0 };
        std::string catalogue_icon;
        std::uint32_t catalogue_icon_form{ 0 };

        constexpr FUCK::TableFlags table_flag_no_borders_in_body = static_cast<FUCK::TableFlags>(ImGuiTableFlags_NoBordersInBody);
        constexpr FUCK::TableFlags table_flag_scroll_y = static_cast<FUCK::TableFlags>(ImGuiTableFlags_ScrollY);

        bool draft_differs_from_saved()
        {
            return draft_icon != saved_icon || draft_icon_form != saved_icon_form;
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
        draft_icon = art->icon;
        draft_icon_form = art->icon_form;
        saved_icon = art->icon;
        saved_icon_form = art->icon_form;
        catalogue_icon = art->icon;
        catalogue_icon_form = art->icon_form;
        GameData::get_art_catalogue_icon(a_art_id, catalogue_icon, catalogue_icon_form);
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

        const ArtDefinition* art = GameData::get_art(editing_art_id);
        if (art == nullptr) {
            close();
            return;
        }

        push_large_font();
        const float button_height = FUCK::CalcTextSize("Cancel").y + FUCK::GetStyleVarVec(ImGuiStyleVar_FramePadding).y * 2.0f +
                                    FUCK::GetStyleVarVec(ImGuiStyleVar_ItemSpacing).y * 2.0f;
        pop_font();
        const float child_window_height = FUCK::GetContentRegionAvail().y - button_height;

        FUCK::BeginChild("LeftTab", ImVec2(FUCK::GetContentRegionAvail().x * 0.5f, child_window_height), false,
                         ImGuiWindowFlags_HorizontalScrollbar);

        using TF = FUCK::TableFlags;
        const TF flags = TF::kRowBg | TF::kBordersOuterH | TF::kBordersOuterV | TF::kBordersInnerV |
                         table_flag_no_borders_in_body | table_flag_scroll_y;

        if (FUCK::BeginTable("ArtIconData", 2, flags, ImVec2(0.0f, 0.0f), 0.0f)) {
            using CF = FUCK::TableColumnFlags;
            FUCK::TableSetupColumn("", CF::kNoSort | CF::kWidthFixed, 0.0f, 0);
            FUCK::TableSetupColumn("", CF::kNoSort | CF::kWidthStretch, 0.0f, 1);
            FUCK::TableSetupScrollFreeze(0, 1);
            FUCK::TableHeadersRow();

            FUCK::TableNextRow();
            FUCK::TableNextColumn();
            FUCK::TextUnformatted(translate_c("$COLUMN_NAME"));
            FUCK::TableNextColumn();
            FUCK::TextUnformatted(art->display_name.c_str());

            FUCK::TableNextRow();
            FUCK::TableNextColumn();
            FUCK::TextUnformatted(translate_c("$COLUMN_ICON"));
            FUCK::TableNextColumn();
            const ImVec2 iconpos = FUCK::GetCursorScreenPos();
            const float ic_size = std::round(scale(60.0f));
            FUCK::Dummy(ImVec2(ic_size, ic_size));
            draw_art_icon_at(draft_icon_form, draft_icon, iconpos, ic_size);

            const bool show_reset = draft_icon != catalogue_icon || draft_icon_form != catalogue_icon_form;
            if (show_reset) {
                FUCK::SameLine();
                if (FUCK::Button((translate("$RESET") + "##reset_art_icon").c_str())) {
                    draft_icon = catalogue_icon;
                    draft_icon_form = catalogue_icon_form;
                }
            }

            FUCK::EndTable();
        }
        FUCK::EndChild();

        FUCK::SameLine();
        FUCK::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
        FUCK::BeginChild("RightTab", ImVec2(0.0f, child_window_height), false, ImGuiWindowFlags_HorizontalScrollbar);
        icon_picker("art_icons", draft_icon_form, draft_icon);
        FUCK::EndChild();
        FUCK::PopStyleVar(1);

        const bool save_enabled = draft_differs_from_saved();
        push_large_font();
        if (!save_enabled) {
            FUCK::BeginDisabled();
        }
        if (FUCK::Button(translate_id("$SAVE").c_str())) {
            if (draft_icon == catalogue_icon && draft_icon_form == catalogue_icon_form) {
                GameData::reset_art_icon(editing_art_id);
            } else {
                GameData::set_art_icon(editing_art_id, draft_icon, draft_icon_form);
            }
            GameData::persist_user_art_icons();
            close();
        }
        if (!save_enabled) {
            FUCK::EndDisabled();
        }
        FUCK::SameLine();
        if (FUCK::Button(translate_id("$CANCEL").c_str())) {
            close();
        }
        pop_font();
    }
}
