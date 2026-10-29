#pragma once

// The engine-free half of the Blink (casts/blink.h): where a press points, and whether it goes
// around the one-cast gate. Header-only so blink_rules_test can pin it without the game.

#include <cmath>

namespace SpellHotbar::casts {

struct BlinkHeading {
	float x{ 0.0f };  // world X, unit length with `y`
	float y{ 0.0f };
	bool from_input{ false };  // false: nothing was held, so this is the backward cone
};

// Held input shorter than this is no direction; a resting stick drifts below it.
inline constexpr float kBlinkInputDeadzone = 0.15f;

// Half of the 90-degree cone behind the player that a no-direction Blink lands in.
inline constexpr float kBlinkBackConeHalfRadians = 0.78539816f;

// Turn the held movement input into a world direction. `move_x` is strafe (+ right) and `move_y`
// is forward, in the frame of `input_yaw` -- the camera, which is what the movement keys move
// the character relative to. Yaw follows the engine's heading: 0 faces world +Y, growing
// clockwise toward +X. With nothing held the Blink goes backward from `facing_yaw`, the way the
// character faces, at a random angle inside the 90-degree cone behind it; `random01` in [0, 1)
// picks the angle.
[[nodiscard]] inline BlinkHeading resolve_blink_heading(float move_x, float move_y, float input_yaw,
	float facing_yaw, float random01) noexcept
{
	const float held = std::sqrt(move_x * move_x + move_y * move_y);
	if (held >= kBlinkInputDeadzone) {
		const float c = std::cos(input_yaw);
		const float s = std::sin(input_yaw);
		// forward = (sin, cos), right = (cos, -sin)
		return BlinkHeading{ (move_y * s + move_x * c) / held, (move_y * c - move_x * s) / held, true };
	}
	constexpr float kPi = 3.14159265f;
	const float yaw = facing_yaw + kPi + (random01 * 2.0f - 1.0f) * kBlinkBackConeHalfRadians;
	return BlinkHeading{ std::sin(yaw), std::cos(yaw), false };
}

// A Blink press never touches `current_cast`, so it is the one slot press that skips the one-cast
// gate: a channel stays live under it. Every other press, Clip Abilities included, still passes
// that gate.
[[nodiscard]] constexpr bool blink_bypasses_cast_gate(bool is_ability_slot, bool is_blink) noexcept
{
	return is_ability_slot && is_blink;
}

// The Blink's GCD. A Blink holds no `current_cast`, so the clock every other press reads cannot
// carry it; this one does, in seconds on any monotonic clock the caller picks.
struct BlinkGcd {
	double started{ 0.0 };
	float duration{ 0.0f };

	void start(double now, float seconds) noexcept
	{
		started = now;
		duration = seconds;
	}

	[[nodiscard]] bool active(double now) const noexcept
	{
		return duration > 0.0f && now - started < static_cast<double>(duration);
	}

	// Elapsed share of the GCD, the convention `get_current_gcd_progress` uses: 0 just pressed,
	// 1 (and 0 once over) when it has run out.
	[[nodiscard]] float progress(double now) const noexcept
	{
		if (!active(now)) {
			return 0.0f;
		}
		return static_cast<float>((now - started) / static_cast<double>(duration));
	}
};

}  // namespace SpellHotbar::casts
