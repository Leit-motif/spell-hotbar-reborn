#include "bow_string_relay.h"

#include <cstdlib>
#include <iostream>
#include <vector>

using SpellHotbar::casts::BowStringCue;
using SpellHotbar::casts::BowStringEvent;
using SpellHotbar::casts::bow_string_cue_for;
using SpellHotbar::casts::bow_string_cues_due;
using SpellHotbar::casts::bow_string_drawn_after;
using SpellHotbar::casts::bow_string_needs_reset_on_exit;
using SpellHotbar::casts::bow_string_notify;
using SpellHotbar::casts::bow_string_relay_for;

namespace {

int g_failures = 0;

void expect(bool cond, const char* msg)
{
	if (!cond) {
		std::cerr << "FAIL: " << msg << '\n';
		++g_failures;
	}
}

// Walk a clip's cues frame by frame the way ArtDriver does, and count the notifies it sends.
struct Walk
{
	int draws{ 0 };
	int releases{ 0 };
	bool drawn{ false };
};

Walk walk(const std::vector<BowStringCue>& cues, float duration, float frame = 1.0f / 60.0f)
{
	Walk w;
	std::size_t next = 0;
	for (float t = 0.0f; t <= duration; t += frame) {
		const std::size_t due = bow_string_cues_due(cues, next, t);
		for (; next < due; ++next) {
			const auto relay = bow_string_relay_for(cues[next].event, w.drawn);
			w.draws += relay == BowStringEvent::draw ? 1 : 0;
			w.releases += relay == BowStringEvent::release ? 1 : 0;
			w.drawn = bow_string_drawn_after(w.drawn, relay);
		}
	}
	return w;
}

void annotations_name_their_cue()
{
	expect(bow_string_cue_for("BowDraw") == BowStringEvent::draw, "BowDraw is a draw cue");
	expect(bow_string_cue_for("BowRelease") == BowStringEvent::release, "BowRelease is a release cue");
	constexpr const char* others[] = { "arrowAttach", "arrowRelease", "BowDrawOut", "BowReleaseOut", "bowReset",
		"bowdraw", "SoundPlay.WPNBowNockSD", "arrowInterpreter.dmg=1.0|count=1|spread=0|consume=1", "" };
	for (const auto* tag : others) {
		expect(bow_string_cue_for(tag) == BowStringEvent::none, "only BowDraw and BowRelease are cues");
	}
	expect(bow_string_notify(BowStringEvent::draw) == "BowDraw", "a draw notifies BowDraw");
	expect(bow_string_notify(BowStringEvent::release) == "BowRelease", "a release notifies BowRelease");
	expect(bow_string_notify(BowStringEvent::none).empty(), "no relay notifies nothing");
}

void the_relay_is_edge_triggered()
{
	expect(bow_string_relay_for(BowStringEvent::draw, false) == BowStringEvent::draw, "draw from slack");
	expect(bow_string_relay_for(BowStringEvent::release, true) == BowStringEvent::release, "release from pulled");
	expect(bow_string_relay_for(BowStringEvent::draw, true) == BowStringEvent::none, "no second draw while pulled");
	expect(bow_string_relay_for(BowStringEvent::release, false) == BowStringEvent::none, "no release while slack");
}

void cues_fire_when_the_clip_reaches_them()
{
	const std::vector<BowStringCue> cues{ { 0.0f, BowStringEvent::draw }, { 1.666667f, BowStringEvent::release } };
	expect(bow_string_cues_due(cues, 0, -1.0f) == 0, "no bound clip fires nothing");
	expect(bow_string_cues_due(cues, 0, 0.0f) == 1, "a cue at 0.000 fires on the first frame");
	expect(bow_string_cues_due(cues, 1, 1.6f) == 1, "the release waits for its time");
	expect(bow_string_cues_due(cues, 1, 1.7f) == 2, "the release fires once its time passes");
	expect(bow_string_cues_due(cues, 2, 9.0f) == 2, "nothing fires twice");
}

void a_long_frame_fires_every_cue_it_passed()
{
	// A hitch that jumps the clip from 0.1 s to 2.5 s must still send both cues, in order.
	const std::vector<BowStringCue> cues{ { 0.0f, BowStringEvent::draw }, { 1.9f, BowStringEvent::release },
		{ 2.011111f, BowStringEvent::draw }, { 2.144444f, BowStringEvent::release } };
	expect(bow_string_cues_due(cues, 1, 2.5f) == 4, "one late frame fires the three cues it skipped");
}

void arcane_shot_draws_and_releases_once()
{
	// Annotation times from the packaged Arcane Shot HKX (2.500 s).
	const auto w = walk({ { 0.0f, BowStringEvent::draw }, { 1.666667f, BowStringEvent::release } }, 2.5f);
	expect(w.draws == 1 && w.releases == 1, "Arcane Shot draws and releases the string once");
	expect(!bow_string_needs_reset_on_exit(w.drawn), "Arcane Shot ends slack");
}

void rapid_shot_cycles_three_times_and_ends_slack()
{
	// Rapid Shot v4's cue times (2.833 s): a draw, then three release/draw cycles ending on a release.
	const auto w = walk({ { 0.0f, BowStringEvent::draw }, { 1.9f, BowStringEvent::release },
							{ 2.011111f, BowStringEvent::draw }, { 2.144444f, BowStringEvent::release },
							{ 2.255556f, BowStringEvent::draw }, { 2.388889f, BowStringEvent::release } },
		2.833333f);
	expect(w.draws == 3 && w.releases == 3, "every Rapid Shot draw and release reaches the bow");
	expect(!bow_string_needs_reset_on_exit(w.drawn), "a clip that ends on its release needs no reset");
}

void a_cancel_mid_draw_resets_the_bow()
{
	const auto w = walk({ { 0.0f, BowStringEvent::draw }, { 1.666667f, BowStringEvent::release } }, 1.0f);
	expect(w.drawn && bow_string_needs_reset_on_exit(w.drawn), "an Ability cut before its release resets the bow");
}

}

int main()
{
	annotations_name_their_cue();
	the_relay_is_edge_triggered();
	cues_fire_when_the_clip_reaches_them();
	a_long_frame_fires_every_cue_it_passed();
	arcane_shot_draws_and_releases_once();
	rapid_shot_cycles_three_times_and_ends_slack();
	a_cancel_mid_draw_resets_the_bow();
	if (g_failures != 0) {
		std::cerr << g_failures << " failure(s)\n";
		return EXIT_FAILURE;
	}
	std::cout << "bow_string_relay_test: all passed\n";
	return EXIT_SUCCESS;
}
