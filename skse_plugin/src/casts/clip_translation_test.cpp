#include "clip_translation.h"

#include <cstdlib>
#include <iostream>
#include <numbers>
#include <vector>

using SpellHotbar::casts::ClipTranslation;
using SpellHotbar::casts::ClipTranslationKey;
using SpellHotbar::casts::interpolate_clip_translation;
using SpellHotbar::casts::local_to_world;
using SpellHotbar::casts::nearly_equal;
using SpellHotbar::casts::parse_animmotion_text;
using SpellHotbar::casts::translation_delta;
using SpellHotbar::casts::frame_delta_to_havok_velocity;
using SpellHotbar::casts::heading_toward;
using SpellHotbar::casts::step_heading;

namespace {

int g_failures = 0;

void expect(bool cond, const char* msg)
{
	if (!cond) {
		std::cerr << "FAIL: " << msg << '\n';
		++g_failures;
	}
}

void expect_xyz(ClipTranslation got, float x, float y, float z, const char* msg)
{
	expect(nearly_equal(got.x, x) && nearly_equal(got.y, y) && nearly_equal(got.z, z), msg);
}

void empty_keys_are_zero()
{
	expect_xyz(interpolate_clip_translation({}, 0.5f), 0.0f, 0.0f, 0.0f,
		"no keys means no translation");
}

void exact_key_returns_that_sample()
{
	const std::vector<ClipTranslationKey> keys{
		{ .time = 0.0167f, .x = 0.0f, .y = 4.375f, .z = 0.0f },
		{ .time = 1.0667f, .x = 0.0f, .y = -318.549f, .z = 0.0f },
	};
	expect_xyz(interpolate_clip_translation(keys, 1.0667f), 0.0f, -318.549f, 0.0f,
		"Disengage peak key is the cumulative back-leap");
}

void midway_is_linear()
{
	const std::vector<ClipTranslationKey> keys{
		{ .time = 0.0f, .x = 0.0f, .y = 0.0f, .z = 0.0f },
		{ .time = 1.0f, .x = 0.0f, .y = -100.0f, .z = 0.0f },
	};
	expect_xyz(interpolate_clip_translation(keys, 0.5f), 0.0f, -50.0f, 0.0f,
		"halfway between cumulative keys is the midpoint");
}

void past_the_last_key_clamps()
{
	const std::vector<ClipTranslationKey> keys{
		{ .time = 1.0667f, .x = 0.0f, .y = -318.549f, .z = 0.0f },
	};
	expect_xyz(interpolate_clip_translation(keys, 2.333f), 0.0f, -318.549f, 0.0f,
		"after the last key the body holds the last sample");
}

void before_the_first_key_lerps_from_origin()
{
	const std::vector<ClipTranslationKey> keys{
		{ .time = 0.2f, .x = 0.0f, .y = 10.0f, .z = 0.0f },
	};
	expect_xyz(interpolate_clip_translation(keys, 0.1f), 0.0f, 5.0f, 0.0f,
		"time before the first key interpolates from the origin");
}

void parse_reads_animmotion_and_ignores_other_text()
{
	const auto motion = parse_animmotion_text("animmotion 0.0 -318.549 0.0");
	expect(motion.has_value(), "animmotion text parses");
	expect(motion && nearly_equal(motion->y, -318.549f), "Y is the back-leap");
	expect(!parse_animmotion_text("HitFrame").has_value(), "HitFrame is not translation");
	expect(!parse_animmotion_text("animmotion").has_value(), "prefix alone is not a key");
}

void yaw_zero_keeps_local_y_as_world_y()
{
	const auto world = local_to_world(ClipTranslation{ .x = 0.0f, .y = -318.549f, .z = 0.0f }, 0.0f);
	expect_xyz(world, 0.0f, -318.549f, 0.0f, "facing north, clip -Y is world -Y");
}

void yaw_east_sends_forward_along_world_x()
{
	const float east = std::numbers::pi_v<float> / 2.0f;
	const auto world = local_to_world(ClipTranslation{ .x = 0.0f, .y = -100.0f, .z = 0.0f }, east);
	expect_xyz(world, -100.0f, 0.0f, 0.0f, "facing east, clip -Y (back) is world -X");
}

void frame_delta_is_the_step_between_samples()
{
	const auto from = ClipTranslation{ .x = 0.0f, .y = 4.375f, .z = 0.0f };
	const auto to = ClipTranslation{ .x = 0.0f, .y = 15.183f, .z = 0.0f };
	const auto d = translation_delta(from, to);
	expect_xyz(d, 0.0f, 10.808f, 0.0f, "one annotation step is the frame delta to apply");
}

void a_frame_delta_becomes_a_havok_velocity()
{
	// 10 world units in 0.1 s is 100 units/s; at Skyrim's Havok scale that is 1.42875 m/s.
	constexpr float kScale = 0.0142875f;
	const auto v = frame_delta_to_havok_velocity(ClipTranslation{ .x = 10.0f, .y = -5.0f, .z = 3.0f }, 0.1f, kScale);
	expect_xyz(v, 1.42875f, -0.714375f, 0.0f, "XY scale to Havok units per second; Z stays with gravity");
}

void a_paused_or_empty_frame_carries_no_velocity()
{
	constexpr float kScale = 0.0142875f;
	const auto delta = ClipTranslation{ .x = 10.0f, .y = 10.0f, .z = 0.0f };
	expect_xyz(frame_delta_to_havok_velocity(delta, 0.0f, kScale), 0.0f, 0.0f, 0.0f, "dt 0 is no velocity");
	expect_xyz(frame_delta_to_havok_velocity(delta, -0.016f, kScale), 0.0f, 0.0f, 0.0f, "negative dt is no velocity");
}

void heading_toward_faces_the_target_in_the_motion_convention()
{
	const float pi = std::numbers::pi_v<float>;
	expect(nearly_equal(heading_toward(0.0f, 100.0f), 0.0f), "a target due north is heading 0");
	expect(nearly_equal(heading_toward(100.0f, 0.0f), pi / 2.0f), "a target due east is a quarter turn");
	expect(nearly_equal(heading_toward(0.0f, -100.0f), pi), "a target due south is a half turn");
	expect(nearly_equal(heading_toward(-100.0f, 0.0f), 1.5f * pi), "a target due west is three quarters, never negative");
	// Facing the target, a clip's forward motion carries the player at it.
	const auto world = local_to_world(ClipTranslation{ .x = 0.0f, .y = 50.0f, .z = 0.0f }, heading_toward(30.0f, 40.0f));
	expect_xyz(world, 30.0f, 40.0f, 0.0f, "forward motion at the target's heading lands on the target");
}

void step_heading_turns_the_short_way_and_stops_at_the_cap()
{
	const float pi = std::numbers::pi_v<float>;
	const float deg = pi / 180.0f;
	expect(nearly_equal(step_heading(0.0f, 90.0f * deg, 10.0f * deg), 10.0f * deg), "a far target turns by the cap");
	expect(nearly_equal(step_heading(0.0f, 5.0f * deg, 10.0f * deg), 5.0f * deg), "a near target is reached exactly");
	expect(nearly_equal(step_heading(350.0f * deg, 10.0f * deg, 10.0f * deg), 0.0f, 0.001f) ||
			nearly_equal(step_heading(350.0f * deg, 10.0f * deg, 10.0f * deg), 2.0f * pi, 0.001f),
		"from 350 to 10 degrees the turn crosses north, not the long way round");
	expect(nearly_equal(step_heading(10.0f * deg, 350.0f * deg, 5.0f * deg), 5.0f * deg), "turning left across north also takes the short way");
	expect(nearly_equal(step_heading(1.0f, 2.0f, -1.0f), 1.0f), "a negative cap does not turn");
}

}  // namespace

int main()
{
	empty_keys_are_zero();
	exact_key_returns_that_sample();
	midway_is_linear();
	past_the_last_key_clamps();
	before_the_first_key_lerps_from_origin();
	parse_reads_animmotion_and_ignores_other_text();
	yaw_zero_keeps_local_y_as_world_y();
	yaw_east_sends_forward_along_world_x();
	frame_delta_is_the_step_between_samples();
	a_frame_delta_becomes_a_havok_velocity();
	a_paused_or_empty_frame_carries_no_velocity();
	heading_toward_faces_the_target_in_the_motion_convention();
	step_heading_turns_the_short_way_and_stops_at_the_cap();

	if (g_failures != 0) {
		std::cerr << g_failures << " failure(s)\n";
		return EXIT_FAILURE;
	}
	std::cout << "ok\n";
	return EXIT_SUCCESS;
}
