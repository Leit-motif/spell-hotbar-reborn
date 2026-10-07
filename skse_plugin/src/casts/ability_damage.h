#pragma once

#include <cmath>

namespace SpellHotbar::casts::ArtDriver {

inline constexpr float kAbilityDamageMultMin = 0.0f;
inline constexpr float kAbilityDamageMultMax = 5.0f;
inline constexpr float kAbilityDamageMultDefault = 1.0f;

// Clamp a player-tuned Ability multiplier into 0x..5x. Non-finite values fall
// back to identity so a corrupt sidecar or overlay cannot poison Precision's
// hit math.
[[nodiscard]] inline float sanitize_ability_damage_mult(float value) noexcept {
  if (!std::isfinite(value)) {
    return kAbilityDamageMultDefault;
  }
  if (value < kAbilityDamageMultMin) {
    return kAbilityDamageMultMin;
  }
  if (value > kAbilityDamageMultMax) {
    return kAbilityDamageMultMax;
  }
  return value;
}

struct AbilityPrecisionDamageModifier {
  bool emit{false};
  float additive_value{0.0f};
};

// Precision 2.0.4 sums deviations from 1.0 (authored DamageMult included). An
// Additive modifier of (slider - 1) therefore yields slider * authored
// DamageMult. Identity is omitted so a 1.0 Ability does not touch the hit.
// Temporally gated: player Precision hits while the SH2 Ability state is
// active, not a named clip collision.
[[nodiscard]] inline AbilityPrecisionDamageModifier
ability_precision_damage_modifier(bool art_active, bool attacker_is_player,
                                  float damage_mult) noexcept {
  if (!art_active || !attacker_is_player) {
    return {};
  }
  const float sanitized = sanitize_ability_damage_mult(damage_mult);
  if (sanitized == kAbilityDamageMultDefault) {
    return {};
  }
  return AbilityPrecisionDamageModifier{.emit = true,
                                        .additive_value = sanitized - 1.0f};
}

} // namespace SpellHotbar::casts::ArtDriver
