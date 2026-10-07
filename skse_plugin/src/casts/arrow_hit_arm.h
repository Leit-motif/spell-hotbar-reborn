#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace SpellHotbar::casts {

	// An effect a bow Ability puts on the arrow it fires, applied to whatever that arrow hits.
	//
	// Arrow Interpreter launches the arrows and SH2 never sees the projectile, so the effect is
	// armed instead: an `SH2_Arrow*` annotation on the clip, read off its annotation track at
	// Activate like the bow string cues, arms a window when the clip reaches it, and the first hit
	// by one of the player's projectiles inside that window takes the effect. The window outlives
	// the Ability on purpose -- a long shot lands after the clip has ended.
	enum class ArrowHitEffect
	{
		none,
		// Blunt Shot: the target is knocked down, every time.
		knockdown
	};

	struct ArrowHitCue
	{
		float time{ 0.0f };
		ArrowHitEffect effect{ ArrowHitEffect::none };
	};

	struct ArrowHitArm
	{
		ArrowHitEffect effect{ ArrowHitEffect::none };
		double expires_ms{ 0.0 };
	};

	// Long enough for an arrow at the edge of useful bow range to land, short enough that a miss
	// does not hand the effect to the next ordinary shot.
	inline constexpr double kArrowHitWindowMs = 3000.0;

	[[nodiscard]] constexpr ArrowHitEffect arrow_hit_effect_for(std::string_view annotation) noexcept
	{
		if (annotation == "SH2_ArrowKnockdown") {
			return ArrowHitEffect::knockdown;
		}
		return ArrowHitEffect::none;
	}

	[[nodiscard]] constexpr ArrowHitArm arrow_hit_arm(ArrowHitEffect effect, double now_ms) noexcept
	{
		return ArrowHitArm{ .effect = effect, .expires_ms = now_ms + kArrowHitWindowMs };
	}

	// What a projectile hit at `now_ms` takes from `arm`. The caller clears the arm when this is not
	// `none`: one shot, one effect.
	[[nodiscard]] constexpr ArrowHitEffect arrow_hit_effect_at(const ArrowHitArm& arm, double now_ms) noexcept
	{
		if (arm.effect == ArrowHitEffect::none || now_ms > arm.expires_ms) {
			return ArrowHitEffect::none;
		}
		return arm.effect;
	}

	// One past the last cue due at `clip_time`, starting from `next`. Cues are sorted by time.
	[[nodiscard]] constexpr std::size_t arrow_hit_cues_due(std::span<const ArrowHitCue> cues, std::size_t next,
		float clip_time) noexcept
	{
		if (clip_time < 0.0f) {
			return next;
		}
		while (next < cues.size() && cues[next].time <= clip_time) {
			++next;
		}
		return next;
	}
}
