#include "combo_cache.h"

#include <cstdlib>
#include <iostream>

using SpellHotbar::casts::CastComboIndex;
using SpellHotbar::casts::CastShape;
using SpellHotbar::casts::CastDelivery;
using SpellHotbar::casts::ArmedDelivery;
using SpellHotbar::casts::classify_armed_delivery;
using SpellHotbar::casts::clip_annotations_unreachable;
using SpellHotbar::casts::retired_cast_stays_armed;
using SpellHotbar::casts::graph_is_in_cuttable_follow_through;
using SpellHotbar::casts::HotbarCastPress;
using SpellHotbar::casts::McoCombo;
using SpellHotbar::casts::RollingMcoCombo;
using SpellHotbar::casts::capture_hotbar_press_to_prevent_dual_fire;
using SpellHotbar::casts::classify_cast_delivery;
using SpellHotbar::casts::classify_hotbar_cast_press;
using SpellHotbar::casts::kTeardownStopCapMs;
using SpellHotbar::casts::teardown_stop_absorbs_close;
using SpellHotbar::casts::cut_committed_cast_for_left_hand_press;
using SpellHotbar::casts::isolate_caster_before_vanilla_spellfire;
using SpellHotbar::casts::SpellFireHand;
using SpellHotbar::casts::spellfire_hand_bit;
using SpellHotbar::casts::spellfire_hand_for_tag;
using SpellHotbar::casts::spellfire_hand_is_armed;
using SpellHotbar::casts::isolate_caster_for_driver_cast;
using SpellHotbar::casts::spellfire_event_commits_the_cast;
using SpellHotbar::casts::spellfire_arm_mask;
using SpellHotbar::casts::keep_commitment_until_cut;
using SpellHotbar::casts::MscoChargeCurve;
using SpellHotbar::casts::charge_time_to_anim_speed;
using SpellHotbar::casts::is_msco_combo_window_close_event;
using SpellHotbar::casts::is_msco_combo_window_open_event;
using SpellHotbar::casts::is_mco_combo_index_edge;
using SpellHotbar::casts::McoSgviVariable;
using SpellHotbar::casts::McoSuccessorTable;
using SpellHotbar::casts::McoSwingTracker;
using SpellHotbar::casts::live_sample_is_pre_advance;
using SpellHotbar::casts::parse_mco_sgvi_sample;
using SpellHotbar::casts::parse_sgvi_int;
using SpellHotbar::casts::should_sample_live_at_cast_begin;
using SpellHotbar::casts::window_close_is_a_stomp_to_undo;
using SpellHotbar::casts::seam_open_disarms_pending_restore;
using SpellHotbar::casts::reopen_keeps_restore_taught;
using SpellHotbar::casts::AbilityLatch;
using SpellHotbar::casts::ability_latch_opens_on_hit;
using SpellHotbar::casts::ability_latch_opens_at;
using SpellHotbar::casts::classify_ability_latch;
using SpellHotbar::casts::is_ability_graph_lost_event;
using SpellHotbar::casts::is_ability_hit_frame_event;
using SpellHotbar::casts::is_hit_frame_annotation;
using SpellHotbar::casts::is_ability_latch_event;
using SpellHotbar::casts::is_ability_win_open_event;
using SpellHotbar::casts::should_capture_attack_during_ability;
using SpellHotbar::casts::should_cut_ability_for_attack;
using SpellHotbar::casts::should_record_mco_combo_sample;
using SpellHotbar::casts::should_hold_cast_for_foreign_swing;
using SpellHotbar::casts::should_retain_local_cast_intent;
using SpellHotbar::casts::swing_kind_for_seam;
using SpellHotbar::casts::AttackSeamKind;
using SpellHotbar::casts::classify_attack_seam_event;
using SpellHotbar::casts::cast_state_watchdog_expired;
using SpellHotbar::casts::kCastStateCapMs;
using SpellHotbar::casts::kForeignSwingPressBufferMs;
using SpellHotbar::casts::kLocalLatchCapMs;
using SpellHotbar::casts::press_hold_cap_ms;
using SpellHotbar::casts::local_latch_hold_expired;
using SpellHotbar::casts::should_yield_shtb_before_hotbar_shout;
using SpellHotbar::casts::channel_chain_window_open;
using SpellHotbar::casts::channel_end_arms_combo_restore;
using SpellHotbar::casts::combo_sample_survives_hold;
using SpellHotbar::casts::should_cut_channel_for_attack;
using SpellHotbar::casts::cast_entry_walks_clip_set;
using SpellHotbar::casts::exit_without_spellfire_is_a_dropped_press;
using SpellHotbar::casts::spellfire_advances_cast_index;
using SpellHotbar::casts::spellfire_opens_combo_window;
using SpellHotbar::casts::deferred_attack_should_send;
using SpellHotbar::casts::seam_holds_for_earlier_cut;
using SpellHotbar::casts::ready_edge_consumes_restore;
using SpellHotbar::casts::is_cast_exit_landed_event;
using SpellHotbar::casts::is_mco_swing_window_close_event;
using SpellHotbar::casts::is_mco_swing_window_open_event;
using SpellHotbar::casts::kDeferredAttackCapMs;

