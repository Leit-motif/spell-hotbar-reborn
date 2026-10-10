#pragma once
#include "../bar/hotbar.h"
#include "combo_cache.h"

namespace SpellHotbar::casts::MscoCastDriver {

	/**
	 * Enter the shtb cast state. The event is SH2_CastRight for combo step 1 and
	 * SH2_Cast2/3/4 for the later clips (MSCO_left2/3/4.hkx). Returns the notify
	 * result — false means no active listener (no hosting drawn idle: sheathed, or
	 * mid-swing in AttackState) and the caller tears the cast down.
	 *
	 * The BOUND clips raise a LEFT-hand SpellFire (OAR Base-default variants:
	 * 0.48s / 0.30s / 0.35s / 0.92s), but OAR selects per-hand replacements over
	 * these paths and arming follows the RESOLVED hand — `arm_spellfire` masks left,
	 * right, or both for dual.
	 *
	 * `charge_time` is written to `MSCO_attackspeed` before the notify so the clip
	 * plays at MSCO's charge-scaled pace. Rooting during the state is not the DLL's: the
	 * cast states are exclusive full-body states, so entering one leaves the locomotion
	 * subtree. They bind bAllowRotation for target tracking and write no bAnimationDriven.
	 *
	 * `shape` picks the entry and what the state is for. A fire-and-forget press enters
	 * the clip set above. A channel enters SH2_CastChannel instead, a state of its own
	 * holding a MODE_LOOPING clip on the shout-inhale path, and stays there for the whole
	 * hold until end_channel sends SH2_CastExit. It walks no clip index and opens no
	 * follow-up window; OAR still picks the per-family clip from the animation-type
	 * global, exactly as it does for the throw set.
	 */
	bool begin(RE::PlayerCharacter* pc, hand_mode hand, float charge_time, CastShape shape);

	/**
	 * The first-person cast runs on VANILLA'S shout graph, not on shtb.
	 *
	 * In first person the player runs two graphs and the first-person one is active; the shtb
	 * state lives only in the third-person behaviours, so an entry there plays a clip whose
	 * annotations never reach the sink, and the cast would wait on a SpellFire that cannot
	 * come. begin() reads the camera and, in first person, takes upstream Spell Hotbar 2's
	 * own path instead: ShoutStart in, MT_BreathExhaleShort at release, ShoutStop out,
	 * liveness from IsShouting, delivery on the authored cast timer. No clip speed and no swing
	 * takeover -- there is no MCO in first person to keep. begin() still attempts its combo
	 * sample, because it reads the camera only after sampling.
	 *
	 * vanilla_voice_path() is true while the live cast is on that path. voice_path_live() is the
	 * liveness read for it (always true on the shtb path, whose liveness is the flag).
	 * voice_release() sends the exhale; voice_reloop() re-sends the inhale for a held channel,
	 * which is how upstream sustained a concentration cast.
	 */
	bool vanilla_voice_path();
	bool voice_path_live(RE::PlayerCharacter* pc);

	/**
	 * True when the live cast began on the CLIP path (third person) and the camera has since
	 * gone to first, which is exactly when `MLh_SpellFire_Event` can no longer reach the event
	 * sink. See `clip_annotations_unreachable` in combo_cache.h for why that costs the player
	 * 8 seconds if nothing acts on it.
	 *
	 * Consult it only while a payload is actually owed. It is a live camera read, and outside
	 * that window `classify_armed_delivery`'s own exits already cover the cast.
	 */
	bool camera_left_clip_graph();
	void voice_release(RE::PlayerCharacter* pc);
	void voice_reloop(RE::PlayerCharacter* pc);

	/**
	 * Load MSCO.ini's charge-to-speed curve. Called at DataLoaded and again at each
	 * begin() so a saved MSCO.ini is picked up without a restart. Missing file keeps
	 * the shipped exponential defaults. Does not read MSCO.dll or Menu Framework.
	 */
	void load_charge_curve();

