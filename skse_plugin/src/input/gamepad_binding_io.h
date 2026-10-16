#pragma once

#include "gamepad_binding.h"
#include <vector>
#include <rapidjson/document.h>

namespace SpellHotbar::Input {
    inline constexpr std::uint32_t gamepad_record_version = 1;
    inline constexpr std::size_t max_gamepad_record_bytes = 1 + 23 * 5;
    inline constexpr const char* gamepad_json_key = "keybind.gamepad_combinations";

    constexpr bool supports_gamepad_combination(int id)
    {
        return (id >= 0 && id <= 13) || id == 19 || id == 20 || id == 22;
    }

    struct GamepadBindingRecord {
        std::uint8_t id;
        GamepadBinding binding;
        bool operator==(const GamepadBindingRecord&) const = default;
    };

    inline bool valid_gamepad_records(std::span<const GamepadBindingRecord> records)
    {
        std::uint32_t seen = 0;
        for (const auto& row : records) {
            if (!supports_gamepad_combination(row.id) || !row.binding.valid() || !row.binding.modifiers || (seen & (1U << row.id))) { return false; }
            seen |= 1U << row.id;
            for (const auto& other : records) {
                if (other.id != row.id && other.binding == row.binding) { return false; }
            }
        }
        return true;
    }

    inline std::vector<std::uint8_t> encode_gamepad_bindings(std::span<const GamepadBindingRecord> records)
    {
        if (!valid_gamepad_records(records)) { return {}; }
        std::vector<std::uint8_t> bytes{ static_cast<std::uint8_t>(records.size()) };
        for (const auto& row : records) {
            bytes.push_back(row.id);
            bytes.push_back(static_cast<std::uint8_t>(row.binding.button & 255));
            bytes.push_back(static_cast<std::uint8_t>(row.binding.button >> 8));
            bytes.push_back(static_cast<std::uint8_t>(row.binding.modifiers & 255));
            bytes.push_back(static_cast<std::uint8_t>(row.binding.modifiers >> 8));
        }
        return bytes;
    }

    inline bool decode_gamepad_bindings(std::span<const std::uint8_t> bytes, std::vector<GamepadBindingRecord>& result)
    {
        if (bytes.empty() || bytes.size() > max_gamepad_record_bytes || bytes.size() != 1U + bytes[0] * 5U) { return false; }
        std::vector<GamepadBindingRecord> rows;
        for (std::size_t i = 1; i < bytes.size(); i += 5) {
            rows.push_back({ bytes[i], { bytes[i + 1] | (bytes[i + 2] << 8),
                static_cast<std::uint16_t>(bytes[i + 3] | (bytes[i + 4] << 8)) } });
        }
        if (!valid_gamepad_records(rows)) { return false; }
        result = std::move(rows);
        return true;
    }

    inline bool read_gamepad_bindings_json(const rapidjson::Value& root, std::vector<GamepadBindingRecord>& result)
    {
        std::vector<GamepadBindingRecord> rows;
        if (root.HasMember(gamepad_json_key)) {
            const auto& list = root[gamepad_json_key];
            if (!list.IsArray() || list.Size() > 23) { return false; }
            for (const auto& row : list.GetArray()) {
                if (!row.IsObject() || !row.HasMember("id") || !row["id"].IsUint() || row["id"].GetUint() >= 23 ||
                    !row.HasMember("button") || !row["button"].IsInt() ||
                    !row.HasMember("modifiers") || !row["modifiers"].IsArray()) { return false; }
                GamepadBinding binding{ row["button"].GetInt(), 0 };
                for (const auto& modifier : row["modifiers"].GetArray()) {
                    if (!modifier.IsInt() || !is_gamepad_button(modifier.GetInt())) { return false; }
                    const auto bit = gamepad_bit(modifier.GetInt());
                    if (binding.modifiers & bit) { return false; }
                    binding.modifiers |= bit;
                }
                rows.push_back({ static_cast<std::uint8_t>(row["id"].GetUint()), binding });
            }
        }
        if (!valid_gamepad_records(rows)) { return false; }
        result = std::move(rows);
        return true;
    }

    inline void write_gamepad_bindings_json(rapidjson::Document& root, std::span<const GamepadBindingRecord> records)
    {
        auto& allocator = root.GetAllocator();
        rapidjson::Value list(rapidjson::kArrayType);
        for (const auto& row : records) {
            rapidjson::Value item(rapidjson::kObjectType);
            item.AddMember("id", row.id, allocator);
            item.AddMember("button", row.binding.button, allocator);
            rapidjson::Value modifiers(rapidjson::kArrayType);
            for (int code = gamepad_first; code < gamepad_first + gamepad_count; ++code) {
                if (row.binding.modifiers & gamepad_bit(code)) { modifiers.PushBack(code, allocator); }
            }
            item.AddMember("modifiers", modifiers, allocator);
            list.PushBack(item, allocator);
        }
        root.AddMember(rapidjson::Value(gamepad_json_key, allocator), list, allocator);
    }
}
