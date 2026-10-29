#include "blink_rules.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using SpellHotbar::casts::blink_bypasses_cast_gate;
using SpellHotbar::casts::BlinkGcd;
using SpellHotbar::casts::BlinkHeading;
using SpellHotbar::casts::resolve_blink_heading;

namespace {

int g_failures = 0;
constexpr float kPi = 3.14159265f;

void expect(bool cond, const char* msg)
{
	if (!cond) {
		std::cerr << "FAIL: " << msg << '\n';
		++g_failures;
	}
}

bool near(float a, float b)
{
	return std::abs(a - b) < 1e-4f;
}

bool heading_is(BlinkHeading h, float x, float y)
{
	return near(h.x, x) && near(h.y, y);
}

// The angle between a heading and the way the character faces, folded into [0, pi].
float angle_from_facing(BlinkHeading h, float facing_yaw)
{
	const float fx = std::sin(facing_yaw);
	const float fy = std::cos(facing_yaw);
	return std::acos(std::fmax(-1.0f, std::fmin(1.0f, h.x * fx + h.y * fy)));
}

void forward_follows_the_camera()
{
	expect(heading_is(resolve_blink_heading(0.0f, 1.0f, 0.0f, 0.0f, 0.5f), 0.0f, 1.0f),
		"W with the camera at yaw 0 goes world +Y");
	expect(heading_is(resolve_blink_heading(0.0f, 1.0f, kPi / 2.0f, 0.0f, 0.5f), 1.0f, 0.0f),
		"W with the camera at yaw 90 goes world +X, whatever the character faces");
	expect(resolve_blink_heading(0.0f, 1.0f, 0.0f, 0.0f, 0.5f).from_input, "a held key is input");
}

void strafe_and_back_rotate_with_the_camera()
{
	expect(heading_is(resolve_blink_heading(1.0f, 0.0f, 0.0f, 0.0f, 0.5f), 1.0f, 0.0f), "D goes right of the camera");
	expect(heading_is(resolve_blink_heading(-1.0f, 0.0f, 0.0f, 0.0f, 0.5f), -1.0f, 0.0f), "A goes left of the camera");
	expect(heading_is(resolve_blink_heading(0.0f, -1.0f, kPi / 2.0f, 0.0f, 0.5f), -1.0f, 0.0f),
		"S goes behind the camera");
}

void diagonals_and_partial_sticks_are_unit_length()
{
	const auto diagonal = resolve_blink_heading(1.0f, 1.0f, 0.0f, 0.0f, 0.5f);
	expect(heading_is(diagonal, std::sqrt(0.5f), std::sqrt(0.5f)), "W+D goes diagonally, full distance");
	const auto half_stick = resolve_blink_heading(0.0f, 0.4f, 0.0f, 0.0f, 0.5f);
	expect(heading_is(half_stick, 0.0f, 1.0f), "a half-pushed stick still blinks the full way");
}

void nothing_held_goes_into_the_back_cone()
{
	for (const float facing : { 0.0f, 1.0f, kPi, 4.5f }) {
		for (const float r : { 0.0f, 0.25f, 0.5f, 0.75f, 0.9999f }) {
			const auto h = resolve_blink_heading(0.0f, 0.0f, 2.0f, facing, r);
			expect(!h.from_input, "no input is the back cone");
			expect(near(std::hypot(h.x, h.y), 1.0f), "the back cone heading is unit length");
			expect(angle_from_facing(h, facing) >= 3.0f * kPi / 4.0f - 1e-3f,
				"the back cone stays inside 45 degrees of straight behind");
		}
	}
	expect(heading_is(resolve_blink_heading(0.0f, 0.0f, 0.0f, 0.0f, 0.5f), 0.0f, -1.0f),
		"the middle of the cone is straight behind the character, not the camera");
}

void stick_drift_counts_as_nothing_held()
{
	expect(!resolve_blink_heading(0.1f, 0.05f, 0.0f, 0.0f, 0.5f).from_input, "drift under the deadzone is no input");
}

void only_a_blink_skips_the_cast_gate()
{
	expect(blink_bypasses_cast_gate(true, true), "a Blink slot goes around the one-cast gate");
	expect(!blink_bypasses_cast_gate(true, false), "a Clip Ability still waits for the gate");
	expect(!blink_bypasses_cast_gate(false, false), "spells, shouts and potions still wait for the gate");
}

void gcd_runs_out_and_reports_progress()
{
	BlinkGcd gcd;
	expect(!gcd.active(0.0), "no Blink yet, no GCD");
	gcd.start(10.0, 0.5f);
	expect(gcd.active(10.0) && gcd.active(10.49), "the GCD holds for its duration");
	expect(near(gcd.progress(10.25), 0.5f), "progress is the elapsed share");
	expect(!gcd.active(10.5) && gcd.progress(10.6) == 0.0f, "the GCD ends");
	gcd.start(20.0, 0.0f);
	expect(!gcd.active(20.0), "a zero GCD never holds");
}

}  // namespace

int main()
{
	forward_follows_the_camera();
	strafe_and_back_rotate_with_the_camera();
	diagonals_and_partial_sticks_are_unit_length();
	nothing_held_goes_into_the_back_cone();
	stick_drift_counts_as_nothing_held();
	only_a_blink_skips_the_cast_gate();
	gcd_runs_out_and_reports_progress();
	if (g_failures != 0) {
		std::cerr << g_failures << " failure(s)\n";
		return EXIT_FAILURE;
	}
	std::cout << "ok\n";
	return EXIT_SUCCESS;
}
