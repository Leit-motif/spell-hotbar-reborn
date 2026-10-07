#include "action_editor.h"

#include "widgets.h"
#include "../../game_data/action_definition.h"
#include "../../game_data/game_data.h"
#include "../../game_data/localization.h"
#include "../../mcp/bind_capture.h"

// The action editor, drawn through FLICK: drafts, capture arming and what Save persists. It
// draws IN PLACE of the bind menu's content, so there is no window frame here.
namespace SpellHotbar::FlickUi::ActionEditor {
    namespace {
        bool show_dialog{ false };
        std::uint32_t editing_action_id{ 0 };
        std::array<char, 128> name_buf{};
        std::string draft_icon;
        std::uint32_t draft_icon_form{ 0 };
        ActionKind draft_kind{ ActionKind::physical_scancode };
        ActionTargetSource draft_target{ ActionTargetSource::captured };
        ActionInputDevice draft_captured_device{ ActionInputDevice::keyboard };
        std::uint32_t draft_captured_scancode{ 0 };
        float draft_stamina{ 0.0f };
        float draft_magicka{ 0.0f };
        float draft_health{ 0.0f };
        float draft_cooldown_days{ 0.0f };
        float draft_gcd{ 0.0f };
        bool persist_failed{ false };

        const char* action_kind_key(ActionKind kind)
        {
            switch (kind) {
            case ActionKind::physical_scancode:
                return "$ACTION_KIND_PHYSICAL";
            case ActionKind::light_attack:
                return "$ACTION_KIND_LIGHT_ATTACK";
            case ActionKind::power_attack:
                return "$ACTION_KIND_POWER_ATTACK";
            }
            return "$ACTION_KIND_PHYSICAL";
        }

        // The order the combo lists, matching the stored enum values.
        constexpr std::array<ActionKind, 3> action_kinds{
            ActionKind::physical_scancode,
            ActionKind::light_attack,
            ActionKind::power_attack,
        };

        const char* action_target_key(ActionTargetSource target)
        {
            switch (target) {
            case ActionTargetSource::captured:
                return "$ACTION_TARGET_CAPTURED";
            }
            return "$ACTION_TARGET_CAPTURED";
        }

        const char* action_input_device_key(ActionInputDevice device)
        {
            switch (device) {
            case ActionInputDevice::keyboard:
                return "$ACTION_DEVICE_KEYBOARD";
            case ActionInputDevice::mouse:
                return "$ACTION_DEVICE_MOUSE";
            case ActionInputDevice::gamepad:
                return "$ACTION_DEVICE_GAMEPAD";
            }
            return "$ACTION_DEVICE_KEYBOARD";
        }

        std::string captured_input_label()
        {
            if (draft_captured_scancode == 0) {
                return translate("$ACTION_UNBOUND");
            }
            return translate(action_input_device_key(draft_captured_device)) + " (DX " +
                std::to_string(draft_captured_scancode) + ")";
        }

        void load_from_action(const ActionDefinition& action)
        {
            name_buf.fill('\0');
            const auto name = action.display_name.substr(0, name_buf.size() - 1);
            std::copy(name.begin(), name.end(), name_buf.begin());
            draft_icon = action.icon;
            draft_icon_form = action.icon_form;
            draft_kind = action.kind;
            draft_target = action.target;
            draft_captured_device = action.captured_device;
            draft_captured_scancode = action.captured_scancode;
            draft_stamina = std::max(action.stamina_cost, 0.0f);
            draft_magicka = std::max(action.magicka_cost, 0.0f);
            draft_health = std::max(action.health_cost, 0.0f);
            draft_cooldown_days = std::max(action.cooldown_days, 0.0f);
            draft_gcd = std::max(action.gcd, 0.0f);
        }

