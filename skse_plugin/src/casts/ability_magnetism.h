#pragma once

namespace RE {
class PlayerCharacter;
}

namespace SpellHotbar::casts::AbilityMagnetism {

// Request True Directional Movement's V1 API. Call once at kDataLoaded. Missing TDM is
// fail-closed and logged once: Abilities then keep the heading they started with.
void negotiate();

// Turn the player toward TDM's locked target by at most one frame of the turn cap. Called from
// the main loop every unpaused frame, before the Ability's motion is read, so that motion follows
// the new heading. Does nothing unless an Ability is live and TDM holds a lock on a living,
// loaded actor.
void update(RE::PlayerCharacter *pc, float dt, bool ability_live);

// Turn the player straight to face TDM's locked target, if TDM holds one. Returns whether it did.
// For a Blink's landing: TDM keeps the lock through the jump but does not re-face the player, so a
// sideways Blink left them up to 38 degrees off the target (measured, issue #34).
bool face_locked_target(RE::PlayerCharacter *pc);

} // namespace SpellHotbar::casts::AbilityMagnetism
