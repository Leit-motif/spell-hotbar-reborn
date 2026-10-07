#pragma once

namespace SpellHotbar::casts::ArtDriver {

/**
 * The art state's own deadline, in gameplay milliseconds.
 *
 * `CastingInstanceWeaponArt::update()` carries an eight-second check of its own, but it compares
 * the instance's cast timer, which starts at zero for an art and only counts down, so it never
 * fires. Without this deadline the art's only exit is a graph event.
 *
 * Measured: flipping to first person 200 ms into a weapon art left the state live for 185 seconds
 * and counting. The clip's hit-frame annotations live on the third-person graph, so once the
 * camera changes they never reach the event sink, the latch never opens, and
 * `can_start_new_cast()` stays false. Every hotbar slot is refused and the Ability latch eats
 * attacks for the rest of the session; only a save load clears it.
 *
 * The camera is not the only way an annotation can go missing -- a bad clip, a failed OAR pick, a
 * mod that swallows the event all end the same way -- so the deadline is deliberately about the
 * annotation never arriving rather than about the camera. The camera refusal in `try_start_art`
 * is the other half and stops the common case earlier.
 *
 * The deadline must not be anchored to the art's START. Nothing else caps a running art, and
 * arts routinely outlive eight seconds. Measured live on Blood Flurry (art 4, five hit frames):
 * the graph sent its own `SH2_ArtExit` at 8.957 s, while a start-anchored deadline cancelled at
 * 8.031 s and cut every ordinary third-person swing about a second early.
 *
 * So the clock runs from the LAST annotation received, not from the start. An art still receiving
 * hit frames is an art whose annotations are arriving, which is the exact condition the deadline
 * exists to detect the absence of. The measured softlock is unaffected: the camera flip stopped
 * every annotation at once, no `hit N of 5` ever arrived, and the last signal stayed the start
 * stamp.
 */
inline constexpr double art_deadline_ms = 8000.0;

/** Whether a live art has gone `art_deadline_ms` without an annotation. `last_signal_ms` is the
 *  stamp of the art's start or of the most recent latch annotation, whichever is later; 0 means
 *  "not running": the same zero-is-absent sentinel `UnpausedClock` documents, which is why its
 *  origin is 1.0 rather than 0.0 and why a real stamp can never read as absent. */
inline constexpr bool art_deadline_passed(double last_signal_ms, double now_ms)
{
    return last_signal_ms > 0.0 && (now_ms - last_signal_ms) > art_deadline_ms;
}

}  // namespace SpellHotbar::casts::ArtDriver
