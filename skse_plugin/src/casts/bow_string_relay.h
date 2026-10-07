#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace SpellHotbar::casts {

	// The equipped bow flexes its string from its own behavior graph (Weapons\Bow\BowBehavior:
	// Idle --BowDraw--> Draw --BowDrawOut--> IdleDrawn --BowRelease--> Release --BowReleaseOut-->
	// Idle). Vanilla drives it from the ENTER-NOTIFY events of the character's `BowDraw` and
	// `Bow_Release` states; its draw and release clips carry no BowDraw/BowRelease annotation at all.
	//
	// A bow Ability plays one clip inside SH2_Art_State and never enters those states. Its clip's
	// own `BowDraw` / `BowRelease` annotations do not reliably reach anything: measured live,
	// the player's animation-event sink saw only a `BowDraw` that was the clip's FIRST annotation at
	// 0.000 s (Rapid and Scatter Shot), never Arcane or Blunt Shot's (a SoundPlay precedes it), and
	// never any `BowRelease`. The bow played no `..\Bow\Animations\*` clip at all for Arcane and
	// Blunt, and Rapid Shot's string stayed pulled until the next ordinary shot.
	//
	// A `NotifyAnimationGraph("BowDraw")` on the player does reach the bow -- both the first- and
	// third-person bows played Bow_DrawLight, then Bow_Release on "BowRelease" -- and no character
	// graph (1hm_behavior, 0_master, mt_behavior) has a transition on either event, so the relay
	// moves the string and nothing else. The relay reads the cue times off the clip's own
	// annotation track at Activate and fires each when the clip's local time reaches it, the same
	// clock the hit-frame latch uses, so it does not depend on the annotation events arriving.
	enum class BowStringEvent
	{
		none,
		draw,
		release
	};

	struct BowStringCue
	{
		float time{ 0.0f };
		BowStringEvent event{ BowStringEvent::none };
	};

	// The cue an annotation names, exact and case-sensitive like the bow graph's own event names.
	[[nodiscard]] constexpr BowStringEvent bow_string_cue_for(std::string_view annotation) noexcept
	{
		if (annotation == "BowDraw") {
			return BowStringEvent::draw;
		}
		if (annotation == "BowRelease") {
			return BowStringEvent::release;
		}
		return BowStringEvent::none;
	}

	// Edge-triggered on the string's state: a draw only from slack, a release only from pulled, so
	// a duplicate cue or a notify the graph echoes back never restarts the bow's clip.
	[[nodiscard]] constexpr BowStringEvent bow_string_relay_for(BowStringEvent cue, bool drawn) noexcept
	{
		if (cue == BowStringEvent::draw && !drawn) {
			return BowStringEvent::draw;
		}
		if (cue == BowStringEvent::release && drawn) {
			return BowStringEvent::release;
		}
		return BowStringEvent::none;
	}

	[[nodiscard]] constexpr std::string_view bow_string_notify(BowStringEvent event) noexcept
	{
		switch (event) {
		case BowStringEvent::draw:
			return "BowDraw";
		case BowStringEvent::release:
			return "BowRelease";
		case BowStringEvent::none:
			break;
		}
		return {};
	}

	// Whether the string is left pulled after relaying `event`.
	[[nodiscard]] constexpr bool bow_string_drawn_after(bool drawn, BowStringEvent event) noexcept
	{
		switch (event) {
		case BowStringEvent::draw:
			return true;
		case BowStringEvent::release:
			return false;
		case BowStringEvent::none:
			break;
		}
		return drawn;
	}

	// One past the last cue due at `clip_time`, starting from `next`. Cues are sorted by time. A
	// negative clip time (no Ability clip bound) fires nothing.
	[[nodiscard]] constexpr std::size_t bow_string_cues_due(std::span<const BowStringCue> cues, std::size_t next,
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

	// An Ability that ends with the string still pulled -- cancelled mid-draw, staggered, a clip
	// that never releases -- sends `bowReset`, the bow graph's wildcard back to Idle. Without it the
	// bow stays in IdleDrawn and swallows the next ordinary draw's BowDraw.
	[[nodiscard]] constexpr bool bow_string_needs_reset_on_exit(bool drawn) noexcept
	{
		return drawn;
	}
}