namespace {

int g_failures = 0;

void expect(bool cond, const char* msg)
{
	if (!cond) {
		std::cerr << "FAIL: " << msg << '\n';
		++g_failures;
	}
}

void rolling_empty_is_not_usable()
{
	RollingMcoCombo cache;
	expect(!cache.usable(0.0).has_value(), "empty rolling combo is not usable");
}

void rolling_returns_the_recorded_value_not_plus_one()
{
	RollingMcoCombo cache;
	cache.record(McoCombo{ .nextAttack = 3, .nextPowerAttack = 2 }, 100.0);
	const auto got = cache.usable(100.0);
	expect(got.has_value(), "fresh record is usable");
	expect(got && got->nextAttack == 3, "preserve nextAttack, never derive +1");
	expect(got && got->nextPowerAttack == 2, "preserve nextPowerAttack");
}

void rolling_is_usable_at_the_age_cap()
{
	RollingMcoCombo cache;
	cache.record(McoCombo{ .nextAttack = 2, .nextPowerAttack = 1 }, 0.0);
	expect(cache.usable(RollingMcoCombo::kMaxAgeMs).has_value(), "usable at 5000ms");
	expect(!cache.usable(RollingMcoCombo::kMaxAgeMs + 1.0).has_value(), "stale at 5001ms");
}

void rolling_later_record_replaces_the_earlier()
{
	RollingMcoCombo cache;
	cache.record(McoCombo{ .nextAttack = 2, .nextPowerAttack = 1 }, 0.0);
	cache.record(McoCombo{ .nextAttack = 4, .nextPowerAttack = 3 }, 10.0);
	const auto got = cache.usable(10.0);
	expect(got && got->nextAttack == 4, "latest attack-time sample wins");
}

void restore_is_consumed_after_one_write()
{
	RollingMcoCombo cache;
	cache.record(McoCombo{ .nextAttack = 3, .nextPowerAttack = 2 }, 100.0);
	cache.arm(100.0);
	const auto peeked = cache.peek();
	expect(peeked && peeked->nextAttack == 3, "peek preserves the sampled index");
	expect(cache.peek().has_value(), "peek does not consume");
	const auto first = cache.consume();
	expect(first.has_value(), "armed restore is writable once");
	expect(first && first->nextAttack == 3, "first write preserves nextAttack");
	expect(first && first->nextPowerAttack == 2, "first write preserves nextPowerAttack");
	expect(!cache.consume().has_value(), "a later ready marker cannot rewrite the index");
	expect(!cache.peek().has_value(), "consumed restore is no longer pending");
}

void stale_sample_does_not_arm_a_restore()
{
	RollingMcoCombo cache;
	cache.record(McoCombo{ .nextAttack = 4, .nextPowerAttack = 3 }, 0.0);
	cache.arm(RollingMcoCombo::kMaxAgeMs + 1.0);
	expect(!cache.consume().has_value(), "stale sample writes nothing and leaves MCO at 1");
}

void empty_cache_does_not_arm_a_restore()
{
	RollingMcoCombo cache;
	cache.arm(0.0);
	expect(!cache.consume().has_value(), "no attack-time sample means no restore");
}

void a_new_swing_drops_a_pending_restore()
{
	RollingMcoCombo cache;
	cache.record(McoCombo{ .nextAttack = 2, .nextPowerAttack = 1 }, 0.0);
	cache.arm(0.0);
	cache.record(McoCombo{ .nextAttack = 3, .nextPowerAttack = 2 }, 10.0);
	expect(!cache.consume().has_value(), "a real swing consumes no leftover restore");
}

void disarm_drops_a_pending_restore_without_writing()
{
	RollingMcoCombo cache;
	cache.record(McoCombo{ .nextAttack = 2, .nextPowerAttack = 1 }, 0.0);
	cache.arm(0.0);
	cache.disarm();
	expect(!cache.consume().has_value(), "disarm writes nothing");
}

void cast_index_starts_at_one_and_wraps_after_four()
{
	CastComboIndex idx;
	expect(idx.current() == 1, "first clip is 1");
	idx.advance();
	expect(idx.current() == 2, "second clip is 2");
	idx.advance();
	expect(idx.current() == 3, "third clip is 3");
	idx.advance();
	expect(idx.current() == 4, "fourth clip is 4");
	idx.advance();
	expect(idx.current() == 1, "wraps to 1, never derives past the clip set");
}

void cast_index_is_unchanged_by_an_attack_gap()
{
	CastComboIndex idx;
	idx.advance();
	expect(idx.current() == 2, "an intervening attack does not reset the cast index");
}

void idle_press_starts_an_ordinary_cast()
{
	expect(classify_hotbar_cast_press(false, false, false) == HotbarCastPress::start,
		"no live cast is an ordinary first press");
	expect(classify_hotbar_cast_press(false, true, true) == HotbarCastPress::start,
		"a stale holding flag cannot turn an idle press into a chain");
}

void committed_follow_up_press_chains()
{
	expect(classify_hotbar_cast_press(true, true, true) == HotbarCastPress::chain,
		"a second press inside the open window on a committed Driver Cast is a combo step");
}

void committed_press_after_winclose_is_refused()
{
	expect(classify_hotbar_cast_press(true, true, false) == HotbarCastPress::refuse,
		"after WinClose, a committed Driver Cast refuses a late follow-up until CastExit");
}

void live_cast_without_commitment_is_refused()
{
	expect(classify_hotbar_cast_press(true, false, false) == HotbarCastPress::refuse,
		"before spellfire, or a concentration channel, the second press is refused");
	expect(classify_hotbar_cast_press(true, false, true) == HotbarCastPress::refuse,
		"a window bit without commitment cannot chain");
}

// One decoder for the two SpellFire tags, so isolation and commitment cannot disagree about
// what an event is.
void spellfire_tags_decode_to_their_own_hand()
{
	expect(spellfire_hand_for_tag("MLh_SpellFire_Event") == SpellFireHand::left,
		"MLh is the left hand's throw frame");
	expect(spellfire_hand_for_tag("MRh_SpellFire_Event") == SpellFireHand::right,
		"MRh is the right hand's throw frame");
	expect(spellfire_hand_for_tag("MSCO_WinOpen") == SpellFireHand::none,
		"a window tag is not a SpellFire");
	expect(spellfire_hand_for_tag("") == SpellFireHand::none,
		"an empty tag names no hand");
	expect(spellfire_hand_bit(SpellFireHand::none) == 0U,
		"`none` owns no mask bit, so it can never match an armed hand");
	expect(!spellfire_hand_is_armed(SpellFireHand::none, 3U),
		"a dual arming still does not arm a non-SpellFire event");
}

void spellfire_of_either_armed_hand_opens_the_combo_window()
{
	constexpr std::uint8_t left_armed{ 1U };
	constexpr std::uint8_t right_armed{ 2U };
	constexpr std::uint8_t dual_armed{ 3U };

	expect(is_msco_combo_window_open_event(SpellFireHand::left, left_armed),
		"a left cast's own SpellFire opens the combo window");
	// Any hand's SpellFire is the graph-side commitment point. Keying the window to MLh
	// alone keeps every right-hand clip on cast index 1 (observed live).
	expect(is_msco_combo_window_open_event(SpellFireHand::right, right_armed),
		"right-hand SpellFire opens the combo window too");
	expect(is_msco_combo_window_open_event(SpellFireHand::left, dual_armed) &&
			is_msco_combo_window_open_event(SpellFireHand::right, dual_armed),
		"a dual cast arms both, so either authored event is its own");
	// With the per-hand pack live, an unarmed hand's event belongs to some other cast and
	// must not advance this one's index or open its window.
	expect(!is_msco_combo_window_open_event(SpellFireHand::right, left_armed),
		"an equipped right-hand vanilla cast does not commit a left Driver Cast graph-side");
	expect(!is_msco_combo_window_open_event(SpellFireHand::left, right_armed),
		"an equipped left-hand vanilla cast does not commit a right Driver Cast graph-side");
	expect(!is_msco_combo_window_open_event(SpellFireHand::none, dual_armed),
		"a non-SpellFire tag never opens the cast combo window, however armed the cast is");
	expect(!is_msco_combo_window_open_event(SpellFireHand::left, 0U),
		"with nothing armed there is no cast to commit");
}

void winclose_tags_close_the_combo_window()
{
	expect(is_msco_combo_window_close_event("MSCO_WinClose"), "MSCO_WinClose closes the window");
	expect(is_msco_combo_window_close_event("MCO_WinClose"), "MCO_WinClose closes the window");
	expect(is_msco_combo_window_close_event("MSCO_winclose"), "MSCO_winclose closes the window");
	expect(is_msco_combo_window_close_event("MCO_winclose"), "MCO_winclose closes the window");
	expect(!is_msco_combo_window_close_event("MSCO_WinOpen"), "MSCO_WinOpen does not close the window");
	expect(!is_msco_combo_window_close_event("MCO_WinOpen"), "MCO_WinOpen does not close the window");
	expect(!is_msco_combo_window_close_event("MSCO_winopen"), "MSCO_winopen does not close the window");
	expect(!is_msco_combo_window_close_event("MCO_winopen"), "MCO_winopen does not close the window");
}

void shipped_exponential_curve_is_1_at_base_time()
{
	const MscoChargeCurve shipped{};
	expect(charge_time_to_anim_speed(0.15f, shipped) == 1.0f,
		"base time 0.15s is speed 1.0 on the shipped exponential curve");
}

void charge_mechanic_off_is_always_one()
{
	MscoChargeCurve off{};
	off.mechanic_on = false;
	expect(charge_time_to_anim_speed(0.5f, off) == 1.0f, "mechanic off leaves clips at 1.0");
	expect(charge_time_to_anim_speed(2.0f, off) == 1.0f, "mechanic off ignores long charge");
}

void firebolt_charge_is_slower_than_base_on_the_shipped_curve()
{
	const MscoChargeCurve shipped{};
	const float speed = charge_time_to_anim_speed(0.5f, shipped);
	expect(speed > 0.81f && speed < 0.82f,
		"Firebolt 0.5s is ~0.815 on (0.15/0.5)^0.17");
}

void zero_charge_clamps_to_max_speed()
{
	const MscoChargeCurve shipped{};
	expect(charge_time_to_anim_speed(0.0f, shipped) == 1.25f,
		"zero charge would divide by zero; clamp to max speed 1.25");
}

void two_second_charge_is_near_min_speed()
{
	const MscoChargeCurve shipped{};
	const float speed = charge_time_to_anim_speed(2.0f, shipped);
	expect(speed > 0.63f && speed < 0.66f, "2s charge is ~0.644 on (0.15/2)^0.17");
}

void linear_mode_is_1_at_its_base_time()
{
	MscoChargeCurve linear{};
	linear.exp_mode = false;
	linear.base_time = 0.5f;
	expect(charge_time_to_anim_speed(0.5f, linear) == 1.0f,
		"linear mode is speed 1.0 at the authored base time");
}

void chain_press_keeps_commitment_until_the_cut()
{
	expect(keep_commitment_until_cut(HotbarCastPress::chain),
		"a chain press still needs the commitment bit when start_cast runs the cut");
	expect(!keep_commitment_until_cut(HotbarCastPress::start),
		"an idle start must not inherit a leftover shout's spellfire");
	expect(!keep_commitment_until_cut(HotbarCastPress::refuse),
		"a refused press does not keep commitment");
}

void handled_hotbar_press_is_captured_when_the_left_hand_holds_a_spell()
{
	expect(capture_hotbar_press_to_prevent_dual_fire(true, true),
		"a Direct Cast with a spell in the left hand must not also reach vanilla");
	expect(!capture_hotbar_press_to_prevent_dual_fire(true, false),
		"without a left-hand spell there is no dual-fire to prevent");
	expect(!capture_hotbar_press_to_prevent_dual_fire(false, true),
		"an unhandled press is not captured by this rule");
}

void driver_cast_isolates_the_left_caster_when_that_hand_holds_a_spell()
{
	expect(isolate_caster_for_driver_cast(true),
		"an in-progress charge is cut when a Driver Cast begins");
	expect(!isolate_caster_for_driver_cast(false),
		"an empty or weapon hand has no equipped spell to isolate");
}

// The arming mask itself is a pure contract.
// hand_mode values: 1 left, 2 right, 3 dual; bits: 1 left, 2 right.
void arm_mask_follows_the_resolved_hand()
{
	expect(spellfire_arm_mask(1) == 1U, "a left cast arms only the left SpellFire");
	expect(spellfire_arm_mask(2) == 2U, "a right cast arms only the right SpellFire");
	expect(spellfire_arm_mask(3) == 3U, "a dual cast arms both hands' events");
	expect(spellfire_arm_mask(0) == 1U, "an unresolved auto falls back to the left annotation");
	expect(spellfire_arm_mask(4) == 1U, "voice cannot arm nothing; it falls back to left");
}

void driver_cast_isolates_before_vanilla_sees_left_spellfire()
{
	constexpr std::uint8_t left_armed{ 1U };
	expect(isolate_caster_before_vanilla_spellfire(true, SpellFireHand::left, left_armed) ==
			SpellFireHand::left,
		"a live Driver Cast must isolate before vanilla processes the clip's left SpellFire");
	expect(isolate_caster_before_vanilla_spellfire(false, SpellFireHand::left, left_armed) ==
			SpellFireHand::none,
		"an ordinary left-hand MSCO cast keeps vanilla SpellFire");
	expect(isolate_caster_before_vanilla_spellfire(true, SpellFireHand::none, left_armed) ==
			SpellFireHand::none,
		"other graph events still reach vanilla during a Driver Cast");
}

// The same rule, per hand. The event's own hand names the caster, so a right-hand clip
// silences the RIGHT equipped caster instead of leaving it to fire.
void driver_cast_isolates_the_hand_the_spellfire_event_names()
{
	constexpr std::uint8_t right_armed{ 2U };
	expect(isolate_caster_before_vanilla_spellfire(true, SpellFireHand::right, right_armed) ==
			SpellFireHand::right,
		"a right-hand clip's MRh SpellFire isolates the right caster, not the left");
	expect(isolate_caster_before_vanilla_spellfire(false, SpellFireHand::right, right_armed) ==
			SpellFireHand::none,
		"an ordinary equipped right-hand cast keeps vanilla SpellFire");
}

// Isolation is what swallows the event before vanilla, so isolating an UNARMED hand eats an
// unrelated vanilla cast released mid-Driver-Cast.
void driver_cast_leaves_an_unarmed_hands_spellfire_to_vanilla()
{
	constexpr std::uint8_t left_armed{ 1U };
	constexpr std::uint8_t right_armed{ 2U };
	constexpr std::uint8_t dual_armed{ 3U };

	expect(isolate_caster_before_vanilla_spellfire(true, SpellFireHand::right, left_armed) ==
			SpellFireHand::none,
		"a left Driver Cast leaves an equipped right-hand spell's own SpellFire alone");
	expect(isolate_caster_before_vanilla_spellfire(true, SpellFireHand::left, right_armed) ==
			SpellFireHand::none,
		"a right Driver Cast leaves an equipped left-hand spell's own SpellFire alone");
	expect(isolate_caster_before_vanilla_spellfire(true, SpellFireHand::left, dual_armed) ==
			SpellFireHand::left &&
			isolate_caster_before_vanilla_spellfire(true, SpellFireHand::right, dual_armed) ==
				SpellFireHand::right,
		"a dual cast arms both hands, so both equipped casters are still silenced");
	expect(isolate_caster_before_vanilla_spellfire(true, SpellFireHand::left, 0U) ==
			SpellFireHand::none,
		"nothing armed isolates nothing, even while the driver holds the graph");
}

// Acceptance needs the same driver-active term isolation carries. The mask stays armed for a
// cast's whole GCD tail and through a retired cast's armed window, so without that term a VANILLA
// release in an armed hand -- a staff's own attack, an equipped spell -- is accepted and written
// to the log as `graph raised a ... SpellFire event` with no isolation line beside it. Nothing is
// mis-delivered, but the commitment evidence in the log is polluted with events no driver cast
// raised.
void a_vanilla_spellfire_after_the_clip_commits_nothing()
{
	constexpr std::uint8_t right_armed = 2U;
	expect(spellfire_event_commits_the_cast(true, SpellFireHand::right, right_armed),
		"our own clip's event, in a hand this cast armed, IS the commitment point");
	expect(!spellfire_event_commits_the_cast(false, SpellFireHand::right, right_armed),
		"a staff's own release after the shtb clip is gone belongs to vanilla, however long the "
		"mask stays armed behind it");
	expect(!spellfire_event_commits_the_cast(true, SpellFireHand::left, right_armed),
		"an unarmed hand's event commits nothing, driver live or not");
	expect(!spellfire_event_commits_the_cast(true, SpellFireHand::none, right_armed),
		"a tag that names no hand commits nothing");
}

// The two halves of the commitment point are one rule. A disagreement between them is invisible in
// the log except as a `graph raised` line with no isolation line under it, so the agreement is
// asserted rather than assumed.
void acceptance_and_isolation_answer_the_same_question()
{
	constexpr SpellFireHand hands[] = { SpellFireHand::none, SpellFireHand::left,
		SpellFireHand::right };
	constexpr std::uint8_t masks[] = { 0U, 1U, 2U, 3U };
	for (const auto hand : hands) {
		for (const auto mask : masks) {
			for (const auto active : { false, true }) {
				const bool isolates =
					isolate_caster_before_vanilla_spellfire(active, hand, mask) !=
					SpellFireHand::none;
				expect(isolates == spellfire_event_commits_the_cast(active, hand, mask),
					"isolation and acceptance must never disagree about whose event this is");
			}
		}
	}
}

void clip_4_does_not_deliver_during_its_windup()
{
	expect(classify_cast_delivery(false, true, false, true) == CastDelivery::wait,
		"clip 4's 0.5s floor must not release during the windup before SpellFire");
}

void clip_4_delivers_when_spellfire_arrives_past_the_floor()
{
	expect(classify_cast_delivery(false, true, true, true) == CastDelivery::deliver,
		"clip 4 delivers at the SpellFire pose once the authored floor has passed");
}

void clips_1_to_3_still_deliver_near_the_start()
{
	expect(classify_cast_delivery(false, false, true, true) == CastDelivery::deliver,
		"clips 1-3 deliver at SpellFire even when it lands before the authored time");
	expect(classify_cast_delivery(false, false, true, false) == CastDelivery::deliver,
		"a cut after SpellFire still delivers; the annotation already led");
	expect(classify_cast_delivery(false, true, true, true) == CastDelivery::deliver,
		"clips 1-3 still deliver when SpellFire and the authored time have both landed");
}

void missing_annotation_falls_back_when_the_clip_ends()
{
	expect(classify_cast_delivery(false, true, false, false) == CastDelivery::deliver,
		"a clip that never raises SpellFire still delivers once it has ended past the floor");
}

void losing_the_state_before_the_floor_cancels()
{
	expect(classify_cast_delivery(false, false, false, false) == CastDelivery::cancel,
		"losing the cast state before the floor and before SpellFire cancels");
}

void an_undelivered_cuttable_cast_is_armed_when_it_retires()
{
	expect(retired_cast_stays_armed(true, false),
		"retiring at GCD expiry before SpellFire must not eat the cast -- it stays armed");
	expect(!retired_cast_stays_armed(true, true),
		"a cast that already delivered has nothing left to arm");
	expect(!retired_cast_stays_armed(false, false),
		"only a cuttable cast is retired with its clip still running");
}

void an_armed_payload_waits_for_its_own_spellfire()
{
	expect(classify_armed_delivery(false, false, true) == ArmedDelivery::hold,
		"the clip is still playing and has not raised its event yet: nothing to do");
	expect(classify_armed_delivery(false, true, true) == ArmedDelivery::on_spellfire,
		"the normal unpressed case -- clip 4's SpellFire at ~1.78s delivers a cast retired at 1.5s");
}

void an_armed_payload_falls_back_to_the_clip_end()
{
	expect(classify_armed_delivery(false, false, false) == ArmedDelivery::on_clip_end,
		"a clip that ended having raised no SpellFire still delivers through the end-of-clip fallback");
}

void a_camera_that_leaves_the_clip_graph_takes_the_annotation_with_it()
{
	// Measured live: a cast that BEGAN in third person and then lost the camera can never see
	// MLh_SpellFire_Event, because that annotation is on the graph the camera left. It
	// delivered at 8.01s, on the state watchdog, against 0.43s undisturbed.
	expect(clip_annotations_unreachable(false, true),
		"began on the clip path, camera now first: its SpellFire cannot arrive");

	// The three cases that must NOT reroute, because their commitment still works.
	expect(!clip_annotations_unreachable(false, false),
		"third person throughout is the ordinary cast -- its own SpellFire arrives");
	expect(!clip_annotations_unreachable(true, true),
		"a cast that STARTED in first person is already on the timer; this is not a second route");
	expect(!clip_annotations_unreachable(true, false),
		"nor does a voice-path cast change its mind when the camera comes back");
}

void an_armed_payload_delivers_exactly_once()
{
	expect(classify_armed_delivery(true, true, true) == ArmedDelivery::hold,
		"the cut already delivered this payload; its SpellFire must not deliver it again");
	expect(classify_armed_delivery(true, false, false) == ArmedDelivery::hold,
		"nor may the clip-end fallback deliver a payload the cut already sent");
}

void a_retired_cast_leaves_a_cuttable_follow_through()
{
	expect(graph_is_in_cuttable_follow_through(false, true),
		"the instance is retired but the clip plays on: the attack cut must still land");
	expect(!graph_is_in_cuttable_follow_through(false, false),
		"clip over and the state left: there is nothing to cut");
}

void a_live_cast_is_never_follow_through()
{
	expect(!graph_is_in_cuttable_follow_through(true, true),
		"a CHARGING cast still has its instance -- cutting it would cost the player the spell, so "
		"only the committed gate may admit it");
	expect(!graph_is_in_cuttable_follow_through(true, false),
		"a live cast that is not even driving the graph is not follow-through either");
}

void already_delivered_does_not_fire_again()
{
	expect(classify_cast_delivery(true, true, true, true) == CastDelivery::wait,
		"a payload that already left the hand is not delivered twice");
}

void left_hand_press_cuts_a_committed_hotbar_cast()
{
	expect(cut_committed_cast_for_left_hand_press(true, true, true),
		"a left-hand FNF press during a committed Driver Cast ends the hotbar state");
	expect(!cut_committed_cast_for_left_hand_press(false, true, true),
		"before commitment the left-hand press keeps today's behaviour");
	expect(!cut_committed_cast_for_left_hand_press(true, false, true),
		"left attack with a weapon or shield is block, not an MSCO hand cast");
	expect(!cut_committed_cast_for_left_hand_press(true, true, false),
		"an unrelated key does not cut");
}

void ability_latch_prefers_winopen_then_hitframe_then_artexit()
{
	expect(classify_ability_latch(true, 3) == AbilityLatch::winOpen,
		"a clip with WinOpen latches there even if it also has HitFrames");
	expect(classify_ability_latch(true, 0) == AbilityLatch::winOpen,
		"WinOpen is the Ability latch when present");
	expect(classify_ability_latch(false, 1) == AbilityLatch::hitFrame,
		"HitFrame is the Ability latch when the clip has no WinOpen");
	expect(classify_ability_latch(false, 0) == AbilityLatch::artExit,
		"SH2_ArtExit is the Ability latch when the clip has neither window");
}

void the_hit_frame_latch_waits_for_the_last_hit()
{
	// The reported bug: a multi-hit art opened on hit 1 and was cancellable through the rest of
	// its own combo. Measured over the art pack, 16 of the 25 arts on this fallback land several
	// hits, first at a median 22% of the clip and last at 55%.
	expect(!ability_latch_opens_on_hit(3, 1), "a three-hit art stays shut on its first hit");
	expect(!ability_latch_opens_on_hit(3, 2), "and on its second");
	expect(ability_latch_opens_on_hit(3, 3), "and opens on its last");

	expect(ability_latch_opens_on_hit(1, 1), "a single-hit art opens on that hit, as before");

	// A clip that lands more hits than its annotations promised must not stay shut forever.
	expect(ability_latch_opens_on_hit(2, 5), "an extra hit past the count still leaves it open");

	// Zero hits is the artExit latch, which never routes through here.
	expect(!ability_latch_opens_on_hit(0, 1), "no hit frames means this latch never opens");
}

void the_hit_frame_latch_opens_when_the_clip_reaches_its_last_hit()
{
	// Dual Flurry: hit frames at 0.583 (twice) and 0.883 (twice). Only three of the four arrived.
	expect(!ability_latch_opens_at(0.5833f, 0.8833f), "Dual Flurry's first pair leaves the latch shut");
	expect(ability_latch_opens_at(0.8833f, 0.8833f), "the first of its last pair opens it, however many arrive");
	expect(ability_latch_opens_at(0.86f, 0.8833f), "a frame early still counts as reached");
	expect(!ability_latch_opens_at(0.82f, 1.05f), "Head Chopper's third hit (0.82) leaves it shut for the fourth (1.05)");
	expect(ability_latch_opens_at(1.10f, 1.05f), "a hit frame arriving late still opens it");
	expect(!ability_latch_opens_at(-1.0f, 0.5f), "an unknown clip time never opens it: the caller counts instead");
}

void ability_latch_events_match_the_classified_kind()
{
	expect(is_ability_win_open_event("MCO_WinOpen"), "MCO_WinOpen opens the Ability latch");
	expect(is_ability_win_open_event("MSCO_WinOpen"), "MSCO_WinOpen opens the Ability latch");
	expect(is_ability_win_open_event("MCO_winopen"), "lowercase MCO WinOpen opens the Ability latch");
	expect(!is_ability_win_open_event("MCO_WinClose"), "WinClose is not the Ability latch");
	expect(is_ability_win_open_event("BFCO_NextWinStart"), "BFCO_NextWinStart, MCO_WinOpen's alias, opens the Ability latch");
	expect(is_ability_latch_event(AbilityLatch::winOpen, "BFCO_NextWinStart"),
		"a BFCO-native window opens a winOpen-latched Ability at runtime");
	expect(!is_ability_win_open_event("MCO_PowerWinOpen"), "a power window does not move a shipped Ability's latch");
	expect(!is_ability_win_open_event("BFCO_NextPowerWinStart"), "nor does BFCO's power window");
	expect(is_ability_hit_frame_event("HitFrame"), "HitFrame is the fallback Ability latch event");
	expect(!is_ability_hit_frame_event("MCO_WinOpen"), "WinOpen is not HitFrame");
	expect(is_ability_latch_event(AbilityLatch::winOpen, "MCO_WinOpen"),
		"a WinOpen latch fires on WinOpen");
	expect(!is_ability_latch_event(AbilityLatch::winOpen, "HitFrame"),
		"a WinOpen latch does not fire early at HitFrame");
	expect(is_ability_latch_event(AbilityLatch::hitFrame, "HitFrame"),
		"a HitFrame latch fires on HitFrame");
	expect(is_ability_latch_event(AbilityLatch::artExit, "SH2_ArtExit"),
		"an ArtExit latch fires on SH2_ArtExit");
	expect(!is_ability_latch_event(AbilityLatch::artExit, "HitFrame"),
		"an ArtExit latch does not fire at HitFrame");
}

void every_hit_frame_annotation_counts_toward_the_latch()
{
	expect(is_hit_frame_annotation("HitFrame"), "a plain HitFrame counts");
	expect(is_hit_frame_annotation("HitFrame.$AoW_Knockback"),
		"Flurry Strike's knockback finisher counts: the graph sends it as HitFrame");
	expect(is_hit_frame_annotation("HitFrame.$ES_HeavyParalysis"), "a paralysis payload counts");
	expect(is_hit_frame_annotation("Hitframe"), "Crane Style's lower-case f counts");
	expect(!is_hit_frame_annotation("preHitFrame"), "preHitFrame is not a hit");
	expect(!is_hit_frame_annotation("HitFrames"), "a longer name is not HitFrame");
	expect(!is_hit_frame_annotation("PIE.$HitFog"), "a PIE payload naming a hit is not one");
	expect(!is_hit_frame_annotation(""), "empty text is not a hit");
}

void a_press_behind_our_shtb_is_retained_until_the_latch_opens()
{
	expect(should_retain_local_cast_intent(true, false, false, false, false),
		"a hotbar press during our Driver Cast or Ability before the latch is the local queue");
	expect(!should_retain_local_cast_intent(true, true, false, false, false),
		"once the latch is open the press starts now rather than waiting");
	expect(!should_retain_local_cast_intent(false, false, false, false, false),
		"nothing of ours live and no swing open: nothing to wait for");
	expect(!should_retain_local_cast_intent(true, false, true, false, false),
		"concentration is not this queue");
}

void a_press_during_someone_elses_swing_is_retained_until_the_swing_closes()
{
	expect(should_retain_local_cast_intent(false, false, false, true, false),
		"a visible swing with nothing of ours live holds the press locally");
	expect(!should_retain_local_cast_intent(false, false, true, true, false),
		"a held concentration channel is still not this queue, swing or not");
	expect(!should_retain_local_cast_intent(true, true, false, true, false),
		"our own open latch releases even while the tracker still says a swing is up");
}

void a_press_is_released_when_the_swings_cancel_window_opens()
{
	expect(!should_retain_local_cast_intent(false, false, false, true, true),
		"the swing's cancel window releases the held press instead of holding to attackStop");
	expect(should_retain_local_cast_intent(false, false, false, true, false),
		"before the window opens the press is still held");
	expect(!should_retain_local_cast_intent(false, false, false, false, false),
		"no swing at all is still no reason to hold");
	expect(!should_retain_local_cast_intent(true, true, false, true, true),
		"our own latch answers first; the other swing's window changes nothing there");
	expect(should_retain_local_cast_intent(true, false, false, true, true),
		"our own CLOSED latch still holds, window or not -- our clip owns the graph");
}

// Once `AttackState` names `shtb`, nothing refuses a mid-swing cast, so the refusal cannot be the
// signal that a press arrived at the wrong frame. The same foreign-swing term therefore has to be
// asked BEFORE the attempt.
// Two waits, two clocks. Behind our own clip the press is queued behind an animation we started;
// behind someone else's swing it is an input buffer, and 4 s of buffer makes a cast go off at a
// frame the player did not choose.
void the_two_press_waits_expire_on_different_clocks()
{
	expect(press_hold_cap_ms(true) == kForeignSwingPressBufferMs,
		"a press waiting on someone else's swing expires on the input buffer");
	expect(press_hold_cap_ms(false) == kLocalLatchCapMs,
		"a press queued behind our own clip keeps the latch cap");
	expect(kForeignSwingPressBufferMs < kLocalLatchCapMs,
		"the input buffer is the shorter of the two, or it is not a buffer");
	expect(!local_latch_hold_expired(1000.0, 900.0, press_hold_cap_ms(true)),
		"100 ms before the window still fires -- that is the responsiveness worth keeping");
	expect(local_latch_hold_expired(1000.0, 700.0, press_hold_cap_ms(true)),
		"300 ms is past the buffer: the press drops and the slot flashes");
	expect(!local_latch_hold_expired(1000.0, 700.0, press_hold_cap_ms(false)),
		"the same 300 ms behind our own clip is nowhere near its cap");
}

void a_press_inside_a_foreign_swing_waits_before_it_is_tried()
{
	expect(should_hold_cast_for_foreign_swing(false, false, true, false),
		"a swing open and short of its window holds the press instead of cutting the swing here");
	expect(!should_hold_cast_for_foreign_swing(false, false, true, true),
		"the cancel window is the frame the press was waiting for");
	expect(!should_hold_cast_for_foreign_swing(false, false, false, false),
		"no swing, no wait -- an ordinary cast still starts on its press");
	expect(!should_hold_cast_for_foreign_swing(true, false, true, false),
		"our own clip owns the graph; our_latch_is_closed() answers that, not this");
	expect(!should_hold_cast_for_foreign_swing(false, true, true, false),
		"a held concentration channel is not this queue, swing or not");
}

void the_swing_tracker_carries_its_own_cancel_window()
{
	McoSwingTracker tracker;
	tracker.set_window_open(true);
	expect(!tracker.open_swing().has_value(),
		"a window annotation with no open swing records nothing");

	tracker.open(McoSgviVariable::next_power_attack, 1, false);
	const auto fresh = tracker.open_swing();
	expect(fresh && !fresh->window_open, "a swing opens before its own window does");

	tracker.set_window_open(true);
	const auto in_window = tracker.open_swing();
	expect(in_window && in_window->window_open, "MCO_PowerWinOpen puts the swing in its window");

	tracker.set_window_open(false);
	const auto closed = tracker.open_swing();
	expect(closed && !closed->window_open, "the window closes without closing the swing");

	tracker.set_window_open(true);
	tracker.open(McoSgviVariable::next_attack, 2, false);
	const auto next = tracker.open_swing();
	expect(next && !next->window_open,
		"the next swing does not inherit the previous swing's open window");
}

void the_cancel_window_is_named_by_the_events_bfco_actually_raises()
{
	expect(is_mco_swing_window_open_event("MCO_WinOpen"), "the light window opens the span");
	expect(is_mco_swing_window_open_event("MCO_PowerWinOpen"),
		"the power window opens it too -- a power attack raises both, together");
	expect(is_mco_swing_window_open_event("MSCO_WinOpen"), "the MSCO spelling is the same event");
	expect(!is_mco_swing_window_open_event("MCO_InputBuffer"),
		"the input buffer annotation is not the cancel window");
	expect(!is_mco_swing_window_open_event("attackStart"), "entry is not the window");

	expect(is_mco_swing_window_close_event("MCO_WinClose"), "the light close ends the span");
	expect(is_mco_swing_window_close_event("MCO_PowerWinClose"),
		"the power close ends it first, which is the conservative edge");
	expect(!is_mco_swing_window_close_event("MCO_WinOpen"), "an open is not a close");
	expect(!is_mco_swing_window_close_event("attackStop"),
		"the swing's end closes the SWING, and does not travel this branch");
}

void a_restore_survives_the_ready_edge_until_an_attack_or_expiry()
{
	// The live trace: a1, a2, spell, shout. The shout's press cut the spell and armed attack 3;
	// the ready edge and the ready reset landed in the same millisecond, the shout began 1.7 s
	// later, and the attack after it played attack 1.
	expect(!ready_edge_consumes_restore(false),
		"with nothing held, a shout can still come between the ready edge and the next swing");
	expect(!ready_edge_consumes_restore(true),
		"a held attack is sent AFTER the ready reset, so the restore must survive to its send");

	RollingMcoCombo rolling;
	rolling.record(McoCombo{ 3, 3 }, 0.0);
	expect(rolling.arm(1000.0).has_value(), "the cut arms attack 3");
	expect(!rolling.expire_pending(2700.0), "1.7 s later, at the shout, it is still pending");
	expect(rolling.peek() && rolling.peek()->nextAttack == 3,
		"so the ready reset the shout samples behind is undone back to attack 3");
	rolling.credit_held_time(1100.0);
	expect(!rolling.expire_pending(1000.0 + RollingMcoCombo::kPendingMaxMs + 1000.0),
		"time spent shouting does not count against the pending restore");
	expect(rolling.expire_pending(1000.0 + RollingMcoCombo::kPendingMaxMs + 1200.0),
		"but past the cap with no attack, it expires and MCO's own reset stands");
	expect(!rolling.restore_pending(), "and nothing is left to put back");
}

void shouting_does_not_age_the_sample_out()
{
	// a2's window close sampled attack 3; a shout and then a spell outran the 5 s cap at a
	// player's pace, and the spell's exit restored nothing.
	RollingMcoCombo rolling;
	rolling.record(McoCombo{ 3, 3 }, 0.0);
	rolling.credit_held_time(2500.0);
	expect(rolling.arm(6000.0).has_value(),
		"6 s after the sample, with 2.5 s of it shouting, the spell still restores attack 3");
	RollingMcoCombo uncredited;
	uncredited.record(McoCombo{ 3, 3 }, 0.0);
	expect(!uncredited.arm(6000.0).has_value(), "6 s of real idle time still ages it out");
}

void a_held_attack_keeps_the_restore_past_the_ready_reset()
{

	// The measured trace: restore armed at 3, ready edge, ready payload writes 1, held send.
	RollingMcoCombo rolling;
	rolling.record(McoCombo{ 3, 3 }, 0.0);
	expect(rolling.arm(10.0).has_value(), "a fresh sample arms");
	if (ready_edge_consumes_restore(true)) {
		(void)rolling.consume();
	}
	expect(rolling.restore_pending(), "the ready reset that follows still finds a restore to undo");
	expect(rolling.peek() && rolling.peek()->nextAttack == 3, "and undoes it back to attack 3");
}

void a_cut_attack_waits_for_the_exit_transition_to_land()
{
	const double cut_at = 1000.0;

	expect(!deferred_attack_should_send(false, true, cut_at, cut_at, kDeferredAttackCapMs),
		"nothing held is nothing to send");
	expect(!deferred_attack_should_send(true, false, cut_at + 5.0, cut_at, kDeferredAttackCapMs),
		"the attack waits while the exit's transition is still in flight");
	expect(deferred_attack_should_send(true, true, cut_at + 14.0, cut_at, kDeferredAttackCapMs),
		"the measured 14 ms ready triple releases the held attack");
	expect(deferred_attack_should_send(true, false, cut_at + kDeferredAttackCapMs, cut_at,
			   kDeferredAttackCapMs),
		"a graph that consumes the exit and raises no ready tag still gets the attack, on the cap");
	expect(!deferred_attack_should_send(true, false, cut_at + kDeferredAttackCapMs - 1.0, cut_at,
			   kDeferredAttackCapMs),
		"one millisecond short of the cap it is still waiting");
}

void an_attack_after_an_earlier_cut_is_held_until_the_cap()
{
	const double cut_at = 1000.0;

	expect(seam_holds_for_earlier_cut(false, true, cut_at + 3.0, cut_at, kDeferredAttackCapMs),
		"an Action's attack reaching the seam 3 ms after its cut is held for the ready triple");
	expect(!seam_holds_for_earlier_cut(true, true, cut_at + 14.0, cut_at, kDeferredAttackCapMs),
		"the held attack's own re-send forwards");
	expect(!seam_holds_for_earlier_cut(false, false, cut_at + 3.0, cut_at, kDeferredAttackCapMs),
		"no cut waiting, or the ready triple already landed: the attack forwards at once");
	expect(!seam_holds_for_earlier_cut(false, true, cut_at + kDeferredAttackCapMs, cut_at,
			   kDeferredAttackCapMs),
		"a marker no ready tag ever cleared lapses on the cap and cannot hold a later press");
}

void the_ready_triple_is_what_says_the_exit_landed()
{
	expect(is_cast_exit_landed_event("MSCO_MagicReady"),
		"magic-ready is the graph saying the cast state is behind it");
	expect(is_cast_exit_landed_event("SBF_ReadyStart"),
		"the ready start of the same triple counts, for a graph that raises only one");
	expect(!is_cast_exit_landed_event("attackStop"),
		"attackStop leads the triple but every real swing ends with it, so it is not an edge");
	expect(!is_cast_exit_landed_event("SBF_ReadyStop"),
		"leaving ready again is not the transition landing");
}

void the_seam_opens_a_swing_for_attacks_only()
{
	expect(swing_kind_for_seam(AttackSeamKind::attack) == McoSgviVariable::next_attack,
		"a light attack opens the light swing");
	expect(swing_kind_for_seam(AttackSeamKind::power_attack) == McoSgviVariable::next_power_attack,
		"a power attack opens the power swing");
	expect(!swing_kind_for_seam(AttackSeamKind::none), "a non-attack event opens no swing");
	expect(!swing_kind_for_seam(AttackSeamKind::bash), "a bash is not a combo position");
	expect(!swing_kind_for_seam(AttackSeamKind::block), "a block is not a combo position");
	expect(!swing_kind_for_seam(AttackSeamKind::dodge), "a dodge is not a combo position");

	McoSwingTracker tracker;
	if (const auto kind = swing_kind_for_seam(classify_attack_seam_event("BFCOAttackstart_1"))) {
		tracker.open(*kind, 2, false);
	}
	const auto open = tracker.open_swing();
	expect(open && open->kind == McoSgviVariable::next_attack && open->playing == 2,
		"a BFCO combo step classified by the seam is an open light swing at its index");
	McoSwingTracker untouched;
	if (const auto kind = swing_kind_for_seam(classify_attack_seam_event("BFCO_MoveStart"))) {
		untouched.open(*kind, 2, false);
	}
	expect(!untouched.open_swing(), "BFCO's graph chatter opens nothing");
}

void a_retained_press_is_dropped_once_its_cap_runs_out()
{
	expect(!local_latch_hold_expired(1000.0, 0.0, kLocalLatchCapMs),
		"a press well inside the cap is still waiting to fire");
	expect(!local_latch_hold_expired(kLocalLatchCapMs - 1.0, 0.0, kLocalLatchCapMs),
		"a press one millisecond short of the cap is not dropped yet");
	expect(local_latch_hold_expired(kLocalLatchCapMs, 0.0, kLocalLatchCapMs),
		"a press that reaches the cap is dropped rather than held");
	expect(local_latch_hold_expired(9000.0, 1000.0, kLocalLatchCapMs),
		"a press long past the cap is dropped whenever the poll next runs");
}

void a_wedged_cast_state_expires_but_a_held_channel_never_does()
{
	expect(cast_state_watchdog_expired(true, false, kCastStateCapMs + 0.5, 0.5, kCastStateCapMs),
		"a state the graph never exited is cleared once the cap is reached");
	expect(!cast_state_watchdog_expired(true, false, kCastStateCapMs - 0.5, 0.5, kCastStateCapMs),
		"a cast still inside the cap is left alone");
	expect(!cast_state_watchdog_expired(true, true, 60000.0, 0.5, kCastStateCapMs),
		"a held concentration channel owns its state for the whole hold");
	expect(!cast_state_watchdog_expired(true, false, 60000.0, 0.0, kCastStateCapMs),
		"no recorded entry is not an elapsed time");
	expect(!cast_state_watchdog_expired(false, false, 60000.0, 0.5, kCastStateCapMs),
		"there is nothing to clear when no cast state is live");
}

void attack_after_the_ability_latch_cuts_and_is_not_captured()
{
	expect(should_cut_ability_for_attack(true, true, true),
		"an attack after the Ability latch sends ArtExit and stays uncaptured");
	expect(!should_cut_ability_for_attack(true, false, true),
		"an attack before the Ability latch must not skip the current hit");
	expect(!should_cut_ability_for_attack(false, true, true),
		"idle is not an Ability cut");
	expect(should_capture_attack_during_ability(true, false, true),
		"an attack before the Ability latch is captured so it cannot mash through");
	expect(!should_capture_attack_during_ability(true, true, true),
		"after the latch the attack press is left uncaptured");
	expect(!should_capture_attack_during_ability(true, false, false),
		"an unrelated key is not captured as an Ability mash-through");
}

void ready_and_default_start_mean_the_ability_graph_is_lost()
{
	// Measured live: Holding Thorns (art 34) left SH2_Art_State at SBF_ReadyStart
	// without SH2_ArtExit. The latch stayed shut and the seam captured every attack until
	// the eight-second deadline.
	expect(is_ability_graph_lost_event("SBF_ReadyStart"),
		"ReadyStart means the graph is back in ready, so the Ability latch must drop");
	expect(is_ability_graph_lost_event("SBF_DefaultStart"),
		"DefaultStart is the same return to locomotion");
	expect(!is_ability_graph_lost_event("SBF_ReadyStop"),
		"leaving ready is how an Ability starts, not how it ends");
	expect(!is_ability_graph_lost_event("SBF_DefaultStop"),
		"leaving default is not a return to idle");
	expect(!is_ability_graph_lost_event("SH2_ArtExit"),
		"ArtExit is the authored end, not a lost-state signal");
}

void ability_hitframe_does_not_replace_the_mco_combo_sample()
{
	expect(!should_record_mco_combo_sample(true),
		"HitFrame on our Ability or Driver Cast must not stomp the sampled combo");
	expect(should_record_mco_combo_sample(false),
		"someone else's MCO swing still samples combo position");
}

void a_legal_hotbar_shout_yields_the_live_shtb_clip()
{
	expect(should_yield_shtb_before_hotbar_shout(true, true),
		"once the latch is open a shout must stop the Ability or Driver Cast first");
	expect(!should_yield_shtb_before_hotbar_shout(true, false),
		"before the latch the shout is queued, not overlapped");
	expect(!should_yield_shtb_before_hotbar_shout(false, true),
		"idle has no shtb clip to stop");
}


void a_channel_does_not_walk_the_cast_clip_set()
{
	expect(spellfire_advances_cast_index(CastShape::fire_and_forget),
		"a fire-and-forget clip advances SH2's cast index at SpellFire");
	expect(!spellfire_advances_cast_index(CastShape::channel),
		"a channel is one cast held, not a chain step");
}

void a_channel_enters_by_its_own_notify()
{
	expect(cast_entry_walks_clip_set(CastShape::fire_and_forget),
		"a fire-and-forget press picks its entry from the MSCO_left1..left4 cast index");
	expect(!cast_entry_walks_clip_set(CastShape::channel),
		"a channel has one entry of its own and never walks the clip set");
}

void a_channel_exit_without_spellfire_is_not_a_dropped_press()
{
	expect(exit_without_spellfire_is_a_dropped_press(CastShape::fire_and_forget),
		"a throw clip cut before SpellFire produced no payload; the cast index resets");
	expect(!exit_without_spellfire_is_a_dropped_press(CastShape::channel),
		"a channel commits on the authored cast-time floor, so its exit must not reset the index");
}

void a_channel_does_not_open_the_follow_up_press_window()
{
	expect(spellfire_opens_combo_window(CastShape::fire_and_forget),
		"a committed Driver Cast opens its window at SpellFire");
	expect(!spellfire_opens_combo_window(CastShape::channel),
		"a second hotbar cast during a hold is a refusal, not a chain");
}

void the_combo_index_is_sampled_where_mco_writes_it()
{
	// Measured live: MCO_WinClose carries the NEXT swing's index; MCO_AttackInitiate and
	// HitFrame carry the swing already playing, which is why sampling there repeated an attack.
	expect(is_mco_combo_index_edge("MCO_WinClose"),
		"window close is retained as the fallback edge after the clip's SGVI advance");
	expect(is_mco_combo_index_edge("MSCO_WinClose"), "the MSCO spelling counts too");
	expect(!is_mco_combo_index_edge("attackStop"),
		"attackStop is MCO's reset to 1 -- the stomp the rolling cache exists to outlive");
	expect(!is_mco_combo_index_edge("MCO_AttackInitiate"),
		"attack time reads the swing already playing, one step behind");
	expect(!is_mco_combo_index_edge("HitFrame"), "so does HitFrame");
	expect(!is_mco_combo_index_edge("MCO_WinOpen"),
		"this load order's packs annotate AT window open, so that edge races the write");
}

void the_sgvi_payload_carries_the_clips_own_advance()
{
	const auto next = parse_sgvi_int("@SGVI|MCO_nextattack|3", "MCO_nextattack");
	expect(next && *next == 3, "the clip's own advance is read out of the tag, not the graph");
	const auto power = parse_sgvi_int("@SGVI|MCO_nextpowerattack|2", "MCO_nextpowerattack");
	expect(power && *power == 2, "the power counter parses the same way");
	const auto wrap = parse_sgvi_int("@SGVI|MCO_nextattack|1", "MCO_nextattack");
	expect(wrap && *wrap == 1, "a wrap back to 1 is a real advance, not a stomp to ignore");
	const auto wide = parse_sgvi_int("@SGVI|MCO_nextattack|12", "MCO_nextattack");
	expect(wide && *wide == 12, "a moveset longer than nine steps still parses");
}

void a_sgvi_tag_for_another_variable_is_rejected()
{
	expect(!parse_sgvi_int("@SGVI|msco_nextright|2", "MCO_nextattack").has_value(),
		"an unrelated clip counter must not be learned as a combo position");
	expect(!parse_sgvi_int("@SGVI|MCO_nextattackfoo|2", "MCO_nextattack").has_value(),
		"the variable name is matched in full, not as a prefix");
	expect(!parse_sgvi_int("@SGVI|MCO_nextattac|2", "MCO_nextattack").has_value(),
		"a truncated name is not the variable");
	expect(!parse_sgvi_int("@SGVI|MCO_nextpowerattack|2", "MCO_nextattack").has_value(),
		"the power counter is a different variable");
}

void a_malformed_sgvi_tag_is_rejected()
{
	expect(!parse_sgvi_int("@SGVI|MCO_nextattack|", "MCO_nextattack").has_value(),
		"a payload with no value teaches nothing");
	expect(!parse_sgvi_int("@SGVI|MCO_nextattack|x", "MCO_nextattack").has_value(),
		"a non-integer value is not an index");
	expect(!parse_sgvi_int("@SGVI|MCO_nextattack|2x", "MCO_nextattack").has_value(),
		"trailing junk invalidates the whole value");
	expect(!parse_sgvi_int("@SGVI|MCO_nextattack", "MCO_nextattack").has_value(),
		"the value separator is required");
	expect(!parse_sgvi_int("@SGVI|MCO_nextattack|-", "MCO_nextattack").has_value(),
		"a lone sign is not an integer");
}

void a_non_sgvi_tag_is_rejected()
{
	expect(!parse_sgvi_int("MCO_WinClose", "MCO_nextattack").has_value(),
		"an ordinary animation event is not an SGVI payload");
	expect(!parse_sgvi_int("@SGVF|MCO_nextattack|3", "MCO_nextattack").has_value(),
		"the float payload is a different annotation");
	expect(!parse_sgvi_int("", "MCO_nextattack").has_value(), "an empty tag parses to nothing");
	expect(!parse_sgvi_int("@SGVI|", "MCO_nextattack").has_value(), "a bare prefix parses to nothing");
}

void the_sgvi_sampler_routes_each_variable_to_its_own_field()
{
	const auto attack = parse_mco_sgvi_sample("@SGVI|MCO_nextattack|3");
	expect(attack && attack->variable == McoSgviVariable::next_attack && attack->value == 3,
		"MCO_nextattack routes to the attack field");
	const auto power = parse_mco_sgvi_sample("@SGVI|MCO_nextpowerattack|4");
	expect(power && power->variable == McoSgviVariable::next_power_attack && power->value == 4,
		"MCO_nextpowerattack routes to the power field");
	expect(!parse_mco_sgvi_sample("@SGVI|msco_nextright|2").has_value(),
		"a variable that is neither counter is not sampled at all");
	expect(!parse_mco_sgvi_sample("MCO_WinClose").has_value(),
		"a window-close is handled by the fallback edge, not by this parser");
}

void a_partial_update_keeps_the_other_field_and_refreshes_the_age()
{
	// The two counters arrive as separate events, so a clip that writes only MCO_nextattack
	// must not blank the power position it was never told about.
	RollingMcoCombo cache;
	cache.record(McoCombo{ .nextAttack = 2, .nextPowerAttack = 4 }, 0.0);
	cache.record_next_attack(3, 4000.0);
	const auto got = cache.usable(4000.0);
	expect(got && got->nextAttack == 3, "the taught value replaces the attack field");
	expect(got && got->nextPowerAttack == 4, "the untold field keeps its last value");
	expect(cache.usable(4000.0 + RollingMcoCombo::kMaxAgeMs).has_value(),
		"the partial update refreshes the sample's age");
	expect(!cache.usable(4000.0 + RollingMcoCombo::kMaxAgeMs + 1.0).has_value(),
		"and it is measured from the partial update, not the earlier full record");
}

void a_partial_update_drops_a_pending_restore()
{
	RollingMcoCombo cache;
	cache.record(McoCombo{ .nextAttack = 2, .nextPowerAttack = 1 }, 0.0);
	cache.arm(0.0);
	expect(cache.restore_pending(), "armed restore is pending");
	cache.record_next_attack(3, 10.0);
	expect(!cache.restore_pending(), "a real swing's own advance invalidates a pending restore");
	expect(!cache.consume().has_value(), "so there is nothing left to write back");
}

void a_partial_power_update_leaves_the_attack_field_alone()
{
	RollingMcoCombo cache;
	cache.record_next_power_attack(2, 0.0);
	const auto first = cache.usable(0.0);
	expect(first && first->nextPowerAttack == 2, "an empty cache can be seeded by one counter");
	expect(first && first->nextAttack == 1, "the untouched field stays at its default");
	cache.record_next_attack(4, 10.0);
	const auto both = cache.usable(10.0);
	expect(both && both->nextAttack == 4 && both->nextPowerAttack == 2,
		"two events across one swing build the whole position");
}

void a_mid_swing_cast_begin_is_sampled_live()
{
	expect(should_sample_live_at_cast_begin(true),
		"a begin() with IsAttacking=1 reads the interrupted swing's pre-stomp advance "
		"(measured n=3 mid-attack2)");
}

void a_cast_begin_outside_a_swing_teaches_nothing()
{
	expect(!should_sample_live_at_cast_begin(false),
		"a deferred re-begin or an idle begin reads the post-stomp 1 (measured n=1, 272ms "
		"later) and must leave the cache's last teaching alone");
}

void a_bfco_swing_open_consumes_the_pending_restore()
{
	expect(seam_open_disarms_pending_restore(true, true),
		"the send-side seam is BFCO's consume edge: MCO_AttackInitiate never fires");
	expect(!seam_open_disarms_pending_restore(false, true),
		"an ordinary swing has nothing to consume");
	expect(!seam_open_disarms_pending_restore(true, false),
		"an out-of-range open is not a swing and must leave the restore pending");

	// Observed in a live log: Night Flurry armed next=2, the clip taught 3, SH2 put 2 back
	// because pending survived the seam open. After disarm the teaching is kept.
	RollingMcoCombo cache;
	cache.record(McoCombo{ .nextAttack = 2, .nextPowerAttack = 8 }, 0.0);
	cache.arm(0.0);
	expect(cache.restore_pending(), "Night Flurry armed the sampled index");
	expect(window_close_is_a_stomp_to_undo(cache.restore_pending()),
		"while pending, every clip advance would be put-ours-back");
	if (seam_open_disarms_pending_restore(cache.restore_pending(), true)) {
		cache.disarm();
	}
	expect(!cache.restore_pending(), "the first post-Ability swing consumed it");
	expect(!window_close_is_a_stomp_to_undo(false),
		"so the clip's own advance is a value to learn, not a stomp");
	cache.record_next_attack(3, 50.0);
	const auto got = cache.usable(50.0);
	expect(got && got->nextAttack == 3,
		"attack2's clip teaching 3 is kept; the combo is not pinned at 2");
}

void an_already_open_restore_taught_swing_stays_restore_taught_on_reopen()
{
	expect(reopen_keeps_restore_taught(true, false, false),
		"a pending restore still marks the swing when initiate is the first open");
	expect(reopen_keeps_restore_taught(false, true, true),
		"MCO initiate after the seam must not drop a restore-taught mark");
	expect(!reopen_keeps_restore_taught(false, true, false),
		"an ordinary already-open swing stays ordinary");
	expect(!reopen_keeps_restore_taught(false, false, true),
		"a stale mark with no open swing of this kind does not carry");
}

void a_pending_restore_survives_the_ready_reset()
{
	expect(window_close_is_a_stomp_to_undo(true),
		"with a restore pending, a window-close (our own cast clip fires one too) carries "
		"the reset's stomp and must be put back");
	expect(!window_close_is_a_stomp_to_undo(false),
		"with nothing pending, a window-close is MCO's genuine next index and is sampled");

	// The live failure: restore 3, the reset writes 1, and sampling it dropped the pending
	// restore so the next swing started the chain over.
	RollingMcoCombo cache;
	cache.record(McoCombo{ .nextAttack = 3, .nextPowerAttack = 3 }, 0.0);
	cache.arm(0.0);
	expect(cache.restore_pending(), "armed restore is pending");
	const auto put_back = cache.peek();
	expect(put_back && put_back->nextAttack == 3, "the reset is undone with our own index");
	expect(cache.restore_pending(), "putting it back does not consume it");
	const auto consumed = cache.consume();
	expect(consumed && consumed->nextAttack == 3, "the next real swing takes 3");
}

void a_hold_does_not_age_out_the_combo_position()
{
	// The failure this fixes: attack1, hold Flames five seconds, swing -- and the swing came
	// out as attack1 because the sample had aged past kMaxAgeMs during the hold.
	RollingMcoCombo cache;
	cache.record(McoCombo{ .nextAttack = 2, .nextPowerAttack = 2 }, 0.0);
	const double release = RollingMcoCombo::kMaxAgeMs + 3000.0;
	expect(!cache.usable(release).has_value(),
		"without crediting the hold, an 8s-old sample is stale and the chain resets");

	cache.credit_held_time(5000.0);
	const auto got = cache.usable(release);
	expect(got && got->nextAttack == 2,
		"crediting the 5s hold keeps the position, so the swing after it continues the chain");
}

void a_gap_before_the_hold_still_ages_out()
{
	// The cap still does its real job: a combo sampled in an earlier fight must not be
	// replayed into an unrelated swing just because a channel was held afterwards.
	expect(combo_sample_survives_hold(30000.0, 29000.0),
		"a long hold started right after the swing hands its position on");
	expect(!combo_sample_survives_hold(30000.0, 1000.0),
		"a swing 29s before the hold is a different fight and must not be restored");
	expect(combo_sample_survives_hold(RollingMcoCombo::kMaxAgeMs, 0.0),
		"a fire-and-forget cast inside the cap is unaffected");
}

void a_channel_is_chainable_out_only_once_it_streams()
{
	expect(!channel_chain_window_open(false, false), "no channel, no window");
	expect(!channel_chain_window_open(true, false),
		"a channel still in its charge has not committed anything to chain out of");
	expect(channel_chain_window_open(true, true), "a streaming channel may hand off");
}

void an_attack_during_a_streaming_channel_ends_it()
{
	expect(should_cut_channel_for_attack(channel_chain_window_open(true, true), true),
		"attack during a streaming channel ends the channel");
	expect(!should_cut_channel_for_attack(channel_chain_window_open(true, true), false),
		"a press that is not an attack leaves the channel alone");
	expect(!should_cut_channel_for_attack(channel_chain_window_open(true, false), true),
		"an attack before the channel streams keeps today's behaviour");
}

void a_channel_that_streamed_hands_its_combo_position_on()
{
	expect(channel_end_arms_combo_restore(true),
		"the swing after a hold continues the chain the hold interrupted");
	expect(!channel_end_arms_combo_restore(false),
		"a channel that never streamed has nothing to hand off");
}

void the_swing_tracker_starts_closed()
{
	McoSwingTracker tracker;
	expect(!tracker.open_swing().has_value(),
		"nothing is swinging until an initiate says so");
}

void the_swing_tracker_reads_back_what_the_initiate_opened()
{
	McoSwingTracker tracker;
	tracker.open(McoSgviVariable::next_attack, 2, false);
	const auto open = tracker.open_swing();
	expect(open.has_value(), "an initiate opens a swing");
	expect(open && open->kind == McoSgviVariable::next_attack, "the light initiate opens a light swing");
	expect(open && open->playing == 2, "the playing index is preserved, never derived");
	expect(open && !open->taught_by_restore, "an ordinary swing is not restore-taught");

	McoSwingTracker restored;
	restored.open(McoSgviVariable::next_power_attack, 3, true);
	const auto marked = restored.open_swing();
	expect(marked && marked->kind == McoSgviVariable::next_power_attack,
		"the power initiate opens a power swing");
	expect(marked && marked->taught_by_restore,
		"a swing whose initiate consumed a restore is marked; its playing index is unverified");
}

void a_second_initiate_replaces_the_open_swing()
{
	McoSwingTracker tracker;
	tracker.open(McoSgviVariable::next_attack, 2, true);
	tracker.open(McoSgviVariable::next_attack, 3, false);
	const auto open = tracker.open_swing();
	expect(open && open->playing == 3, "the newest swing is the open one");
	expect(open && !open->taught_by_restore,
		"the restore-taught mark belongs to its own swing and must not leak into the next");
}

void an_unreadable_playing_index_closes_the_tracker()
{
	McoSwingTracker tracker;
	tracker.open(McoSgviVariable::next_attack, 2, false);
	tracker.open(McoSgviVariable::next_attack, 0, false);
	expect(!tracker.open_swing().has_value(),
		"0 is not a combo index; forget the swing rather than reason from a wrong one");
	tracker.open(McoSgviVariable::next_attack, 11, false);
	expect(!tracker.open_swing().has_value(), "nor is an index past the plausible moveset length");
	tracker.open(McoSgviVariable::next_attack, -1, false);
	expect(!tracker.open_swing().has_value(), "nor is a negative read");
	tracker.open(McoSgviVariable::next_attack, 10, false);
	expect(tracker.open_swing().has_value(), "10 is still inside the accepted range");
}

void attack_stop_closes_the_swing()
{
	McoSwingTracker tracker;
	tracker.open(McoSgviVariable::next_attack, 2, false);
	tracker.close();
	expect(!tracker.open_swing().has_value(),
		"after attackStop the next @SGVI|MCO_nextattack|1 is the ready reset, not a wrap");
}

// The pass is one-shot in the driver, so the test models it the way the driver spends it: `armed`
// goes false as soon as a stop reads the flag, whether or not that stop was inside the cap.
void a_teardown_attack_stop_is_absorbed_once()
{
	bool armed = true;
	const double armed_at = 1000.0;

	const bool first = teardown_stop_absorbs_close(armed, armed_at + 10.0, armed_at,
		kTeardownStopCapMs);
	armed = false;
	expect(first,
		"the cast state's own exit stop, 10 ms after the cut, leaves the swing open");

	expect(!teardown_stop_absorbs_close(armed, armed_at + 20.0, armed_at, kTeardownStopCapMs),
		"the swing's real attackStop, the second stop of the pass, closes the tracker");
}

void a_teardown_pass_that_nothing_consumed_expires()
{
	expect(!teardown_stop_absorbs_close(true, 1000.0 + kTeardownStopCapMs, 1000.0,
			   kTeardownStopCapMs),
		"a teardown that raised no stop cannot reach forward and eat a later real one");
	expect(teardown_stop_absorbs_close(true, 1000.0 + kTeardownStopCapMs - 1.0, 1000.0,
			   kTeardownStopCapMs),
		"one millisecond short of the cap the pass is still live");
	expect(!teardown_stop_absorbs_close(false, 1000.0, 1000.0, kTeardownStopCapMs),
		"an uncut swing's attackStop closes the tracker exactly as it always did");
}

void the_successor_table_knows_nothing_by_default()
{
	McoSuccessorTable table;
	expect(!table.lookup(0, McoSgviVariable::next_attack, 1).has_value(),
		"a successor is never derived; an untaught pair stays unknown");
}

void the_successor_table_round_trips_what_a_clip_taught()
{
	McoSuccessorTable table;
	table.learn(3, McoSgviVariable::next_attack, 2, 3);
	const auto got = table.lookup(3, McoSgviVariable::next_attack, 2);
	expect(got && *got == 3, "swing 2 taught 3, so 2 is followed by 3 on that weapon");
	expect(!table.lookup(3, McoSgviVariable::next_attack, 3).has_value(),
		"learning 2->3 says nothing about what follows 3");
}

void a_later_teaching_replaces_the_earlier()
{
	McoSuccessorTable table;
	table.learn(0, McoSgviVariable::next_attack, 4, 1);
	table.learn(0, McoSgviVariable::next_attack, 4, 2);
	const auto got = table.lookup(0, McoSgviVariable::next_attack, 4);
	expect(got && *got == 2, "the clips are the authority; the newest teaching wins");
}

void the_two_counters_keep_separate_chains()
{
	McoSuccessorTable table;
	table.learn(1, McoSgviVariable::next_attack, 2, 3);
	table.learn(1, McoSgviVariable::next_power_attack, 2, 1);
	const auto light = table.lookup(1, McoSgviVariable::next_attack, 2);
	const auto power = table.lookup(1, McoSgviVariable::next_power_attack, 2);
	expect(light && *light == 3, "the light chain keeps its own successor at index 2");
	expect(power && *power == 1, "a shorter power chain wraps at 2 without touching the light one");
}

void each_weapon_keeps_its_own_moveset()
{
	McoSuccessorTable table;
	table.learn(4, McoSgviVariable::next_attack, 3, 4);
	expect(!table.lookup(5, McoSgviVariable::next_attack, 3).has_value(),
		"a greatsword's wrap must not answer for a dagger's");
	const auto got = table.lookup(4, McoSgviVariable::next_attack, 3);
	expect(got && *got == 4, "the weapon that taught it still knows it");
}

void out_of_range_teachings_are_refused()
{
	McoSuccessorTable table;
	table.learn(-1, McoSgviVariable::next_attack, 2, 3);
	table.learn(McoSuccessorTable::kWeaponKeys, McoSgviVariable::next_attack, 2, 3);
	expect(!table.lookup(0, McoSgviVariable::next_attack, 2).has_value(),
		"a weapon key outside 0..9 is refused rather than folded onto a neighbour's row");
	expect(!table.lookup(-1, McoSgviVariable::next_attack, 2).has_value(),
		"and looking one up stays empty");

	table.learn(0, McoSgviVariable::next_attack, 0, 3);
	table.learn(0, McoSgviVariable::next_attack, 11, 3);
	expect(!table.lookup(0, McoSgviVariable::next_attack, 0).has_value(),
		"0 is not a combo index on the from side");
	expect(!table.lookup(0, McoSgviVariable::next_attack, 11).has_value(),
		"nor past the plausible moveset length");

	table.learn(0, McoSgviVariable::next_attack, 2, 0);
	table.learn(0, McoSgviVariable::next_attack, 3, 99);
	expect(!table.lookup(0, McoSgviVariable::next_attack, 2).has_value(),
		"a taught value of 0 is a failed read, not a combo index");
	expect(!table.lookup(0, McoSgviVariable::next_attack, 3).has_value(),
		"nor is a value past the moveset length");
}

void a_sample_equal_to_the_playing_index_is_pre_advance()
{
	expect(live_sample_is_pre_advance(true, 2, 2),
		"the clip has not written its advance yet; restoring 2 would replay the interrupted swing");
	expect(!live_sample_is_pre_advance(true, 2, 3),
		"a value different from the playing index is the successor the clip already taught");
	expect(!live_sample_is_pre_advance(false, 2, 2),
		"with no open swing of that kind there is nothing to compare against; take the sample as-is");
	expect(!live_sample_is_pre_advance(false, 0, 1),
		"a closed tracker never claims pre-advance");
}

void the_sgvi_parser_reads_a_payload_shaped_string()
{
	// The engine splits `PIE.@SGVI|MCO_nextattack|2` at the first '.', so what the
	// parser is fed is the part after it. Same text, different field.
	const auto sample = parse_mco_sgvi_sample("@SGVI|MCO_nextattack|2");
	expect(sample && sample->variable == McoSgviVariable::next_attack && sample->value == 2,
		"the advance arrives in the payload and parses exactly as it did from a tag");
	expect(!parse_mco_sgvi_sample("PIE").has_value(),
		"the event name left behind by the split teaches nothing on its own");
	expect(!parse_mco_sgvi_sample("PIE.@SGVI|MCO_nextattack|2").has_value(),
		"the unsplit annotation is not what reaches the sink; only its halves do");
}

void an_interrupted_swing_hands_its_successor_on()
{
	// The interrupt end to end, in pure terms: swing 2 is up, the cast lands before the clip's own
	// advance, so the live read is 2 -- the swing being interrupted. Restoring that replays it.
	McoSwingTracker tracker;
	tracker.open(McoSgviVariable::next_attack, 2, false);
	const auto open = tracker.open_swing();
	expect(open.has_value(), "the swing is open at the moment of the cast");

	const int sampled = 2;
	const bool matching = open && open->kind == McoSgviVariable::next_attack;
	expect(live_sample_is_pre_advance(matching, open->playing, sampled),
		"a sample equal to the playing index is the pre-advance interrupt");

	McoSuccessorTable table;
	table.learn(0, McoSgviVariable::next_attack, 2, 3);
	const auto substituted = table.lookup(0, McoSgviVariable::next_attack, open->playing);
	expect(substituted && *substituted == 3,
		"the clip already taught that 2 is followed by 3, so the restore hands on 3");

	// The wrap is the case arithmetic would get wrong: on a four-step moveset, 4 is followed by 1.
	McoSuccessorTable wrap;
	wrap.learn(0, McoSgviVariable::next_attack, 4, 1);
	const auto wrapped = wrap.lookup(0, McoSgviVariable::next_attack, 4);
	expect(wrapped && *wrapped == 1,
		"the last swing wraps to 1 because the clip said so, never because of +1 arithmetic");
}

void a_reset_valued_payload_is_never_recorded_or_learned()
{
	// Measured live: a cut swing's AttackState-exit notify beats attackStop to the
	// sink, so it arrives with the tracker open and IsAttacking true -- the same gates a real
	// advance passes. Its value is always 1, and a clip can never teach itself, so 1 is
	// quarantined from the payload edge; the wrap pair rides the WinClose sampler instead.
	expect(!SpellHotbar::casts::payload_advance_is_recordable(1),
		"1 is the reset value; the exit notify of a cut swing carries it through every other gate");
	expect(SpellHotbar::casts::payload_advance_is_recordable(2),
		"a real advance to 2 is recordable");
	expect(SpellHotbar::casts::payload_advance_is_recordable(10),
		"the last plausible index is recordable");
	expect(!SpellHotbar::casts::payload_advance_is_recordable(0),
		"0 is a failed read, not a teaching");
	expect(!SpellHotbar::casts::payload_advance_is_recordable(11),
		"past the moveset range is not a teaching");
}

void an_unlearned_pre_advance_keeps_todays_behaviour()
{
	McoSuccessorTable table;
	const int sampled = 2;
	expect(live_sample_is_pre_advance(true, 2, sampled), "still a pre-advance interrupt");
	expect(!table.lookup(0, McoSgviVariable::next_attack, 2).has_value(),
		"with nothing learned the sampled value is kept -- the replay that shipped, logged as such");
}
}  // namespace

