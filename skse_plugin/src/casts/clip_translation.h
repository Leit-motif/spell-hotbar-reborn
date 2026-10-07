#pragma once

#include <charconv>
#include <cmath>
#include <optional>
#include <span>
#include <string_view>

namespace SpellHotbar::casts {

// One animmotion key: cumulative actor-local translation at a clip time.
// Havok Y is character-forward; X is right; Z is up. Units match Skyrim world
// units (the Disengage dump peaks near -318 on Y, ~3 m back).
struct ClipTranslationKey {
	float time = 0.0f;
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

struct ClipTranslation {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

[[nodiscard]] inline bool nearly_equal(float a, float b, float eps = 0.001f) noexcept
{
	return std::fabs(a - b) <= eps;
}

// Parse "animmotion x y z". Anything else is ignored (HitFrame, PIE, …).
[[nodiscard]] inline std::optional<ClipTranslation> parse_animmotion_text(
	std::string_view text) noexcept
{
	constexpr std::string_view kPrefix = "animmotion ";
	if (!text.starts_with(kPrefix)) {
		return std::nullopt;
	}
	text.remove_prefix(kPrefix.size());

	ClipTranslation out{};
	float* slots[3] = { &out.x, &out.y, &out.z };
	for (float* slot : slots) {
		while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
			text.remove_prefix(1);
		}
		if (text.empty()) {
			return std::nullopt;
		}
		float value = 0.0f;
		const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
		if (parsed.ec != std::errc{}) {
			return std::nullopt;
		}
		*slot = value;
		text.remove_prefix(static_cast<std::size_t>(parsed.ptr - text.data()));
	}
	return out;
}

// AMR's segment interpolation: keys are cumulative, t is clamped to the last key.
[[nodiscard]] inline ClipTranslation interpolate_clip_translation(
	std::span<const ClipTranslationKey> keys, float time) noexcept
{
	if (keys.empty()) {
		return {};
	}
	const float end = keys.back().time;
	const float t = time > end ? end : time;

	for (std::size_t i = 0; i < keys.size(); ++i) {
		if (t > keys[i].time) {
			continue;
		}
		const ClipTranslationKey& cur = keys[i];
		const float prevTime = i == 0 ? 0.0f : keys[i - 1].time;
		const ClipTranslation prev = i == 0
			? ClipTranslation{}
			: ClipTranslation{ keys[i - 1].x, keys[i - 1].y, keys[i - 1].z };
		const float duration = cur.time - prevTime;
		float progress = 1.0f;
		if (duration > 1.0e-6f) {
			progress = (t - prevTime) / duration;
		}
		const float inv = 1.0f - progress;
		return ClipTranslation{
			cur.x * progress + prev.x * inv,
			cur.y * progress + prev.y * inv,
			cur.z * progress + prev.z * inv,
		};
	}
	const auto& last = keys.back();
	return ClipTranslation{ last.x, last.y, last.z };
}

// Rotate a character-local XY delta into world XY. yaw=0 faces world +Y.
[[nodiscard]] inline ClipTranslation local_to_world(
	ClipTranslation local, float yawRadians) noexcept
{
	const float c = std::cos(yawRadians);
	const float s = std::sin(yawRadians);
	return ClipTranslation{
		local.x * c + local.y * s,
		-local.x * s + local.y * c,
		local.z,
	};
}

[[nodiscard]] inline ClipTranslation translation_delta(
	ClipTranslation from, ClipTranslation to) noexcept
{
	return ClipTranslation{ to.x - from.x, to.y - from.y, to.z - from.z };
}

// The heading that faces a point `dx`, `dy` world units away, in the convention `local_to_world`
// uses: 0 faces +Y, and angles grow clockwise toward +X. Returned in [0, 2*pi).
[[nodiscard]] inline float heading_toward(float dx, float dy) noexcept
{
	constexpr float kTwoPi = 6.28318530718f;
	const float a = std::atan2(dx, dy);
	return a < 0.0f ? a + kTwoPi : a;
}

// One frame of a capped turn from `current` toward `desired` (radians), taking the short way
// round. The result is in [0, 2*pi).
[[nodiscard]] inline float step_heading(float current, float desired, float maxStep) noexcept
{
	constexpr float kPi = 3.14159265359f;
	constexpr float kTwoPi = 6.28318530718f;
	float delta = std::fmod(desired - current, kTwoPi);
	if (delta > kPi) {
		delta -= kTwoPi;
	} else if (delta < -kPi) {
		delta += kTwoPi;
	}
	const float step = maxStep < 0.0f ? 0.0f : maxStep;
	delta = delta > step ? step : (delta < -step ? -step : delta);
	float out = std::fmod(current + delta, kTwoPi);
	return out < 0.0f ? out + kTwoPi : out;
}

// One frame's world XY delta as a velocity the character controller carries, in Havok units per
// second. Moving through the controller rather than placing the actor is what lets walls and
// other actors stop an Ability's motion. Z is left to the controller, so gravity and slopes hold.
[[nodiscard]] inline ClipTranslation frame_delta_to_havok_velocity(
	ClipTranslation worldDelta, float dt, float worldScale) noexcept
{
	if (!(dt > 0.0f)) {
		return {};
	}
	const float k = worldScale / dt;
	return ClipTranslation{ worldDelta.x * k, worldDelta.y * k, 0.0f };
}

}  // namespace SpellHotbar::casts