	/**
	 * Is the current Driver Cast inside its SpellFire-to-WinClose combo window? A
	 * follow-up hotbar press chains only while this is true.
	 */
	bool combo_window_open();

	/**
	 * Observe the player's animation-event stream. Ends the state on SH2_CastExit,
	 * records MCO combo position at attack-time events, and writes that position
	 * back once after a Driver Cast's ready-state reset (the one write SH2 makes to MCO's
	 * graph state: combo position across a cast it started itself).
	 *
	 * `a_payload` is the event's own payload member, and it is not optional decoration:
	 * a clip annotation `PIE.@SGVI|MCO_nextattack|3` reaches the sink split at the first
	 * `.` into the event name (raised as `Pie`, not the annotation's `PIE`) and the
	 * payload `@SGVI|MCO_nextattack|3`, so MCO's combo advance is carried there rather
	 * than in the tag. Both fields are run through the same SGVI parser: a pack that writes
	 * the annotation without a `.` puts it in the tag instead, and neither form should be
	 * the one that works.
	 *
	 * It also tracks which swing is open (opened at MCO_AttackInitiate /
	 * MCO_PowerAttackInitiate for real swings, closed at attackStop / MCO_EndAnimation or
	 * when the initiate belongs to our own borrowed clip), so a cast that interrupts a
	 * swing can tell a pre-advance sample -- which would replay the interrupted swing --
	 * from the successor it is supposed to hand on.
	 *
	 * `a_spellfire_hand` and `a_armed_mask` are the hook's single decode of the event and the
	 * arming snapshot it read for it. The graph-side commitment point -- combo window, clip
	 * committed, cast-index advance -- fires on an ARMED hand's SpellFire only, so the caller
	 * supplies both rather than this function decoding the tag a second time.
	 */
	void observe_graph_event(RE::Actor* a_player, const RE::BSFixedString& a_tag,
		const RE::BSFixedString& a_payload, SpellFireHand a_spellfire_hand,
		std::uint8_t a_armed_mask);

	/**
	 * Is the shtb cast state live right now? Raised from the entry notify's own return and
	 * cleared on the state ending, so a dropped event cannot leak into the next cast.
	 */
	bool is_active();

	/**
	 * The send-side seam forwarded an attack and the graph accepted it. Opens the swing tracker
	 * at the index the graph holds for that kind, exactly as the MCO initiate branch of
	 * observe_graph_event does -- which BFCO's graph never reaches, so this is the only edge
	 * that sees a BFCO swing start. A kind that is not an attack opens nothing. Called before
	 * the graph plays the swing, on whatever thread sent the notify.
	 */
	void observe_attack_sent(RE::Actor* a_player, AttackSeamKind a_kind);

	/**
	 * Is someone else's swing up, as far as the tracker knows? True from the seam (or the MCO
	 * initiate) to attackStop / MCO_EndAnimation. The local cast-intent latch waits on this.
	 */
	bool swing_open();

	/**
	 * Is the open swing inside its cancel window? True between the swing's `MCO_WinOpen` /
	 * `MCO_PowerWinOpen` and its first window-close annotation -- the span MCO's own graph let
	 * a cast in at, and the edge the local cast-intent latch releases a held press on. False
	 * whenever no swing is open.
	 */
	bool swing_window_open();

	/**
	 * A cast is being cut by an attack, so its teardown is about to raise an `attackStop` of its
	 * own. Arm one pass: the next `attackStop` / `MCO_EndAnimation` inside `kTeardownStopCapMs`
	 * leaves the swing tracker OPEN instead of closing it, so a swing that cut a cast stays
	 * tracked for its whole clip and its advance can still be learned.
	 *
	 * Called from the cut itself, which runs before the attack is forwarded and therefore before
	 * `observe_attack_sent` opens the swing. That order is why the flag is spent by a cap rather
	 * than by the swing's own start.
	 */
	void arm_teardown_stop();

