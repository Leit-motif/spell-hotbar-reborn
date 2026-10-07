#include "ability_magnetism.h"
#include "../../third_party/tdm/TrueDirectionalMovementAPI.h"
#include "../logger/logger.h"
#include "clip_translation.h"

#include <mutex>

namespace SpellHotbar::casts::AbilityMagnetism {

namespace {

// 360 degrees a second. SH2 holds the player's heading for the length of an Ability and TDM's
// lock does not turn the player through it, so without this a Knockback shove that moves the
// target off the opening line sends every later hit past it (measured: heading 81.9 degrees from
// cast to exit under lock).
constexpr float kTurnRateRadiansPerSecond = 6.28318530718f;

TDM_API::IVTDM1 *g_tdm = nullptr;
std::once_flag g_negotiation_once;
RE::FormID g_engaged_target = 0;

[[nodiscard]] RE::NiPointer<RE::Actor> locked_target() {
  if (!g_tdm || !g_tdm->GetTargetLockState()) {
    return {};
  }
  auto target = g_tdm->GetCurrentTarget().get();
  const auto *actor = target.get();
  if (!actor || actor->IsPlayerRef() || actor->IsDead() || actor->IsDisabled() ||
      !actor->Is3DLoaded()) {
    return {};
  }
  return target;
}

} // namespace

void negotiate() {
  std::call_once(g_negotiation_once, [] {
    g_tdm = static_cast<TDM_API::IVTDM1 *>(
        TDM_API::RequestPluginAPI(TDM_API::InterfaceVersion::V1));
    if (g_tdm) {
      logger::info("SpellHotbar2 TDM API: active (V1) -- Abilities turn toward "
                   "the lock-on target");
    } else {
      logger::info("SpellHotbar2 TDM API: unavailable -- Abilities keep their "
                   "starting heading");
    }
  });
}

void update(RE::PlayerCharacter *pc, float dt, bool ability_live) {
  const auto target = ability_live && pc ? locked_target() : RE::NiPointer<RE::Actor>{};
  if (!target) {
    g_engaged_target = 0;
    return;
  }
  const auto from = pc->GetPosition();
  const auto to = target->GetPosition();
  const float current = pc->GetAngleZ();
  const float next = step_heading(current, heading_toward(to.x - from.x, to.y - from.y),
                                  kTurnRateRadiansPerSecond * dt);
  if (next != current) {
    pc->SetRotationZ(next);
  }
  if (target->GetFormID() != g_engaged_target) {
    g_engaged_target = target->GetFormID();
    logger::info("SH2 magnetism: turning toward {:08X} (TDM lock)", g_engaged_target);
  }
}

} // namespace SpellHotbar::casts::AbilityMagnetism
