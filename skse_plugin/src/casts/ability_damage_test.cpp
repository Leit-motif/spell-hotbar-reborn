#include "ability_damage.h"

#include <cstdlib>
#include <iostream>
#include <limits>

using SpellHotbar::casts::ArtDriver::ability_precision_damage_modifier;
using SpellHotbar::casts::ArtDriver::kAbilityDamageMultDefault;
using SpellHotbar::casts::ArtDriver::sanitize_ability_damage_mult;

namespace {

int g_failures = 0;

void expect(bool cond, const char *msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++g_failures;
  }
}

void inactive_emits_no_modifier() {
  const auto mod = ability_precision_damage_modifier(false, true, 3.0f);
  expect(!mod.emit, "inactive Ability does not scale Precision hits");
}

void non_player_attacker_emits_no_modifier() {
  const auto mod = ability_precision_damage_modifier(true, false, 3.0f);
  expect(!mod.emit, "NPC Precision hits are not the Ability slider");
}

void zero_is_additive_minus_one() {
  const auto mod = ability_precision_damage_modifier(true, true, 0.0f);
  expect(mod.emit, "0x still emits so authored DamageMult is cancelled");
  expect(mod.additive_value == -1.0f, "0x is Additive -1.0");
}

void identity_emits_no_modifier() {
  const auto mod = ability_precision_damage_modifier(true, true, 1.0f);
  expect(!mod.emit, "1x is identity and must not touch the hit");
}

void three_is_additive_two() {
  const auto mod = ability_precision_damage_modifier(true, true, 3.0f);
  expect(mod.emit, "3x emits a Precision modifier");
  expect(mod.additive_value == 2.0f,
         "3x is Additive 2.0 so authored DamageMult still multiplies");
}

void non_finite_falls_back_to_identity() {
  expect(
      sanitize_ability_damage_mult(std::numeric_limits<float>::quiet_NaN()) ==
          kAbilityDamageMultDefault,
      "NaN becomes 1x");
  expect(sanitize_ability_damage_mult(std::numeric_limits<float>::infinity()) ==
             kAbilityDamageMultDefault,
         "Infinity becomes 1x");
  expect(
      sanitize_ability_damage_mult(-std::numeric_limits<float>::infinity()) ==
          kAbilityDamageMultDefault,
      "negative Infinity becomes 1x");
  const auto nan_mod = ability_precision_damage_modifier(
      true, true, std::numeric_limits<float>::quiet_NaN());
  expect(!nan_mod.emit, "NaN snapshot is treated as identity");
}

void out_of_range_is_clamped() {
  expect(sanitize_ability_damage_mult(-1.0f) == 0.0f, "below 0x clamps to 0x");
  expect(sanitize_ability_damage_mult(9.0f) == 5.0f, "above 5x clamps to 5x");
  const auto high = ability_precision_damage_modifier(true, true, 9.0f);
  expect(high.emit && high.additive_value == 4.0f, "5x clamp is Additive 4.0");
}

} // namespace

int main() {
  inactive_emits_no_modifier();
  non_player_attacker_emits_no_modifier();
  zero_is_additive_minus_one();
  identity_emits_no_modifier();
  three_is_additive_two();
  non_finite_falls_back_to_identity();
  out_of_range_is_clamped();

  if (g_failures > 0) {
    std::cerr << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "ability_damage_test passed\n";
  return EXIT_SUCCESS;
}