        void apply_to_action(ActionDefinition& action)
        {
            action.display_name = name_buf.data();
            if (action.display_name.empty()) {
                if (const ActionDefinition* catalogue = GameData::get_action_catalogue(action.id)) {
                    action.display_name = catalogue->display_name;
                }
            }
            action.icon = draft_icon;
            action.icon_form = draft_icon_form;
            action.kind = draft_kind;
            action.target = draft_target;
            action.captured_device = draft_captured_device;
            action.captured_scancode = draft_captured_scancode;
            action.stamina_cost = std::max(draft_stamina, 0.0f);
            action.magicka_cost = std::max(draft_magicka, 0.0f);
            action.health_cost = std::max(draft_health, 0.0f);
            action.cooldown_days = std::max(draft_cooldown_days, 0.0f);
            action.gcd = std::max(draft_gcd, 0.0f);
        }

        // FUCK has no BeginCombo; the kind list goes through FlickUi::combo, an index over
        // strings, built from the same action_kind_key labels.
        void draw_kind_combo()
        {
            std::vector<std::string> labels;
            labels.reserve(action_kinds.size());
            int index = 0;
            for (int i = 0; i < static_cast<int>(action_kinds.size()); ++i) {
                labels.emplace_back(translate_c(action_kind_key(action_kinds[static_cast<std::size_t>(i)])));
                if (action_kinds[static_cast<std::size_t>(i)] == draft_kind) {
                    index = i;
                }
            }
            if (combo("##action_kind", &index, labels)) {
                draft_kind = action_kinds[static_cast<std::size_t>(std::clamp(index, 0, static_cast<int>(action_kinds.size()) - 1))];
            }
        }

        void update_captured_input_from_event()
        {
            if (const auto result = Mcp::bind_capture().take_action_capture_result(editing_action_id)) {
                draft_captured_device = result->input.device;
                draft_captured_scancode = result->input.dx_scancode;
            }
        }
    }

    bool is_open() { return show_dialog; }

    void open(std::uint32_t a_action_id)
    {
        const ActionDefinition* action = GameData::get_action(a_action_id);
        if (action == nullptr) {
            return;
        }
        editing_action_id = a_action_id;
        load_from_action(*action);
        persist_failed = false;
        show_dialog = true;
    }

    void close()
    {
        if (editing_action_id != 0) {
            auto& capture = Mcp::bind_capture();
            if (capture.action_armed() && capture.pending_action_id() == editing_action_id) {
                capture.cancel();
            }
            capture.discard_action_capture_result(editing_action_id);
        }
        show_dialog = false;
        editing_action_id = 0;
        persist_failed = false;
    }

