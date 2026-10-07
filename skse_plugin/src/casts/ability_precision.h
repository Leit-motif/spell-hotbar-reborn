#pragma once

namespace SpellHotbar::casts::AbilityPrecision {

// Request Precision V1 and register the Ability pre-hit callback. Call once at
// kDataLoaded. Missing or incompatible Precision is fail-closed and logged
// once; the editor slider still saves.
void negotiate();

} // namespace SpellHotbar::casts::AbilityPrecision