	/**
	 * Spend the pass without consuming a stop. The seam calls this when a NEW attack arrives:
	 * whatever teardown armed the flag never raised its stop, and a flag left standing would
	 * absorb this swing's real one.
	 */
	void clear_teardown_stop();

	/**
	 * Should the player's graph events be traced right now?
	 *
	 * True while the state is live, and then for a bounded burst of events after a cut. The
	 * burst is the point: the events that say whether a cut actually handed off to an attack
	 * all arrive *after* the state is gone, so a trace that stopped at the cut would go dark
	 * exactly where the only interesting question is. Bounded by a count rather than a clock
	 * because this is read on the animation thread.
	 */
	/**
	 * Close the swing tracker because our own `shtb` state has just taken that swing. Call
	 * ONLY after the graph has accepted the entry -- an entry it turned down has taken nothing,
	 * and the local latch's put-back needs the swing still open to return the press to its end.
	 * No-op when no swing is tracked.
	 */
	void close_swing_taken_over();

	bool should_trace_graph_events();

	/**
	 * End a concentration channel: hand its combo position on and make sure the state is
	 * gone. Sent whether or not the state is still live -- a channel normally leaves it at
	 * the end of the start clip, so by release there is usually nothing left to exit.
	 */
	void end_channel(RE::PlayerCharacter* pc);

	/**
	 * Leave the cast state early via SH2_CastExit. Sent unconditionally: the event's
	 * only listener is the state's own local transition, so it reaches nothing when
	 * the state is not live, and a missed exit cannot strand the state.
	 *
	 * Returns whether the graph CONSUMED the notify. A consumed exit means a transition is
	 * still to land -- about 14 ms of it, measured -- and the seam waits that out before
	 * forwarding the attack that caused the cut. A refused one leaves nothing to wait for.
	 */
	bool cancel(RE::PlayerCharacter* pc);

	/**
	 * An attack is ending the cast: send `SH2_CastCut`, whose 1hm transition is instant, before
	 * the caller's `SH2_CastExit`. The exit's default 0.2 s blend, completing under a swing that
	 * had already started, restarted the attack clip.
	 *
	 * Returns whether the graph took it. A taken cut leaves the state before the exit arrives, so
	 * the exit is then refused, yet the graph still has its return to magic-ready to land: the
	 * caller must treat a taken cut as a pending exit and hold the attack for it.
	 */
	bool notify_attack_cut(RE::PlayerCharacter* pc);

	/**
	 * Time out a cast state the graph never left. Called once per frame from update_cast.
	 *
	 * The state's only ordinary end is the graph raising SH2_CastExit, and the graph is free to
	 * refuse the notify that asks for it — measured seven times in one session. A state that can
	 * only be cleared by an event that may never arrive cannot be allowed to own the input latch
	 * for the rest of the session, so past the cap this tears the cast down as a cancel would.
	 * A held concentration channel is exempt: it legitimately owns its state for the whole hold.
	 */
	void poll_watchdog(RE::PlayerCharacter* pc);

	/**
	 * Arm the last sampled MCO combo so the next restore edge writes it back.
	 * Abilities reuse this, the same combo-position write a Driver Cast makes; they must
	 * not write MCO_nextattack=1 on entry.
	 */
	void arm_combo_restore();

	/**
	 * End-of-cast cleanup; same send as cancel(), kept separate for call-site intent.
	 * The state also exits itself through an end-of-clip trigger raising SH2_CastExit.
	 */
	void finish(RE::PlayerCharacter* pc);

	void reset_session();

	/**
	 * Interrupt the left MagicCaster when that hand holds a spell. Driver Cast
	 * begin, Ability begin, and yielding our shtb clip all use this so a cancelled
	 * MSCO left charge cannot stick IsCasting until sheathe.
	 */
	void interrupt_equipped_casters_if_spell(RE::PlayerCharacter* pc);
}
