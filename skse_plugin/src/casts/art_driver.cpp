#include "art_driver.h"
#include "ability_damage.h"
#include "bow_string_relay.h"
#include "clip_translation_driver.h"
#include "combo_cache.h"
#include "msco_cast_driver.h"
#include "unpaused_clock.h"
#include <atomic>
#include <mutex>
#include "../logger/logger.h"
#include "../game_data/game_data.h"

using namespace std::literals;

namespace SpellHotbar::casts::ArtDriver {

	namespace {
		std::atomic<bool> state_active{ false };
		std::atomic<float> damage_mult_snapshot{ kAbilityDamageMultDefault };
		std::atomic<bool> latch_is_open{ false };
		std::atomic<int> latch{ static_cast<int>(AbilityLatch::artExit) };
		// The live clip's hit-frame count, and how many have arrived. The latch opens on the
		// last one; see `ability_latch_opens_on_hit`.
		std::atomic<int> latch_hit_total{ 0 };
		std::atomic<int> latch_hits_seen{ 0 };
		// Clip time of the last hit frame; the latch opens once the clip reaches it
		// (`ability_latch_opens_at`). The count above stays for the log and as the fallback.
		std::atomic<float> latch_last_hit_time{ -1.0f };
		// Gameplay-ms stamp of the live art, 0 when nothing is running.
		std::atomic<double> started_ms{ 0.0 };
		// Gameplay-ms stamp of the last latch annotation this art received, falling back to its
		// start stamp. The deadline runs from here, not from `started_ms`: Blood Flurry's own
		// `SH2_ArtExit` lands at 8.957 s, so a start-anchored eight seconds cuts a working art.
		std::atomic<double> last_signal_ms{ 0.0 };
		std::atomic<int> trace_budget{ 0 };
		// Set by bind_latch during NotifyAnimationGraph; cleared at the start of begin().
		// Distinct from clip_generator_active: this is "did THIS notify bind a clip".
		std::atomic<bool> clip_bound_this_start{ false };
		std::atomic<bool> clip_generator_active{ false };
		// Cleared on Deactivate so the next Activate of the same HKX is a new start.
		std::atomic<const void*> last_bound_animation{ nullptr };
		std::atomic<const void*> activating_animation{ nullptr };
		constexpr int post_exit_trace_events{ 24 };
		// True between a relayed BowDraw and its BowRelease. See `bow_string_relay.h`.
		std::atomic<bool> bow_string_drawn{ false };
		// The live clip's BowDraw / BowRelease cue times, written at Activate (animation thread),
		// walked by `poll_bow_string` (main thread).
		std::mutex bow_cue_mutex;
		std::vector<BowStringCue> bow_cues;
		std::size_t bow_cue_next{ 0 };
		// Same lifecycle, same lock. The arm is NOT cleared with the cues: it belongs to an arrow
		// that may still be in flight after the Ability has ended.
		std::vector<ArrowHitCue> arrow_hit_cues;
		std::size_t arrow_hit_cue_next{ 0 };
		ArrowHitArm arrow_hit_armed{};

		void clear_bow_cues()
		{
			std::lock_guard lock{ bow_cue_mutex };
			bow_cues.clear();
			bow_cue_next = 0;
			arrow_hit_cues.clear();
			arrow_hit_cue_next = 0;
		}

		// For `on_exit`, which runs on the animation thread when the clip's own SH2_ArtExit ends
		// the art: the notify runs on the main thread a frame later instead of re-entering the
		// graph from inside its own event dispatch.
		void notify_bow_string(std::string_view event)
		{
			auto* tasks = SKSE::GetTaskInterface();
			if (!tasks) {
				return;
			}
			tasks->AddTask([event = std::string{ event }]() {
				if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
					const bool consumed = pc->NotifyAnimationGraph(event);
					logger::debug("SH2 art: relayed {} to the bow -> {}", event, consumed);
				}
			});
		}

		void send_exit(RE::PlayerCharacter* pc)
		{
			if (pc) {
				const bool consumed = pc->NotifyAnimationGraph("SH2_ArtExit"sv);
				logger::debug("SH2 art: notified SH2_ArtExit -> {}", consumed);
			}
		}

