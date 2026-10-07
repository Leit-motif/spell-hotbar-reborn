#pragma once

namespace SpellHotbar::casts::ArtDriver {

/**
 * An Ability has started only when SH2_ArtStart was consumed AND SH2_Art_Clip
 * activated during that notify.
 *
 * Observed live: Enrage (F) 138 ms after Holding Thorns' SH2_ArtExit.
 * NotifyAnimationGraph returned true, so the plugin billed selector 21, stamina
 * and cooldown. SH2_Art_Clip never Activated. The Thorns clip kept playing for
 * another 4 s. The same shape hit Shoulder Slam 161 ms after Wind Slice.
 *
 * Named for the clip bind, not for movement commitment, which is a property of
 * the behavior state (its bAnimationDriven plant).
 */
inline constexpr bool ability_start_took_the_clip(bool art_start_consumed, bool shtb_clip_activated)
{
	return art_start_consumed && shtb_clip_activated;
}

/**
 * A leftover SH2_Art_Clip re-Activating the same animation is not this start.
 * First start of a session: previous is null, any real activation counts.
 * Same pointer after that generator Deactivated is a new Ability start.
 */
inline constexpr bool shtb_clip_is_this_start(const void* activating, const void* previous,
	bool generator_deactivated_since_previous = false)
{
	if (activating == nullptr) {
		return false;
	}
	if (activating != previous) {
		return true;
	}
	return generator_deactivated_since_previous;
}

/**
 * Do not notify SH2_ArtStart while SH2_Art_Clip is still Active.
 *
 * Observed live: mashed Holding Thorns at Shoulder Slam's latch. ArtStart was
 * consumed, Slam's generator was still Active, Havok sought leftover Slam to
 * the start (animation and sound), and the clip-activate gate refused Thorns.
 * OAR only picks a new HKX on Activate. Never notify-then-refuse.
 */
inline constexpr bool ability_start_may_notify(bool shtb_clip_active)
{
	return !shtb_clip_active;
}

/**
 * Hold the next Ability on Cast Intent until leftover Deactivates.
 *
 * Early mash while the live Ability's latch is still shut is the existing
 * latch hold, not this one. Latch-open, or leftover after the driver went
 * idle, is the window ArtStart would restart the old clip.
 */
inline constexpr bool should_hold_ability_for_leftover_clip(
	bool shtb_clip_active, bool art_active, bool latch_open)
{
	if (!shtb_clip_active) {
		return false;
	}
	if (art_active && !latch_open) {
		return false;
	}
	return true;
}

/**
 * Tear leftover down at the latch with SH2_ArtExit so the follow-up can bind
 * instead of waiting out the rest of the clip. After the clip's own ArtExit
 * the driver is already idle and a second ArtExit is not consumed.
 */
inline constexpr bool should_teardown_leftover_clip_at_latch(
	bool shtb_clip_active, bool art_active, bool latch_open)
{
	return shtb_clip_active && art_active && latch_open;
}

}  // namespace SpellHotbar::casts::ArtDriver
