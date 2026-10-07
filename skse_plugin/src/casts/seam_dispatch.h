#pragma once

#include <string_view>

#include "attack_seam.h"

namespace RE {
	class PlayerCharacter;
}

namespace SpellHotbar::casts {

// What the caller must do with the event or idle it was about to pass on.
enum class SeamDisposition {
	// Forward to the original. The seam either did nothing or ran a teardown first; either way
	// the attack itself is not this seam's decision.
	forward,
	// Do not forward. The single case where the seam swallows: an Ability owns the swing and its
	// latch has not opened, which is the send-side equivalent of the input hook capturing a press.
	capture,
};

/*
 * The body of the attack seam, split from the hook that decodes the event.
 *
 * The `NotifyAnimationGraph` hook is the only entry point: BFCO's idles arrive on the same
 * virtual (see `attack_seam_hook.h`), so no second hook at `AIProcess::SetupSpecialIdle` is
 * needed. The split stays anyway: the hook decodes a name, this decides what the cut does, and a
 * future source that really does need its own entry point runs the identical teardown.
 *
 * `a_source` is a label for the log line, never a behaviour switch: the seam's whole point is
 * that the cut does not depend on which mechanism the attack arrived by.
 *
 * Re-entrancy is handled here for the same reason. The teardown sends notifies of its own
 * (`SH2_CastExit`, the Ability's exit) which travel the same hook, so a single thread-local guard
 * is what makes "a cut never re-enters the seam" a property of the code rather than of the
 * current event names.
 */
[[nodiscard]] SeamDisposition apply_attack_seam(
	RE::PlayerCharacter* a_player, std::string_view a_name, AttackSeamKind a_kind,
	const char* a_source);

// True while a seam teardown is running on this thread. Both hooks forward unread when set.
[[nodiscard]] bool seam_cut_in_progress() noexcept;

/*
 * The seam's second duty: after the hook forwards an attack, tell the cast driver a swing has
 * started. `a_accepted` is the original's own return -- a graph that refused the event
 * (staggered, sheathed, mid-recovery) plays no swing, and a tracker opened for one would hold a
 * press until its cap for a swing that never ends. Called after the forward so the answer is the
 * graph's, not a guess.
 */
void note_attack_forwarded(RE::PlayerCharacter* a_player, AttackSeamKind a_kind, bool a_accepted);

/*
 * The deferred forward.
 *
 * A cut whose `SH2_CastExit` the graph accepted has a transition still to land -- 14 ms of it,
 * measured 3 of 3 cells. Forwarding the attack inside that window puts the graph's return to
 * magic-ready INSIDE a swing that has already started, and the swing dies without ever reaching
 * `MCO_WinOpen`. So the cut captures the attack instead, and these three run the wait:
 *
 *   - `apply_attack_seam` arms it and answers `capture`,
 *   - `note_cast_exit_landed` is called from the cast driver's event sink when the graph raises
 *     the ready tags that say the transition is done,
 *   - `poll_deferred_attack` runs once per unpaused frame and re-sends the attack, which travels
 *     the seam hook exactly as the original did -- the cast state is gone by then, so it forwards
 *     and opens the swing tracker through the ordinary path.
 *
 * `kDeferredAttackCapMs` is the backstop for a graph that consumes the exit and raises no ready
 * tag: past it the attack is sent regardless. Nothing here ever swallows the attack.
 */
void note_cast_exit_landed();
void poll_deferred_attack(RE::PlayerCharacter* a_player);

// Drop any attack still waiting. A save load or a session reset invalidates the swing it was
// going to start; sending it late would swing on a character who has just arrived.
void clear_deferred_attack();

}  // namespace SpellHotbar::casts
