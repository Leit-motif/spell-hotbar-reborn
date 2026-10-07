#pragma once
#include "../bar/hotbar.h"
#include "../input/input.h"

/**
 * One last-wins Cast Intent.
 *
 * Spell Hotbar 2 owns the payload — slot, type, and FormID or Ability id — and
 * revalidates it once on fire. Two clocks decide the legal frame:
 *
 * - This mod's latch, when the player is in a Driver Cast or Ability we started, or in a
 *   swing the send-side seam is tracking (someone else's MCO or BFCO attack).
 * - ShoutMCO, for a real shout and anything else the local latch does not hold.
 *
 * ShoutMCO is optional. With no API, a press the local latch does not hold is refused
 * (dead press); the local latch still buffers.
 *
 * Everything here runs on the main thread: the input hook, the papyrus `castSlot`
 * task, ShoutMCO's callback, and the game-loop poll. No locking, no atomics.
 */
namespace SpellHotbar::casts::CastIntent {

	/**
	 * The read-only compatibility status, reported once and never as a popup.
	 */
	enum class Status {
		unavailable,  // no ShoutMCO, or it does not export the API
		active,       // negotiated and taking intents
		incompatible  // present, but implements no major version we know
	};

	/**
	 * Resolve ShoutMCO's export and negotiate the API version. Call once, after every SKSE
	 * plugin DLL is loaded (`kPostLoad`). Logs the resulting status one time.
	 */
	void negotiate();

	Status get_status();

	/**
	 * The status as one word: `active`, `unavailable`, `incompatible`.
	 */
	const char* status_name();

	/**
	 * Offer the pressed slot as a Cast Intent.
	 *
	 * Snapshots the slot's current assignment. Returns true when the press is retained —
	 * either locally until this mod's latch opens, or by ShoutMCO until its release
	 * callback. The caller reports that as accepted rather than failed. Any other
	 * answer — no API for a ShoutMCO clock, the request rejected, or nothing to wait
	 * for — returns false, and the caller reports the refusal it already had.
	 */
	bool offer(size_t slot, const Input::KeyBind& keybind);

	/**
	 * True while a retained payload is being fired. Start paths must not offer
	 * again, or a still-true IsShouting bit would drop a hotbar shout on release.
	 */
	bool is_firing();

	/**
	 * True while this mod retains a payload that has not yet fired. A hotbar shout
	 * must not inject a `"Shout"` ButtonEvent until that payload is released.
	 */
	bool is_pending();

	/**
	 * True when our Driver Cast or Ability is live and its latch is still closed. Start paths
	 * check this BEFORE attempting a cast: behind our own clip there is nothing to try.
	 */
	bool our_latch_is_closed();

	/**
	 * Everything our_latch_is_closed() says, plus someone else's swing being open on the cast
	 * driver's tracker. This is the retain-and-release predicate: offer() retains on it and
	 * poll_local_release() releases when it turns false.
	 */
	bool should_retain_now();

	/**
	 * True when a press should be held BEFORE it is tried, because someone else's swing is open
	 * and has not reached its cancel window.
	 *
	 * The graph's refusal cannot serve as the signal here: `AttackState` names `shtb`, so the
	 * graph accepts a mid-swing cast, and a press tried first cuts the swing on whatever frame it
	 * arrived on. Start paths check this alongside our_latch_is_closed().
	 */
	bool should_hold_for_foreign_swing();

	/**
	 * If a locally retained payload's latch is now open (or our shtb state has ended),
	 * fire it once. No-op for a ShoutMCO-owned handle.
	 */
	void poll_local_release();

	/**
	 * Withdraw a pending intent, if this mod owns one, and drop the payload immediately. Safe to
	 * call at any time; a stale or absent intent is not an error.
	 */
	void cancel();

	/**
	 * Keep this Ability press until leftover SH2_Art_Clip Deactivates.
	 * Does not bill. Works during a release attempt so a latch fire that still sees
	 * leftover can put the payload back instead of flashing refuse.
	 */
	bool hold_for_leftover_clip(size_t slot, const Input::KeyBind& keybind);

	/**
	 * True while a leftover-clip hold is the reason the press is pending. The slot
	 * must not flash refuse: this is a wait, not a no.
	 */
	bool holding_for_leftover_clip();
}
