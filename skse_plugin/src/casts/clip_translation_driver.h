#pragma once

namespace RE
{
	class Actor;
	class hkbClipGenerator;
	class PlayerCharacter;
}

namespace SpellHotbar::casts::ClipTranslationDriver {

	void install();

	// Called from the main loop every unpaused frame with that frame's delta. While an shtb state
	// is live, hands one frame of clip animmotion to the player's character controller as
	// velocity, so collision stops it; falls back to placing the actor if the controller does
	// not take it. No-op when no bound clip has keys.
	void apply(RE::PlayerCharacter* pc, float dt);

	// Drop the bound clip (save load / new game / shtb exit).
	void reset();

	// How far the bound SH2_Art_Clip has played, in clip seconds (the time its annotations use),
	// or a negative value when no art clip is bound. The Ability latch reads it to tell when the
	// clip has reached its last hit frame.
	[[nodiscard]] float art_clip_time();
}
