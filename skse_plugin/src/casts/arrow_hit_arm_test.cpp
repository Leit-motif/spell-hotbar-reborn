#include "arrow_hit_arm.h"

#include <cstdlib>
#include <iostream>
#include <vector>

using SpellHotbar::casts::ArrowHitCue;
using SpellHotbar::casts::ArrowHitEffect;
using SpellHotbar::casts::arrow_hit_arm;
using SpellHotbar::casts::arrow_hit_cues_due;
using SpellHotbar::casts::arrow_hit_effect_at;
using SpellHotbar::casts::arrow_hit_effect_for;
using SpellHotbar::casts::kArrowHitWindowMs;

namespace {

int g_failures = 0;

void expect(bool cond, const char* msg)
{
	if (!cond) {
		std::cerr << "FAIL: " << msg << '\n';
		++g_failures;
	}
}

void only_the_knockdown_annotation_arms()
{
	expect(arrow_hit_effect_for("SH2_ArrowKnockdown") == ArrowHitEffect::knockdown, "SH2_ArrowKnockdown arms a knockdown");
	constexpr const char* others[] = { "arrowInterpreter.dmg=1.5|count=1|spread=0|consume=1", "arrowDetach",
		"BowRelease", "sh2_arrowknockdown", "SH2_ArrowKnockdown.x", "" };
	for (const auto* tag : others) {
		expect(arrow_hit_effect_for(tag) == ArrowHitEffect::none, "nothing else arms an arrow effect");
	}
}

void a_hit_inside_the_window_takes_the_effect()
{
	const auto arm = arrow_hit_arm(ArrowHitEffect::knockdown, 1000.0);
	expect(arrow_hit_effect_at(arm, 1000.0) == ArrowHitEffect::knockdown, "a point-blank hit knocks down");
	expect(arrow_hit_effect_at(arm, 1000.0 + kArrowHitWindowMs) == ArrowHitEffect::knockdown,
		"a hit on the last millisecond still counts");
}

void a_late_hit_or_no_arm_takes_nothing()
{
	const auto arm = arrow_hit_arm(ArrowHitEffect::knockdown, 1000.0);
	expect(arrow_hit_effect_at(arm, 1000.0 + kArrowHitWindowMs + 1.0) == ArrowHitEffect::none,
		"a missed shot does not knock down the next ordinary hit");
	expect(arrow_hit_effect_at({}, 1000.0) == ArrowHitEffect::none, "an ordinary shot arms nothing");
}

void the_cue_fires_when_the_clip_reaches_the_shot()
{
	// Blunt Shot's shot sits at 2.667 s.
	const std::vector<ArrowHitCue> cues{ { 2.666667f, ArrowHitEffect::knockdown } };
	expect(arrow_hit_cues_due(cues, 0, -1.0f) == 0, "no bound clip arms nothing");
	expect(arrow_hit_cues_due(cues, 0, 2.6f) == 0, "the draw does not arm");
	expect(arrow_hit_cues_due(cues, 0, 2.7f) == 1, "the shot arms");
	expect(arrow_hit_cues_due(cues, 1, 3.4f) == 1, "the shot arms once");
}

}

int main()
{
	only_the_knockdown_annotation_arms();
	a_hit_inside_the_window_takes_the_effect();
	a_late_hit_or_no_arm_takes_nothing();
	the_cue_fires_when_the_clip_reaches_the_shot();
	if (g_failures != 0) {
		std::cerr << g_failures << " failure(s)\n";
		return EXIT_FAILURE;
	}
	std::cout << "arrow_hit_arm_test: all passed\n";
	return EXIT_SUCCESS;
}
