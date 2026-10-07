#include "art_overlay_json.h"

#include <cstdlib>
#include <iostream>

#include <rapidjson/document.h>

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++g_failures;
  }
}

rapidjson::Document parse(const char *json) {
  rapidjson::Document document;
  document.Parse(json);
  if (document.HasParseError() || !document.IsObject()) {
    std::cerr << "FAIL: invalid JSON fixture\n";
    ++g_failures;
  }
  return document;
}

void legacy_entry_without_damage_defaults_to_identity() {
  const auto document = parse(R"({"art_id":17,"name":"Legacy","gcd":1.5})");
  const auto overlay = SpellHotbar::art_player_overlay_from_json(document);
  expect(overlay.damage_mult == 1.0f, "missing damage_mult loads as 1x");
}

void overlay_json_round_trip_keeps_damage_multiplier() {
  SpellHotbar::ArtPlayerOverlay expected;
  expected.display_name = "Tuned Ability";
  expected.icon = "GREATER_POWER";
  expected.icon_form = 0x1234;
  expected.art_class = SpellHotbar::ArtClass::TwoHand;
  expected.stamina_cost = 12.0f;
  expected.magicka_cost = 3.0f;
  expected.health_cost = 1.0f;
  expected.cooldown = "8s";
  expected.gcd = 1.25f;
  expected.damage_mult = 3.0f;

  rapidjson::Document document;
  document.SetObject();
  auto encoded = SpellHotbar::art_player_overlay_to_json(
      17, expected, document.GetAllocator());
  expect(encoded["art_id"].GetUint() == 17, "round trip keeps the art id");
  const auto actual = SpellHotbar::art_player_overlay_from_json(encoded);
  expect(actual.display_name == expected.display_name &&
             actual.icon == expected.icon,
         "round trip keeps labels");
  expect(actual.icon_form == expected.icon_form &&
             actual.art_class == expected.art_class,
         "round trip keeps icon form and class");
  expect(actual.stamina_cost == expected.stamina_cost &&
             actual.magicka_cost == expected.magicka_cost &&
             actual.health_cost == expected.health_cost,
         "round trip keeps costs");
  expect(actual.cooldown == expected.cooldown && actual.gcd == expected.gcd,
         "round trip keeps cooldown tuning");
  expect(actual.damage_mult == expected.damage_mult,
         "round trip keeps damage multiplier");
}

} // namespace

int main() {
  legacy_entry_without_damage_defaults_to_identity();
  overlay_json_round_trip_keeps_damage_multiplier();
  if (g_failures != 0) {
    return EXIT_FAILURE;
  }
  std::cout << "art_overlay_json_test passed\n";
  return EXIT_SUCCESS;
}
