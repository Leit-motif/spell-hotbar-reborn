#include "seam_dispatch.h"

#include <mutex>
#include <string>

#include "art_driver.h"
#include "casting_controller.h"
#include "msco_cast_driver.h"
#include "combo_cache.h"
#include "unpaused_clock.h"
#include "../logger/logger.h"

namespace SpellHotbar::casts {

	namespace {

		// See the header. One guard for both entry points, so a teardown's own notifies -- and an
		// idle-driven cut landing inside a virtual-driven one -- are forwarded unread.
		thread_local bool t_in_seam_cut = false;

		// True only while `poll_deferred_attack` is re-sending an attack it held. The re-send
		// DOES have to travel the seam -- that is how the swing tracker opens -- so this cannot
		// be folded into `t_in_seam_cut`, which skips the seam entirely. It marks the one thing
		// the re-send must not do; see the clear below.
		thread_local bool t_in_deferred_resend = false;

		struct SeamCutGuard
		{
			SeamCutGuard() { t_in_seam_cut = true; }
			~SeamCutGuard() { t_in_seam_cut = false; }
			SeamCutGuard(const SeamCutGuard&) = delete;
			SeamCutGuard& operator=(const SeamCutGuard&) = delete;
		};

		// Same RAII for the same reason. A leaked flag here is not recoverable: it would stay
		// true for the life of the thread and skip `clear_teardown_stop()` on every later attack,
		// so a stale teardown pass could absorb a later swing's real stop -- the failure that
		// clear exists to prevent, arriving by another door, and just as silent.
		struct DeferredResendGuard
		{
			DeferredResendGuard() { t_in_deferred_resend = true; }
			~DeferredResendGuard() { t_in_deferred_resend = false; }
			DeferredResendGuard(const DeferredResendGuard&) = delete;
			DeferredResendGuard& operator=(const DeferredResendGuard&) = delete;
		};

		// The one attack waiting for a cut's exit transition to land.
		// Armed on whichever thread sent the attack, flagged from the animation thread's event
		// sink, and spent on the main thread's frame poll, so the whole record takes one mutex --
		// the same shape `McoSwingTracker` uses for the same reason. At most one exists: an
		// attack arriving while one is armed replaces it, because the player pressed twice and
		// the second press is the current one.
		struct DeferredAttack {
			RE::BSFixedString name;
			bool armed = false;
			bool exit_landed = false;
			double armed_at_ms = 0.0;
		};

		// A cut that ran before its attack reached the seam (an Action's, the input hook's), still waiting for the
		// ready triple. Under the same mutex as the held attack it turns into.
		struct EarlierCut {
			bool waiting = false;
			double cut_at_ms = 0.0;
		};

		std::mutex g_deferred_mutex;
		DeferredAttack g_deferred;
		EarlierCut g_earlier_cut;

		// Caller holds g_deferred_mutex. `armed_at` is the cut's time, not the hold's, so the cap
		// bounds the whole wait from the cut on both paths.
		void hold_attack_locked(std::string_view a_name, double a_armed_at_ms)
		{
			// Through a std::string: a string_view carries no guarantee of a terminator, and this
			// one is about to become a BSFixedString.
			const std::string owned{ a_name };
			g_deferred = DeferredAttack{ RE::BSFixedString(owned.c_str()), true, false, a_armed_at_ms };
		}

		// Hold this attack on the marker an earlier cut left, if one stands. Otherwise the marker is
		// spent: landed, lapsed, or never set, the first new attack to get here ends it.
		bool hold_for_earlier_cut(std::string_view a_name, const char* a_source)
		{
			const std::lock_guard lock{ g_deferred_mutex };
			if (seam_holds_for_earlier_cut(t_in_deferred_resend, g_earlier_cut.waiting,
					UnpausedClock::now_ms(), g_earlier_cut.cut_at_ms, kDeferredAttackCapMs)) {
				hold_attack_locked(a_name, g_earlier_cut.cut_at_ms);
				g_earlier_cut = EarlierCut{};
				logger::info("SH2 seam: holding \"{}\" for an earlier cut's transition (source={})",
					a_name, a_source);
				return true;
			}
			if (!t_in_deferred_resend) {
				g_earlier_cut = EarlierCut{};
			}
			return false;
		}

	}

