#pragma once

namespace SpellHotbar::Input {

// A down-only ButtonEvent leaves a phantom held key, so every target down needs an up. The up
// carries a tiny hold because a zero hold reads as an instant no-op rather than a release.
// Physical Actions get their real hold from the source key; the two callers that have no source
// hold to pass -- the Papyrus castSlot bounded tap, and a release retried after the source is
// already gone -- use this value instead.
inline constexpr float kKeyboardTapReleaseHeldSecs = 0.001f;

}  // namespace SpellHotbar::Input