    void draw()
    {
        if (!show_dialog || editing_action_id == 0) {
            return;
        }

        ActionDefinition* action = GameData::get_action_mut(editing_action_id);
        if (action == nullptr) {
            close();
            return;
        }
        update_captured_input_from_event();

        push_large_font();
        const float button_height = FUCK::CalcTextSize("Cancel").y + FUCK::GetStyleVarVec(ImGuiStyleVar_FramePadding).y * 2.0f +
                                    FUCK::GetStyleVarVec(ImGuiStyleVar_ItemSpacing).y * 2.0f;
        pop_font();
        const float child_window_height = FUCK::GetContentRegionAvail().y - button_height;

        FUCK::BeginChild("ActionLeft", ImVec2(FUCK::GetContentRegionAvail().x * 0.48f, child_window_height), true);
        FUCK::TextUnformatted(translate_c("$COLUMN_NAME"));
        FUCK::InputText("##action_name", name_buf.data(), name_buf.size());

        FUCK::TextUnformatted(translate_c("$COLUMN_ICON"));
        const ImVec2 icon_pos = FUCK::GetCursorScreenPos();
        const float icon_size = std::round(scale(60.0f));
        FUCK::Dummy(ImVec2(icon_size, icon_size));
        draw_art_icon_at(draft_icon_form, draft_icon, icon_pos, icon_size);

        FUCK::TextUnformatted(translate_c("$ACTION_KIND"));
        draw_kind_combo();

        // A graph kind has no target to resolve and nothing to capture: it sends its attack event
        // straight to the player's animation graph. Hide both controls and say why instead.
        const bool graph_kind = draft_kind != ActionKind::physical_scancode;
        if (graph_kind) {
            FUCK::TextWrapped("%s", translate_c("$ACTION_KIND_GRAPH_HELP"));
        }

        // One source since the OCPA and TK Dodge readers came out, so a dropdown
        // offers the player no choice. Show what it is instead, the same way the kind is shown;
        // draft_target still round-trips the value so a second source only needs the control back.
        if (!graph_kind) {
            FUCK::TextUnformatted(translate_c("$ACTION_TARGET"));
            FUCK::TextUnformatted(translate_c(action_target_key(draft_target)));

            const bool captured_target = draft_target == ActionTargetSource::captured;
            if (captured_target) {
                FUCK::Text("%s: %s", translate_c("$ACTION_SCANCODE"), captured_input_label().c_str());
                FUCK::SameLine();
                auto& capture = Mcp::bind_capture();
                const bool capture_armed = capture.action_armed() &&
                    capture.pending_action_id() == editing_action_id;
                const char* capture_key = capture_armed ? "$ACTION_CAPTURE_CANCEL" : "$ACTION_CAPTURE";
                if (FUCK::Button(translate_id(capture_key).c_str())) {
                    if (capture_armed) {
                        capture.cancel();
                    } else {
                        capture.arm_action(editing_action_id);
                    }
                }
                if (capture_armed) {
                    FUCK::TextUnformatted(translate_c("$ACTION_CAPTURE_ARMED"));
                }
            } else {
                FUCK::BeginDisabled();
                FUCK::Text("%s: %s", translate_c("$ACTION_SCANCODE"), captured_input_label().c_str());
                FUCK::EndDisabled();
            }
        }

        // FUCK has no InputFloat. These five were stepper fields with no clamp in the control
        // itself -- apply_to_action() is what floors them at zero -- so an unbounded DragFloat
        // (min == max) is the same control.
        FUCK::DragFloat(translate_id("$STAMINA_COST").c_str(), &draft_stamina, 1.0f, 0.0f, 0.0f, "%.0f");
        FUCK::DragFloat(translate_id("$MAGICKA_COST").c_str(), &draft_magicka, 1.0f, 0.0f, 0.0f, "%.0f");
        FUCK::DragFloat(translate_id("$HEALTH_COST").c_str(), &draft_health, 1.0f, 0.0f, 0.0f, "%.0f");
        FUCK::DragFloat(translate_id("$COOLDOWN_DAYS").c_str(), &draft_cooldown_days, 0.01f, 0.0f, 0.0f, "%.4f");
        FUCK::DragFloat(translate_id("$GLOBAL_COOLDOWN").c_str(), &draft_gcd, 0.1f, 0.0f, 0.0f, "%.1f");

        FUCK::EndChild();
        FUCK::SameLine();

        FUCK::BeginChild("ActionRight", ImVec2(0.0f, child_window_height), true, ImGuiWindowFlags_HorizontalScrollbar);
        icon_picker("action_icons", draft_icon_form, draft_icon);
        FUCK::EndChild();

        push_large_font();
        if (FUCK::Button(translate_id("$SAVE").c_str())) {
            ActionDefinition candidate = *action;
            apply_to_action(candidate);
            if (GameData::persist_action_player_overlay(candidate)) {
                *action = std::move(candidate);
                close();
            } else {
                persist_failed = true;
            }
        }
        FUCK::SameLine();
        if (FUCK::Button(translate_id("$RESET").c_str())) {
            if (const ActionDefinition* catalogue = GameData::get_action_catalogue(action->id)) {
                load_from_action(*catalogue);
            }
        }
        FUCK::SameLine();
        if (FUCK::Button(translate_id("$CANCEL").c_str())) {
            close();
        }
        if (persist_failed) {
            FUCK::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", translate_c("$ACTION_SAVE_FAILED"));
        }
        pop_font();
    }
}