	bool seam_cut_in_progress() noexcept
	{
		return t_in_seam_cut;
	}

	SeamDisposition apply_attack_seam(
		RE::PlayerCharacter* a_player, std::string_view a_name, AttackSeamKind a_kind,
		const char* a_source)
	{
		if (a_player == nullptr || a_kind == AttackSeamKind::none || t_in_seam_cut) {
			return SeamDisposition::forward;
		}

		const SeamCutGuard guard;

		// A caller (an Action, the input hook) already cut the cast for THIS attack, a frame before
		// its event got here.
		// Hold it for the ready triple the same as the cut below would. Ahead of the teardown-pass
		// clear: that pass is this cut's own, armed for the stop still on its way.
		if (hold_for_earlier_cut(a_name, a_source)) {
			return SeamDisposition::capture;
		}

		// A new attack has reached the seam, so any teardown pass still armed belongs to an
		// earlier cut whose stop never arrived. Spend it here, before the cut below arms a fresh
		// one, so a stale flag can never absorb THIS swing's real attackStop.
		//
		// A deferred re-send is the exception. It is not a new press; it is the same press
		// arriving late, and the pass it would spend is its OWN cut's. Measured on an Iron Rapier
		// the teardown stop leads both ready tags in the same millisecond, so the pass is already
		// consumed by then and clearing it is a no-op -- but that is one weapon's ordering. On a
		// graph whose stop lands after the ready tag, or after the deferred forward's 200 ms cap
		// has already fired the re-send, clearing here would disarm the pass and let a late
		// teardown stop close the swing that was just opened -- the very close the pass exists
		// to absorb, and silent, because no `teardown attackStop` line would be written to
		// reveal it. The pass's own 300 ms cap still bounds the pass we leave standing.
		if (!t_in_deferred_resend) {
			MscoCastDriver::clear_teardown_stop();
		}

		// The cast cut, read here on the send side rather than on the key. Both halves of the
		// cuttable span, exactly as the input hook read them: `is_committed_cast_holding_graph`
		// covers the commitment point to GCD expiry, `is_cuttable_follow_through` covers
		// retirement to clip end. A cast still CHARGING is in neither, so an attack then does
		// not cut it.
		//
		// The cut runs BEFORE the caller forwards, so the state is gone by the time the graph is
		// asked to play the swing. The seam ends SH2's state; it does not decide whether the
		// attack happens.
		if (CastingController::is_committed_cast_holding_graph() ||
			CastingController::is_cuttable_follow_through()) {
			const auto cut = CastingController::cut_committed_cast_for_attack(a_player);
			if (cut == CastingController::CastCut::none) {
				// Another thread's cut (an Action's, measured in the same millisecond) ended the
				// state while this one waited its turn, and left the marker this one checked too
				// early above. Ask again now that it stands.
				if (hold_for_earlier_cut(a_name, a_source)) {
					return SeamDisposition::capture;
				}
				return SeamDisposition::forward;
			}
			logger::info("SH2 seam: cut committed cast for \"{}\" (source={})", a_name, a_source);
			// A consumed exit has a transition still to land, and an attack forwarded into it
			// dies. Hold the attack here and send it from the frame poll once the graph says the
			// transition is done. A refused exit has nothing to wait for and forwards at once.
			if (cut == CastingController::CastCut::cut_exit_pending) {
				const std::lock_guard lock{ g_deferred_mutex };
				hold_attack_locked(a_name, UnpausedClock::now_ms());
				// This hold spends the marker the cut just left, as the marker's own hold does.
				// Left standing, it held BFCO's follow-up idle (`BFCOAttackstart_1`, sent a moment
				// after the power attack it belongs to) in the same slot, which replaced the power
				// attack: measured live, that rep re-sent only the follow-up and swung nothing.
				g_earlier_cut = EarlierCut{};
				logger::info("SH2 seam: holding \"{}\" for the cast exit's transition", a_name);
				return SeamDisposition::capture;
			}
			return SeamDisposition::forward;
		}

		// The concentration chain-out. A channel's state loops for the whole hold, so there is
		// no clip to cut short; what an attack has to end is the channel itself. Both predicate terms are true here by construction: the
		// window is open (checked on the line above) and this IS an attack.
		if (CastingController::is_channel_chainable() &&
			should_cut_channel_for_attack(true, true)) {
			CastingController::cut_channel_for_attack(a_player);
			logger::info("SH2 seam: cut channel for \"{}\" (source={})", a_name, a_source);
			return SeamDisposition::forward;
		}

		// The Ability latch. The one branch that does NOT forward: before the latch opens, the
		// Ability owns the swing, and the input hook expressed that by capturing the press. The
		// send-side equivalent of a captured press is an unsent event -- or, from the idle hook,
		// an idle that never plays.
		if (ArtDriver::is_active()) {
			const bool latch_open = ArtDriver::latch_open();
			if (should_capture_attack_during_ability(true, latch_open, true)) {
				logger::info("SH2 seam: captured \"{}\" before the Ability latch (source={})",
					a_name, a_source);
				return SeamDisposition::capture;
			}
			if (should_cut_ability_for_attack(true, latch_open, true)) {
				ArtDriver::cancel(a_player);
				logger::info("SH2 seam: cut Ability for \"{}\" (source={})", a_name, a_source);
			}
		}

		return SeamDisposition::forward;
	}

