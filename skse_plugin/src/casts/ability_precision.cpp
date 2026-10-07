#include "ability_precision.h"
#include "../../third_party/precision/PrecisionAPI.h"
#include "../logger/logger.h"
#include "ability_damage.h"
#include "art_driver.h"

#include <mutex>

namespace SpellHotbar::casts::AbilityPrecision {

namespace {

PRECISION_API::IVPrecision1 *g_precision = nullptr;
std::once_flag g_negotiation_once;

PRECISION_API::PreHitCallbackReturn
on_pre_hit(const PRECISION_API::PrecisionHitData &hit) {
  PRECISION_API::PreHitCallbackReturn result;
  const bool attacker_is_player = hit.attacker && hit.attacker->IsPlayerRef();
  const auto mod = ArtDriver::ability_precision_damage_modifier(
      ArtDriver::is_active(), attacker_is_player,
      ArtDriver::active_damage_mult());
  if (!mod.emit) {
    return result;
  }
  result.modifiers.push_back(PRECISION_API::PreHitModifier{
      .modifierType = PRECISION_API::PreHitModifier::ModifierType::Damage,
      .modifierOperation =
          PRECISION_API::PreHitModifier::ModifierOperation::Additive,
      .modifierValue = mod.additive_value,
  });
  return result;
}

} // namespace

void negotiate() {
  std::call_once(g_negotiation_once, [] {
    g_precision = static_cast<PRECISION_API::IVPrecision1 *>(
        PRECISION_API::RequestPluginAPI(PRECISION_API::InterfaceVersion::V1));
    if (!g_precision) {
      logger::info("SpellHotbar2 Precision API: unavailable (Ability damage "
                   "slider will not scale hits)");
      return;
    }
    const auto registered = g_precision->AddPreHitCallback(
        SKSE::GetPluginHandle(), PRECISION_API::PreHitCallback{on_pre_hit});
    if (registered == PRECISION_API::APIResult::OK ||
        registered == PRECISION_API::APIResult::AlreadyRegistered) {
      logger::info("SpellHotbar2 Precision API: active (V1 pre-hit)");
      return;
    }
    g_precision = nullptr;
    logger::info("SpellHotbar2 Precision API: incompatible (Ability damage "
                 "slider will not scale hits)");
  });
}

} // namespace SpellHotbar::casts::AbilityPrecision
