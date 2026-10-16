#pragma once

#include "gamepad_key_names.h"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <rapidjson/document.h>

namespace SpellHotbar::GameData {
    enum class ControllerLabelStyle : std::uint8_t { xbox, playstation, steam };

    constexpr ControllerLabelStyle controller_label_style(int value)
    {
        return value >= 0 && value <= 2 ? static_cast<ControllerLabelStyle>(value) : ControllerLabelStyle::xbox;
    }

    inline constexpr const char* controller_label_json_key = "settings.controller_labels";
    inline constexpr std::uint32_t controller_label_record_version = 1;

    inline ControllerLabelStyle read_controller_labels_json(const rapidjson::Value& root)
    {
        if (!root.IsObject() || !root.HasMember(controller_label_json_key) ||
            !root[controller_label_json_key].IsInt()) { return ControllerLabelStyle::xbox; }
        return controller_label_style(root[controller_label_json_key].GetInt());
    }

    constexpr ControllerLabelStyle read_controller_labels_record(
        std::uint32_t version, std::uint32_t length, std::uint8_t value)
    {
        return version == controller_label_record_version && length == 1 ?
            controller_label_style(value) : ControllerLabelStyle::xbox;
    }

    template <class Table>
    const Table& controller_key_table(const Table& original, const Table& playstation,
        const Table& steam, ControllerLabelStyle style)
    {
        if (style == ControllerLabelStyle::playstation) { return playstation; }
        if (style == ControllerLabelStyle::steam) { return steam; }
        return original;
    }

    template <class Table>
    const typename Table::mapped_type& select_controller_key_name(const Table& original,
        const Table& playstation, const Table& steam, ControllerLabelStyle style, int code)
    {
        const auto& table = controller_key_table(original, playstation, steam, style);
        const auto it = table.find(code);
        return it != table.end() ? it->second : original.at(code);
    }

    inline std::vector<int> controller_binding_keys(int button, std::uint16_t modifiers)
    {
        if (button <= 0) { return {}; }
        std::vector<int> keys;
        for (int code = 266; code <= 281; ++code) {
            if (modifiers & (1U << (code - 266))) { keys.push_back(code); }
        }
        keys.push_back(button);
        return keys;
    }

    template <class Lookup>
    std::string format_controller_binding(int button, std::uint16_t modifiers, Lookup name)
    {
        if (button <= 0) { return name(button); }
        std::string text;
        for (int code : controller_binding_keys(button, modifiers)) {
            if (!text.empty()) { text += "+"; }
            text += name(code);
        }
        return text;
    }

    struct ControllerLabel {
        std::string_view xbox;
        std::string_view short_xbox;
        std::string_view icon;
        std::string_view playstation;
        std::string_view steam;
    };

    // Native SKSE codes 266..281, independent of the player's control map.
    inline constexpr std::array<ControllerLabel, 16> controller_labels{{
        {"DUp", "DU", "Up", "DUp", "DUp"}, {"DDn", "DD", "Down", "DDn", "DDn"},
        {"DL", "DL", "Left", "DL", "DL"}, {"DR", "DR", "Right", "DR", "DR"},
        {"Start", "St", "360_Start", "Options", "Menu"}, {"Back", "Bk", "360_Back", "Share", "View"},
        {"LTh", "LTh", "360_LS", "L3", "L3"}, {"RTh", "RTh", "360_RS", "R3", "R3"},
        {"LB", "LB", "360_LB", "L1", "L1"}, {"RB", "RB", "360_RB", "R1", "R1"},
        {"A", "A", "360_A", "Cross", "A"}, {"B", "B", "360_B", "Circle", "B"},
        {"X", "X", "360_X", "Square", "X"}, {"Y", "Y", "360_Y", "Triangle", "Y"},
        {"LT", "LT", "360_LT", "L2", "L2"}, {"RT", "RT", "360_RT", "R2", "R2"},
    }};

    // Only a wholly stock row opts into family presentation. A custom label OR icon
    // preserves the entire row, including on a switch back to Xbox or a resource reload.
    inline const ControllerLabel* stock_controller_label(int code, std::string_view text,
        std::string_view short_text, std::string_view icon_path)
    {
        if (code < 266 || code > 281) { return nullptr; }
        const auto& label = controller_labels[code - 266];
        std::string normalized(icon_path);
        for (auto& c : normalized) {
            if (c == '\\') { c = '/'; }
            if (c >= 'A' && c <= 'Z') { c += 'a' - 'A'; }
        }
        std::string expected = "Interface/ImGuiIcons/Icons/" + std::string(label.icon) + ".png";
        for (auto& c : expected) { if (c >= 'A' && c <= 'Z') { c += 'a' - 'A'; } }
        return unambiguous_gamepad_key_name(code, text) == label.xbox &&
            unambiguous_gamepad_key_name(code, short_text) == label.short_xbox && normalized == expected ? &label : nullptr;
    }
}