int main()
{
	rolling_empty_is_not_usable();
	rolling_returns_the_recorded_value_not_plus_one();
	rolling_is_usable_at_the_age_cap();
	rolling_later_record_replaces_the_earlier();
	restore_is_consumed_after_one_write();
	stale_sample_does_not_arm_a_restore();
	empty_cache_does_not_arm_a_restore();
	a_new_swing_drops_a_pending_restore();
	disarm_drops_a_pending_restore_without_writing();
	cast_index_starts_at_one_and_wraps_after_four();
	cast_index_is_unchanged_by_an_attack_gap();
	idle_press_starts_an_ordinary_cast();
	committed_follow_up_press_chains();
	committed_press_after_winclose_is_refused();
	live_cast_without_commitment_is_refused();
	spellfire_tags_decode_to_their_own_hand();
	spellfire_of_either_armed_hand_opens_the_combo_window();
	arm_mask_follows_the_resolved_hand();
	winclose_tags_close_the_combo_window();
	shipped_exponential_curve_is_1_at_base_time();
	charge_mechanic_off_is_always_one();
	firebolt_charge_is_slower_than_base_on_the_shipped_curve();
	zero_charge_clamps_to_max_speed();
	two_second_charge_is_near_min_speed();
	linear_mode_is_1_at_its_base_time();
	chain_press_keeps_commitment_until_the_cut();
	handled_hotbar_press_is_captured_when_the_left_hand_holds_a_spell();
	driver_cast_isolates_the_left_caster_when_that_hand_holds_a_spell();
	driver_cast_isolates_before_vanilla_sees_left_spellfire();
	driver_cast_isolates_the_hand_the_spellfire_event_names();
	driver_cast_leaves_an_unarmed_hands_spellfire_to_vanilla();
	a_vanilla_spellfire_after_the_clip_commits_nothing();
	acceptance_and_isolation_answer_the_same_question();
	clip_4_does_not_deliver_during_its_windup();
	clip_4_delivers_when_spellfire_arrives_past_the_floor();
	clips_1_to_3_still_deliver_near_the_start();
	missing_annotation_falls_back_when_the_clip_ends();
	losing_the_state_before_the_floor_cancels();
	already_delivered_does_not_fire_again();
	left_hand_press_cuts_a_committed_hotbar_cast();
	ability_latch_prefers_winopen_then_hitframe_then_artexit();
	the_hit_frame_latch_waits_for_the_last_hit();
	the_hit_frame_latch_opens_when_the_clip_reaches_its_last_hit();
	ability_latch_events_match_the_classified_kind();
	every_hit_frame_annotation_counts_toward_the_latch();
	a_press_behind_our_shtb_is_retained_until_the_latch_opens();
	a_press_during_someone_elses_swing_is_retained_until_the_swing_closes();
	a_press_inside_a_foreign_swing_waits_before_it_is_tried();
	the_two_press_waits_expire_on_different_clocks();
	a_press_is_released_when_the_swings_cancel_window_opens();
	the_swing_tracker_carries_its_own_cancel_window();
	the_cancel_window_is_named_by_the_events_bfco_actually_raises();
	a_held_attack_keeps_the_restore_past_the_ready_reset();
	a_cut_attack_waits_for_the_exit_transition_to_land();
	a_restore_survives_the_ready_edge_until_an_attack_or_expiry();
	shouting_does_not_age_the_sample_out();
	an_attack_after_an_earlier_cut_is_held_until_the_cap();
	the_ready_triple_is_what_says_the_exit_landed();
	the_seam_opens_a_swing_for_attacks_only();
	a_retained_press_is_dropped_once_its_cap_runs_out();
	a_wedged_cast_state_expires_but_a_held_channel_never_does();
	attack_after_the_ability_latch_cuts_and_is_not_captured();
	ready_and_default_start_mean_the_ability_graph_is_lost();
	ability_hitframe_does_not_replace_the_mco_combo_sample();
	a_legal_hotbar_shout_yields_the_live_shtb_clip();
	a_channel_does_not_walk_the_cast_clip_set();
	a_channel_enters_by_its_own_notify();
	a_channel_exit_without_spellfire_is_not_a_dropped_press();
	a_channel_does_not_open_the_follow_up_press_window();
	the_combo_index_is_sampled_where_mco_writes_it();
	the_sgvi_payload_carries_the_clips_own_advance();
	a_sgvi_tag_for_another_variable_is_rejected();
	a_malformed_sgvi_tag_is_rejected();
	a_non_sgvi_tag_is_rejected();
	the_sgvi_sampler_routes_each_variable_to_its_own_field();
	a_partial_update_keeps_the_other_field_and_refreshes_the_age();
	a_partial_update_drops_a_pending_restore();
	a_partial_power_update_leaves_the_attack_field_alone();
	a_mid_swing_cast_begin_is_sampled_live();
	a_cast_begin_outside_a_swing_teaches_nothing();
	a_bfco_swing_open_consumes_the_pending_restore();
	an_already_open_restore_taught_swing_stays_restore_taught_on_reopen();
	a_pending_restore_survives_the_ready_reset();
	a_hold_does_not_age_out_the_combo_position();
	a_gap_before_the_hold_still_ages_out();
	a_channel_is_chainable_out_only_once_it_streams();
	an_attack_during_a_streaming_channel_ends_it();
	a_channel_that_streamed_hands_its_combo_position_on();
	the_swing_tracker_starts_closed();
	the_swing_tracker_reads_back_what_the_initiate_opened();
	a_second_initiate_replaces_the_open_swing();
	an_unreadable_playing_index_closes_the_tracker();
	attack_stop_closes_the_swing();
	a_teardown_attack_stop_is_absorbed_once();
	a_teardown_pass_that_nothing_consumed_expires();
	the_successor_table_knows_nothing_by_default();
	the_successor_table_round_trips_what_a_clip_taught();
	a_later_teaching_replaces_the_earlier();
	the_two_counters_keep_separate_chains();
	each_weapon_keeps_its_own_moveset();
	out_of_range_teachings_are_refused();
	a_sample_equal_to_the_playing_index_is_pre_advance();
	the_sgvi_parser_reads_a_payload_shaped_string();
	an_interrupted_swing_hands_its_successor_on();
	a_reset_valued_payload_is_never_recorded_or_learned();
	an_unlearned_pre_advance_keeps_todays_behaviour();
	an_undelivered_cuttable_cast_is_armed_when_it_retires();
	an_armed_payload_waits_for_its_own_spellfire();
	an_armed_payload_falls_back_to_the_clip_end();
	a_camera_that_leaves_the_clip_graph_takes_the_annotation_with_it();
	an_armed_payload_delivers_exactly_once();
	a_retired_cast_leaves_a_cuttable_follow_through();
	a_live_cast_is_never_follow_through();

	if (g_failures != 0) {
		std::cerr << g_failures << " failure(s)\n";
		return EXIT_FAILURE;
	}
	std::cout << "ok\n";
	return EXIT_SUCCESS;
}
