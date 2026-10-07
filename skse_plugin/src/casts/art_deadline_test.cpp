#include "art_deadline.h"

#include <cstdlib>
#include <iostream>

using SpellHotbar::casts::ArtDriver::art_deadline_ms;
using SpellHotbar::casts::ArtDriver::art_deadline_passed;

namespace {

int g_failures = 0;

void expect(bool cond, const char* msg)
{
    if (!cond) {
        std::cerr << "FAIL: " << msg << '\n';
        ++g_failures;
    }
}

void a_stopped_art_never_expires()
{
    // 0 is "not running". Without this the deadline would fire on every idle frame, because
    // `now - 0` is enormous once the session has been up for eight seconds.
    expect(!art_deadline_passed(0.0, 1.0), "a stopped art does not expire at the origin");
    expect(!art_deadline_passed(0.0, 9'999'999.0), "nor after a long session");
}

void an_art_inside_its_deadline_is_left_alone()
{
    expect(!art_deadline_passed(1000.0, 1000.0), "an art that just started is not expired");
    expect(!art_deadline_passed(1000.0, 1000.0 + art_deadline_ms - 1.0), "one ms short is not expired");
    expect(!art_deadline_passed(1000.0, 1000.0 + art_deadline_ms), "exactly the cap is not yet expired");
}

void an_art_past_its_deadline_expires()
{
    expect(art_deadline_passed(1000.0, 1000.0 + art_deadline_ms + 1.0), "one ms past the cap expires");
    // The measured softlock: 185 s with the latch shut.
    expect(art_deadline_passed(1000.0, 1000.0 + 185'000.0), "the measured 185s softlock expires");
}

void the_deadline_is_eight_seconds()
{
    // The same figure as the cast state's own watchdog.
    expect(art_deadline_ms == 8000.0, "the deadline is eight seconds");
}

void a_working_art_is_not_cut_by_its_own_length()
{
    // Measured live. Blood Flurry (art 4) runs 8.957 s: its last hit-frame annotation lands at
    // about 7.7 s and the graph's own `SH2_ArtExit` at 8.957 s. Anchored to the START this
    // expires at 8.031 s and cuts a healthy swing short; anchored to the LAST ANNOTATION it
    // does not.
    constexpr double start = 1000.0;
    constexpr double last_hit = start + 7'700.0;
    constexpr double graph_exit = start + 8'957.0;
    expect(art_deadline_passed(start, graph_exit), "start-anchored, Blood Flurry would be cut");
    expect(!art_deadline_passed(last_hit, graph_exit), "annotation-anchored, it runs to its own exit");
}

void an_annotation_pushes_the_deadline_out()
{
    // Each annotation restamps the clock, so a long multi-hit art survives as long as its clip
    // keeps talking, and the countdown restarts the moment the annotations stop.
    constexpr double late_signal = 60'000.0;
    expect(!art_deadline_passed(late_signal, late_signal + 1.0), "a fresh annotation resets the clock");
    expect(!art_deadline_passed(late_signal, late_signal + art_deadline_ms),
        "and buys another full deadline from that point");
    expect(art_deadline_passed(late_signal, late_signal + art_deadline_ms + 1.0),
        "silence after the last annotation still expires");
}

}  // namespace

int main()
{
    a_stopped_art_never_expires();
    an_art_inside_its_deadline_is_left_alone();
    an_art_past_its_deadline_expires();
    the_deadline_is_eight_seconds();
    a_working_art_is_not_cut_by_its_own_length();
    an_annotation_pushes_the_deadline_out();

    if (g_failures > 0) {
        std::cerr << g_failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "art_deadline_test passed\n";
    return EXIT_SUCCESS;
}