	void note_attack_forwarded(
		RE::PlayerCharacter* a_player, AttackSeamKind a_kind, bool a_accepted)
	{
		if (!a_accepted || a_player == nullptr) {
			return;
		}
		MscoCastDriver::observe_attack_sent(a_player, a_kind);
	}

	void note_attack_cut_pending()
	{
		const std::lock_guard lock{ g_deferred_mutex };
		g_earlier_cut = EarlierCut{ true, UnpausedClock::now_ms() };
	}

	void note_cast_exit_landed()
	{
		const std::lock_guard lock{ g_deferred_mutex };
		if (g_deferred.armed) {
			g_deferred.exit_landed = true;
		}
		// Landed before the attack arrived: nothing to wait for, so that attack forwards at once.
		g_earlier_cut = EarlierCut{};
	}

	void poll_deferred_attack(RE::PlayerCharacter* a_player)
	{
		RE::BSFixedString name;
		double held = 0.0;
		bool on_cap = false;
		{
			const std::lock_guard lock{ g_deferred_mutex };
			if (!deferred_attack_should_send(g_deferred.armed, g_deferred.exit_landed,
					UnpausedClock::now_ms(), g_deferred.armed_at_ms, kDeferredAttackCapMs)) {
				return;
			}
			name = g_deferred.name;
			held = UnpausedClock::now_ms() - g_deferred.armed_at_ms;
			on_cap = !g_deferred.exit_landed;
			// Cleared BEFORE the send, not after: the send travels the seam hook, and a record
			// still armed there could be replaced or re-read mid-flight.
			g_deferred = DeferredAttack{};
		}
		if (a_player == nullptr) {
			return;
		}
		logger::info("SH2 seam: sending held attack \"{}\" {:.0f}ms after the cut ({})",
			name.c_str() ? name.c_str() : "", held,
			on_cap ? "cap, no ready tag" : "exit transition landed");
		// Through the graph, not around it: this re-enters the seam hook, finds no cast state to
		// cut, forwards, and opens the swing tracker at `note_attack_forwarded` -- the ordinary
		// path, one or two frames later than the press. The flag says which of the seam's jobs
		// that re-entry is exempt from.
		const DeferredResendGuard resend_guard;
		a_player->NotifyAnimationGraph(name);
	}

	bool deferred_attack_armed()
	{
		const std::lock_guard lock{ g_deferred_mutex };
		return g_deferred.armed;
	}

	void clear_deferred_attack()
	{
		const std::lock_guard lock{ g_deferred_mutex };
		g_deferred = DeferredAttack{};
		g_earlier_cut = EarlierCut{};
	}

}  // namespace SpellHotbar::casts
