#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// The Blink: a mage-art Ability that teleports the player a short way in the held direction, or
// backward when nothing is held. It plays no clip and never becomes `current_cast`, so a
// concentration channel keeps running under it (issue Leit-motif/spell-hotbar-ng#32).
//
// Movement is a dislocation, not a dash: Havok rays pick a landing, then `SetPosition` puts the
// player there. Nothing tells the behavior graph to change state and no velocity fights the root.
// The ray, landing and fall-tracker logic is adapted from izicoder/skse-blink (MIT).
namespace SpellHotbar::casts::Blink {

	// A press of a Blink Ability's slot. Charges the Ability's cost, cooldown and GCD only when
	// the player actually moved; every refusal is free and says why in the log.
	bool try_blink(std::uint32_t art_id, std::size_t slot);

	// Once per unpaused frame: shows the model again after the jump and removes old portals.
	void update();

	// A game load: puts the model back if a Blink was mid-jump, and forgets its portals.
	void reset();

	// The Blink's own GCD, which the press gate and the bar's sweep read alongside the cast's.
	[[nodiscard]] bool gcd_active();
	[[nodiscard]] float gcd_progress();
	[[nodiscard]] float gcd_duration();

	// The left stick as the input hook last saw it; the keyboard is read from the device.
	void note_left_stick(float x, float y);

	// Test seam: the movement input the next Blink reads instead of the devices, once.
	// DevBench's injected input never reaches the devices this module reads.
	void set_test_input(float x, float y);

	// Test seams: whether the Blink's iframes have the player a ghost now, and a mesh placed
	// `forward` units ahead of the camera for four seconds, to judge an effect without a Blink.
	[[nodiscard]] bool is_ghost();
	void place_test_model(const std::string& model, float forward);
}
