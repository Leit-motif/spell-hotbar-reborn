#include "casts/art_deadline.h"

#include <cstdlib>
#include <iostream>

namespace {
    using SpellHotbar::casts::ArtDriver::art_deadline_ms;
    using SpellHotbar::casts::ArtDriver::ArtInstanceCap;

    void require(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    constexpr double start_stamp = 1000.0;
    constexpr double frame_ms = 100.0;

    // Advance one frame at a time until the cap fires; return the silent time at which it did.
    double fire_time(ArtInstanceCap& cap, double signal_stamp, double already_ms = 0.0)
    {
        double t = already_ms;
        while (t < 60'000.0) {
            t += frame_ms;
            if (cap.advance(frame_ms, signal_stamp)) {
                return t;
            }
        }
        return -1.0;
    }

    void an_art_that_hears_nothing_is_retired_at_eight_seconds()
    {
        ArtInstanceCap cap;
        for (double t = frame_ms; t <= art_deadline_ms; t += frame_ms) {
            require(!cap.advance(frame_ms, start_stamp), "the cap must not fire before eight seconds");
        }
        require(cap.advance(frame_ms, start_stamp), "the cap must fire on the first frame past eight seconds");
    }

    void the_cap_fires_at_eight_seconds_whatever_the_frame_rate()
    {
        // 60 fps in float deltas, as the game hands them over.
        ArtInstanceCap cap;
        constexpr float delta_s = 1.0f / 60.0f;
        double elapsed_ms = 0.0;
        bool fired = false;
        while (!fired && elapsed_ms < 60'000.0) {
            const double delta_ms = static_cast<double>(delta_s) * 1000.0;
            elapsed_ms += delta_ms;
            fired = cap.advance(delta_ms, start_stamp);
        }
        require(fired, "the cap must fire at 60 fps");
        require(elapsed_ms > art_deadline_ms, "not before eight seconds");
        require(elapsed_ms <= art_deadline_ms + 17.0, "within one frame of eight seconds");
    }

    void a_healthy_long_art_runs_to_its_own_exit()
    {
        // Measured live: Blood Flurry's last hit-frame annotation lands at about 7.7 s and its
        // graph exit at 8.957 s. A start-anchored cap would cut it at 8.0 s.
        ArtInstanceCap cap;
        constexpr double last_hit_at = 7'700.0;
        constexpr double graph_exit_at = 8'957.0;
        double t = 0.0;
        for (; t + frame_ms <= last_hit_at; t += frame_ms) {
            require(!cap.advance(frame_ms, start_stamp), "Blood Flurry must not be cut before its last hit");
        }
        const double last_hit_stamp = start_stamp + last_hit_at;
        for (; t + frame_ms <= graph_exit_at; t += frame_ms) {
            require(!cap.advance(frame_ms, last_hit_stamp), "Blood Flurry must reach its own exit");
        }
    }

    void silence_after_the_last_annotation_still_expires()
    {
        ArtInstanceCap cap;
        constexpr double annotated_at = 3'000.0;
        double t = 0.0;
        for (; t < annotated_at; t += frame_ms) {
            cap.advance(frame_ms, start_stamp);
        }
        const double fired_at = fire_time(cap, start_stamp + annotated_at, t);
        require(fired_at > annotated_at + art_deadline_ms, "the restarted cap must not fire early");
        require(fired_at <= annotated_at + art_deadline_ms + frame_ms, "the restarted cap must still fire");
    }

    void eight_seconds_exactly_is_not_yet_expired()
    {
        // The same boundary as `art_deadline_passed`: strictly past the cap.
        ArtInstanceCap cap;
        require(!cap.advance(art_deadline_ms, start_stamp), "exactly eight seconds is not past the cap");
        require(cap.advance(1.0, start_stamp), "one ms more is");
    }
}

int main()
{
    an_art_that_hears_nothing_is_retired_at_eight_seconds();
    the_cap_fires_at_eight_seconds_whatever_the_frame_rate();
    a_healthy_long_art_runs_to_its_own_exit();
    silence_after_the_last_annotation_still_expires();
    eight_seconds_exactly_is_not_yet_expired();
    std::cout << "art_instance_cap_tests passed\n";
    return EXIT_SUCCESS;
}
