#pragma once

#include <cstdint>

#include <rapidjson/document.h>

#include "art_definition.h"

namespace SpellHotbar {

// Version-1 art overlay JSON predates damage_mult. Parsing starts from the
// overlay defaults so an older entry with no member is explicitly 1x rather
// than inheriting mutable live state.
[[nodiscard]] inline ArtPlayerOverlay
art_player_overlay_from_json(const rapidjson::Value &object) {
  ArtPlayerOverlay overlay;
  if (object.HasMember("name") && object["name"].IsString()) {
    overlay.display_name = object["name"].GetString();
  }
  if (object.HasMember("icon") && object["icon"].IsString()) {
    overlay.icon = object["icon"].GetString();
  }
  if (object.HasMember("icon_form") && object["icon_form"].IsUint()) {
    overlay.icon_form = object["icon_form"].GetUint();
  }
  if (object.HasMember("art_class") && object["art_class"].IsString()) {
    overlay.art_class = parse_art_class(object["art_class"].GetString());
  }
  if (object.HasMember("stamina_cost") && object["stamina_cost"].IsNumber()) {
    overlay.stamina_cost =
        static_cast<float>(object["stamina_cost"].GetDouble());
  }
  if (object.HasMember("magicka_cost") && object["magicka_cost"].IsNumber()) {
    overlay.magicka_cost =
        static_cast<float>(object["magicka_cost"].GetDouble());
  }
  if (object.HasMember("health_cost") && object["health_cost"].IsNumber()) {
    overlay.health_cost = static_cast<float>(object["health_cost"].GetDouble());
  }
  if (object.HasMember("cooldown") && object["cooldown"].IsString()) {
    overlay.cooldown = object["cooldown"].GetString();
  }
  if (object.HasMember("gcd") && object["gcd"].IsNumber()) {
    overlay.gcd = static_cast<float>(object["gcd"].GetDouble());
  }
  if (object.HasMember("damage_mult") && object["damage_mult"].IsNumber()) {
    overlay.damage_mult = static_cast<float>(object["damage_mult"].GetDouble());
  }
  return overlay;
}

[[nodiscard]] inline rapidjson::Value
art_player_overlay_to_json(std::uint32_t art_id,
                           const ArtPlayerOverlay &overlay,
                           rapidjson::Document::AllocatorType &allocator) {
  rapidjson::Value entry(rapidjson::kObjectType);
  entry.AddMember("art_id", art_id, allocator);
  entry.AddMember("name",
                  rapidjson::Value(overlay.display_name.c_str(), allocator),
                  allocator);
  entry.AddMember("icon", rapidjson::Value(overlay.icon.c_str(), allocator),
                  allocator);
  entry.AddMember("icon_form", overlay.icon_form, allocator);
  entry.AddMember(
      "art_class",
      rapidjson::Value(art_class_label(overlay.art_class), allocator),
      allocator);
  entry.AddMember("stamina_cost", overlay.stamina_cost, allocator);
  entry.AddMember("magicka_cost", overlay.magicka_cost, allocator);
  entry.AddMember("health_cost", overlay.health_cost, allocator);
  entry.AddMember("cooldown",
                  rapidjson::Value(overlay.cooldown.c_str(), allocator),
                  allocator);
  entry.AddMember("gcd", overlay.gcd, allocator);
  entry.AddMember("damage_mult", overlay.damage_mult, allocator);
  return entry;
}

} // namespace SpellHotbar