		void on_exit(std::string_view tag, bool reset_selector = true)
		{
			state_active.store(false, std::memory_order_release);
			damage_mult_snapshot.store(kAbilityDamageMultDefault, std::memory_order_relaxed);
			latch_is_open.store(false, std::memory_order_relaxed);
			started_ms.store(0.0, std::memory_order_relaxed);
			last_signal_ms.store(0.0, std::memory_order_relaxed);
			if (reset_selector) {
				GameData::reset_art_selector();
			}
			clear_bow_cues();
			if (bow_string_needs_reset_on_exit(bow_string_drawn.exchange(false, std::memory_order_relaxed))) {
				notify_bow_string("bowReset"sv);
			}
			MscoCastDriver::arm_combo_restore();
			logger::debug("SH2 art: state exiting ({})", tag);
		}
	}

	/**
	 * `begin()` is called exactly once, with a `CastingInstanceWeaponArt` freshly installed as
	 * `current_cast`. That instance's `update()` in casting_controller.cpp returns false for as long
	 * as this state is active, so it stays installed and the hotbar stays refused. Its eight-second
	 * check compares the instance's cast timer, which starts at zero for an art and only counts
	 * down, so that check never fires. Without a bound of its own `state_active` stays true with
	 * nothing to time it out and the input latch behind it retains every press; a camera flip
	 * mid-art held the hotbar for 185 seconds that way. `poll_deadline` below is that bound: eight
	 * seconds, the same figure as the cast state's own watchdog, and the same `cancel()` teardown.
	 *
	 * That deadline runs from the last annotation, not from the start. Anchoring it to the start
	 * makes it fire on healthy arts: Blood Flurry's graph sends its own `SH2_ArtExit` at 8.957 s,
	 * measured live, so eight seconds from the press cuts it a second early every time.
	 */
	bool begin(RE::PlayerCharacter* pc, float damage_mult)
	{
		if (!pc) {
			return false;
		}
		if (!ability_start_may_notify(clip_generator_active.load(std::memory_order_relaxed))) {
			logger::debug("SH2 art: leftover SH2_Art_Clip still Active; not notifying ArtStart");
			return false;
		}
		trace_budget.store(0, std::memory_order_relaxed);
		latch_is_open.store(false, std::memory_order_relaxed);
		latch.store(static_cast<int>(AbilityLatch::artExit), std::memory_order_relaxed);
		latch_hit_total.store(0, std::memory_order_relaxed);
		latch_hits_seen.store(0, std::memory_order_relaxed);
		latch_last_hit_time.store(-1.0f, std::memory_order_relaxed);
		clip_bound_this_start.store(false, std::memory_order_relaxed);
		activating_animation.store(nullptr, std::memory_order_relaxed);
		const bool sent = pc->NotifyAnimationGraph("SH2_ArtStart"sv);
		const bool took_the_clip =
			ability_start_took_the_clip(sent, clip_bound_this_start.load(std::memory_order_relaxed));
		if (took_the_clip) {
			damage_mult_snapshot.store(sanitize_ability_damage_mult(damage_mult), std::memory_order_relaxed);
			state_active.store(true, std::memory_order_release);
		} else {
			damage_mult_snapshot.store(kAbilityDamageMultDefault, std::memory_order_relaxed);
			state_active.store(false, std::memory_order_relaxed);
		}
		const double start_stamp = took_the_clip ? UnpausedClock::now_ms() : 0.0;
		started_ms.store(start_stamp, std::memory_order_relaxed);
		last_signal_ms.store(start_stamp, std::memory_order_relaxed);
		if (took_the_clip) {
			last_bound_animation.store(
				activating_animation.load(std::memory_order_relaxed), std::memory_order_relaxed);
			MscoCastDriver::interrupt_equipped_casters_if_spell(pc);
		} else if (sent) {
			logger::info(
				"SH2 art: SH2_ArtStart consumed but SH2_Art_Clip did not activate");
		} else {
			logger::info("SH2 art: SH2_ArtStart not consumed (mid-swing or patch missing)");
		}
		logger::debug("SH2 art: notified SH2_ArtStart -> {}", sent);
		return took_the_clip;
	}

	void bind_latch(bool has_win_open, int hit_frame_count, float last_hit_time, const void* animation,
		std::vector<BowStringCue> cues, std::vector<ArrowHitCue> hit_cues)
	{
		if (!shtb_clip_is_this_start(
				animation, last_bound_animation.load(std::memory_order_relaxed),
				// Bind runs before observe_clip_activate, so this flag is still
				// the previous generator. False here is "already idle" — same
				// HKX is a new start even if last_bound was not cleared yet.
				!clip_generator_active.load(std::memory_order_relaxed))) {
			if (!is_active()) {
				logger::info(
					"SH2 art: SH2_Art_Clip re-activated leftover animation; not this start");
			}
			return;
		}
		clip_bound_this_start.store(true, std::memory_order_relaxed);
		activating_animation.store(animation, std::memory_order_relaxed);
		const auto kind = classify_ability_latch(has_win_open, hit_frame_count);
		latch.store(static_cast<int>(kind), std::memory_order_relaxed);
		latch_hit_total.store(hit_frame_count, std::memory_order_relaxed);
		latch_hits_seen.store(0, std::memory_order_relaxed);
		latch_last_hit_time.store(last_hit_time, std::memory_order_relaxed);
		logger::debug("SH2 art: latch {} (winopen={} hitframes={} last at {:.3f}s)",
			static_cast<int>(kind), has_win_open, hit_frame_count, last_hit_time);
		std::stable_sort(cues.begin(), cues.end(),
			[](const BowStringCue& a, const BowStringCue& b) { return a.time < b.time; });
		if (!cues.empty()) {
			logger::debug("SH2 art: {} bow string cue(s), first at {:.3f}s", cues.size(), cues.front().time);
		}
		std::stable_sort(hit_cues.begin(), hit_cues.end(),
			[](const ArrowHitCue& a, const ArrowHitCue& b) { return a.time < b.time; });
		if (!hit_cues.empty()) {
			logger::debug("SH2 art: {} arrow hit cue(s), first at {:.3f}s", hit_cues.size(), hit_cues.front().time);
		}
		std::lock_guard lock{ bow_cue_mutex };
		bow_cues = std::move(cues);
		bow_cue_next = 0;
		arrow_hit_cues = std::move(hit_cues);
		arrow_hit_cue_next = 0;
	}

	ArrowHitEffect take_arrow_hit_effect()
	{
		std::lock_guard lock{ bow_cue_mutex };
		const auto effect = arrow_hit_effect_at(arrow_hit_armed, UnpausedClock::now_ms());
		if (effect != ArrowHitEffect::none) {
			arrow_hit_armed = {};
		}
		return effect;
	}

	void poll_clip_cues(RE::PlayerCharacter* pc)
	{
		if (!pc || !is_active()) {
			return;
		}
		const float clip_time = ClipTranslationDriver::art_clip_time();
		std::vector<BowStringEvent> relays;
		{
			std::lock_guard lock{ bow_cue_mutex };
			const std::size_t hits_due = arrow_hit_cues_due(arrow_hit_cues, arrow_hit_cue_next, clip_time);
			for (; arrow_hit_cue_next < hits_due; ++arrow_hit_cue_next) {
				arrow_hit_armed = arrow_hit_arm(arrow_hit_cues[arrow_hit_cue_next].effect, UnpausedClock::now_ms());
				logger::debug("SH2 art: arrow hit effect {} armed at {:.3f}s",
					static_cast<int>(arrow_hit_armed.effect), clip_time);
			}
			const std::size_t due = bow_string_cues_due(bow_cues, bow_cue_next, clip_time);
			bool drawn = bow_string_drawn.load(std::memory_order_relaxed);
			for (; bow_cue_next < due; ++bow_cue_next) {
				const auto relay = bow_string_relay_for(bow_cues[bow_cue_next].event, drawn);
				if (relay != BowStringEvent::none) {
					relays.push_back(relay);
					drawn = bow_string_drawn_after(drawn, relay);
				}
			}
			bow_string_drawn.store(drawn, std::memory_order_relaxed);
		}
		// Outside the lock: a notify can activate clips, and Activate binds cues.
		for (const auto relay : relays) {
			const auto event = bow_string_notify(relay);
			const bool consumed = pc->NotifyAnimationGraph(event);
			logger::debug("SH2 art: bow string {} at {:.3f}s -> {}", event, clip_time, consumed);
		}
	}

	void observe_graph_event(RE::Actor*, const RE::BSFixedString& a_tag)
	{
		const std::string_view tag{ a_tag.c_str() ? a_tag.c_str() : "" };
		const auto kind = static_cast<AbilityLatch>(latch.load(std::memory_order_relaxed));
		if (is_active() && is_ability_latch_event(kind, tag)) {
			// Any latch annotation is proof the clip is still talking to us, so it pushes the
			// deadline out. See `art_deadline.h`.
			last_signal_ms.store(UnpausedClock::now_ms(), std::memory_order_relaxed);
			if (kind == AbilityLatch::hitFrame) {
				// Wait for the LAST hit, not the first. A multi-hit art that opens on hit 1 is
				// cancellable through the rest of its own combo.
				// Read from the clip's own time where it is known, because hit frames that share a
				// game frame arrive as one and a count can then never complete.
				const int total = latch_hit_total.load(std::memory_order_relaxed);
				const int seen = latch_hits_seen.fetch_add(1, std::memory_order_relaxed) + 1;
				const float clip_time = ClipTranslationDriver::art_clip_time();
				const float last_hit = latch_last_hit_time.load(std::memory_order_relaxed);
				const bool opens = clip_time >= 0.0f && last_hit >= 0.0f
					? ability_latch_opens_at(clip_time, last_hit)
					: ability_latch_opens_on_hit(total, seen);
				if (!opens) {
					logger::debug("SH2 art: hit {} of {} at {:.3f}s -- latch still shut", seen, total, clip_time);
					return;
				}
				logger::debug("SH2 art: latch open (hit {} of {} at {:.3f}s, last hit frame {:.3f}s)",
					seen, total, clip_time, last_hit);
				latch_is_open.store(true, std::memory_order_relaxed);
				return;
			}
			latch_is_open.store(true, std::memory_order_relaxed);
			logger::debug("SH2 art: latch open ({})", tag);
		}
		const bool graph_lost = is_active() && is_ability_graph_lost_event(tag);
		if (tag == "SH2_ArtExit"sv ||
			graph_lost ||
			(is_active() && latch_is_open.load(std::memory_order_relaxed) &&
				(tag == "MCO_AttackEnterNotify"sv || tag == "MCO_AttackInitiate"sv))) {
			if (graph_lost) {
				logger::info(
					"SH2 art: graph left without SH2_ArtExit ({}); releasing the Ability latch",
					tag);
			}
			on_exit(tag);
		}
	}

	bool is_active()
	{
		return state_active.load(std::memory_order_acquire);
	}

	float active_damage_mult()
	{
		return damage_mult_snapshot.load(std::memory_order_relaxed);
	}

	bool clip_is_active()
	{
		return clip_generator_active.load(std::memory_order_relaxed);
	}

	void observe_clip_activate()
	{
		clip_generator_active.store(true, std::memory_order_relaxed);
	}

	void observe_clip_deactivate()
	{
		clip_generator_active.store(false, std::memory_order_relaxed);
		// Drop the billed HKX so a later Activate of the same file is a new start.
		last_bound_animation.store(nullptr, std::memory_order_relaxed);
	}

	bool latch_open()
	{
		return latch_is_open.load(std::memory_order_relaxed);
	}

	bool should_trace_graph_events()
	{
		if (is_active()) {
			return true;
		}
		int remaining = trace_budget.load(std::memory_order_relaxed);
		while (remaining > 0) {
			if (trace_budget.compare_exchange_weak(remaining, remaining - 1, std::memory_order_relaxed)) {
				return true;
			}
		}
		return false;
	}

	void cancel(RE::PlayerCharacter* pc)
	{
		if (is_active()) {
			trace_budget.store(post_exit_trace_events, std::memory_order_relaxed);
		}
		send_exit(pc);
		on_exit("SH2_ArtExit"sv);
	}

	void try_teardown_leftover_at_latch(RE::PlayerCharacter* pc)
	{
		if (!should_teardown_leftover_clip_at_latch(
				clip_is_active(), is_active(), latch_open())) {
			return;
		}
		send_exit(pc);
		// Keep the follow-up selector: OAR reads it on the next Activate. Full
		// cancel() would zero it and leave ArtStart selecting nothing.
		on_exit("SH2_ArtExit"sv, false);
	}

	void finish(RE::PlayerCharacter* pc)
	{
		send_exit(pc);
		on_exit("SH2_ArtExit"sv);
	}

	void poll_deadline(RE::PlayerCharacter* pc)
	{
		if (!is_active()) {
			return;
		}
		const double last_signal = last_signal_ms.load(std::memory_order_relaxed);
		const double now = UnpausedClock::now_ms();
		if (!art_deadline_passed(last_signal, now)) {
			return;
		}
		// Warn, not debug: reaching this means an annotation the art was waiting on never
		// arrived, and without the cancel the whole hotbar stays refused for the session.
		const double started = started_ms.load(std::memory_order_relaxed);
		logger::warn("SH2 art: deadline expired {:.1f}s after the last annotation ({:.1f}s into the "
					 "art) with the latch {} -- cancelling",
			(now - last_signal) / 1000.0, (now - started) / 1000.0, latch_open() ? "open" : "shut");
		cancel(pc);
	}

	void reset_session()
	{
		state_active.store(false, std::memory_order_release);
		damage_mult_snapshot.store(kAbilityDamageMultDefault, std::memory_order_relaxed);
		latch_is_open.store(false, std::memory_order_relaxed);
		latch.store(static_cast<int>(AbilityLatch::artExit), std::memory_order_relaxed);
		latch_last_hit_time.store(-1.0f, std::memory_order_relaxed);
		started_ms.store(0.0, std::memory_order_relaxed);
		last_signal_ms.store(0.0, std::memory_order_relaxed);
		trace_budget.store(0, std::memory_order_relaxed);
		clip_bound_this_start.store(false, std::memory_order_relaxed);
		clip_generator_active.store(false, std::memory_order_relaxed);
		last_bound_animation.store(nullptr, std::memory_order_relaxed);
		activating_animation.store(nullptr, std::memory_order_relaxed);
		bow_string_drawn.store(false, std::memory_order_relaxed);
		clear_bow_cues();
		{
			std::lock_guard lock{ bow_cue_mutex };
			arrow_hit_armed = {};
		}
		GameData::reset_art_selector();
		ClipTranslationDriver::reset();
	}
}
