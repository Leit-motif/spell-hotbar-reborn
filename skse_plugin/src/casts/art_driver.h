#pragma once

#include "art_deadline.h"
#include "ability_start.h"
#include "arrow_hit_arm.h"
#include "bow_string_relay.h"

#include <vector>

namespace SpellHotbar::casts::ArtDriver {

	// Enter SH2_Art_State. True only when SH2_ArtStart was consumed AND SH2_Art_Clip
	// activated during that notify. False means no listener (sheathed, mid-swing, missing
	// patch) or the graph accepted ArtStart without picking a new clip — chaining onto a
	// leftover Ability.
	bool begin(RE::PlayerCharacter* pc, float damage_mult = 1.0f);

	void observe_graph_event(RE::Actor* a_player, const RE::BSFixedString& a_tag);

	// True while SH2_Art_State is live — the state, not the casting instance; same
	// rule as MscoCastDriver::is_active(). Rooting is not read off this: commitment
	// is a property of the behavior state, its own bAnimationDriven modifier in the graph.
	bool is_active();

	// Gameplay-ms stamp of the live art's start or of its last latch annotation, whichever is
	// later; 0 when nothing is running. The clock `art_deadline_passed` measures silence from.
	[[nodiscard]] double last_signal_stamp_ms();

	// Snapshotted Ability damage multiplier while the state is active. Identity is 1.0.
	// Published before is_active() goes true (release/acquire); the Precision callback
	// reads this, never the live ArtDefinition.
	[[nodiscard]] float active_damage_mult();

	// True while the SH2_Art_Clip generator is Active, including leftover after the
	// driver has already gone idle on SH2_ArtExit. ArtStart into that generator seeks
	// leftover to the start; hold until Deactivate instead.
	bool clip_is_active();

	void observe_clip_activate();
	void observe_clip_deactivate();

	// Classify the live clip's latch from its annotations (WinOpen else HitFrame
	// else SH2_ArtExit). Called when SH2_Art_Clip activates. `animation` is the
	// Havok animation pointer: a leftover clip re-Activating the previous art's
	// bytes is ignored. `hit_frame_count` is how many HitFrame annotations the
	// clip carries and `last_hit_time` the clip time of the last one: the HitFrame
	// latch opens on the LAST of them, so a multi-hit art is not cancellable
	// through its own combo. `bow_cues` are the clip's BowDraw / BowRelease
	// annotation times, which `poll_clip_cues` relays to the equipped bow; `arrow_hit_cues` are its
	// `SH2_Arrow*` annotations, which arm an effect for the arrow that shot fires.
	void bind_latch(bool has_win_open, int hit_frame_count, float last_hit_time,
		const void* animation = nullptr, std::vector<BowStringCue> bow_cues = {},
		std::vector<ArrowHitCue> arrow_hit_cues = {});

	// Act on every cue the live Ability clip has reached: relay BowDraw / BowRelease to the
	// equipped bow's graph (bow_string_relay.h) and arm arrow-hit effects (arrow_hit_arm.h). Call
	// every unpaused frame on the main thread.
	void poll_clip_cues(RE::PlayerCharacter* pc);

	// The effect armed for a hit by one of the player's projectiles right now, consumed by this
	// call: one shot, one effect. `none` outside a window.
	[[nodiscard]] ArrowHitEffect take_arrow_hit_effect();

	bool latch_open();

	bool should_trace_graph_events();

	void cancel(RE::PlayerCharacter* pc);

	// At the Ability latch, notify SH2_ArtExit so leftover can Deactivate. No-op
	// after the clip's own exit: that event is not consumed a second time.
	void try_teardown_leftover_at_latch(RE::PlayerCharacter* pc);

	void finish(RE::PlayerCharacter* pc);

	// Cancel a live art that has outstayed `art_deadline_ms`. Call every unpaused frame, and NOT
	// from the casting instance -- the instance is retired while the clip is still playing,
	// which is
	// exactly the case this exists to survive.
	void poll_deadline(RE::PlayerCharacter* pc);

	// Drop in-memory art liveness without touching the graph (save load / new game).
	void reset_session();
}
