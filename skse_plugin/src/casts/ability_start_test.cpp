#include "ability_start.h"

#include <cstdlib>
#include <iostream>

using SpellHotbar::casts::ArtDriver::ability_start_may_notify;
using SpellHotbar::casts::ArtDriver::ability_start_took_the_clip;
using SpellHotbar::casts::ArtDriver::should_hold_ability_for_leftover_clip;
using SpellHotbar::casts::ArtDriver::should_teardown_leftover_clip_at_latch;
using SpellHotbar::casts::ArtDriver::shtb_clip_is_this_start;

namespace {

int g_failures = 0;

void expect(bool cond, const char* msg)
{
	if (!cond) {
		std::cerr << "FAIL: " << msg << '\n';
		++g_failures;
	}
}

void a_healthy_ability_start_binds_the_clip_during_artstart()
{
	// 19:01:48.502 Holding Thorns: bind_latch and animmotion bind logged before
	// NotifyAnimationGraph returned true.
	expect(ability_start_took_the_clip(true, true),
		"ArtStart consumed with a fresh SH2_Art_Clip is a started Ability");
}

void a_chained_ability_whose_clip_does_not_activate_has_not_started()
{
	// 19:01:52.748 Enrage (F): ArtStart consumed, no SH2_Art_Clip Activate.
	expect(!ability_start_took_the_clip(true, false),
		"ArtStart consumed without a new clip is not a started Ability");
}

void an_unconsumed_artstart_has_not_started()
{
	expect(!ability_start_took_the_clip(false, false), "unconsumed ArtStart has not started");
	expect(!ability_start_took_the_clip(false, true),
		"a leftover clip without ArtStart is not a started Ability");
}

void a_leftover_clip_reactivating_is_not_this_start()
{
	const int thorns{};
	const int enrage{};
	expect(shtb_clip_is_this_start(&thorns, nullptr), "the first clip of a session is this start");
	expect(!shtb_clip_is_this_start(&thorns, &thorns, false),
		"re-Activating Holding Thorns is not Enrage's start");
	expect(shtb_clip_is_this_start(&enrage, &thorns), "a different animation is this start");
	expect(!shtb_clip_is_this_start(nullptr, &thorns), "a bind with no animation is not this start");
}

void the_same_clip_after_deactivate_is_this_start()
{
	// 16:32:39 Shoulder Slam mashed after the first billed Slam (16:21:15). Same
	// HKX pointer, generator already idle, logged leftover and looped slot 3.
	const int slam{};
	expect(!shtb_clip_is_this_start(&slam, &slam, false),
		"the same animation is leftover while the generator never Deactivated");
	expect(shtb_clip_is_this_start(&slam, &slam, true),
		"the same animation after the generator Deactivated is this start");
}

void artstart_is_not_notified_while_the_clip_is_still_active()
{
	// 20:02 mash: ArtStart into leftover Slam sought it to the start.
	expect(!ability_start_may_notify(true),
		"ArtStart into a still-Active SH2_Art_Clip restarts leftover");
	expect(ability_start_may_notify(false),
		"ArtStart is legal once leftover has Deactivated");
}

void a_latch_mash_holds_until_leftover_deactivates()
{
	expect(!should_hold_ability_for_leftover_clip(false, false, false),
		"no leftover clip is not a hold");
	expect(!should_hold_ability_for_leftover_clip(true, true, false),
		"early mash during the live Ability is the existing latch hold");
	expect(should_hold_ability_for_leftover_clip(true, true, true),
		"mash at the latch holds until leftover Deactivates");
	expect(should_hold_ability_for_leftover_clip(true, false, false),
		"leftover after the driver went idle holds until Deactivate");
	expect(should_hold_ability_for_leftover_clip(true, false, true),
		"leftover after ArtExit still holds, latch or not");
}

void leftover_is_torn_down_at_the_latch_not_after_its_own_exit()
{
	expect(should_teardown_leftover_clip_at_latch(true, true, true),
		"latch-open leftover is sent SH2_ArtExit so the follow-up can bind");
	expect(!should_teardown_leftover_clip_at_latch(true, false, false),
		"after the clip's own ArtExit a second ArtExit is not consumed");
	expect(!should_teardown_leftover_clip_at_latch(true, true, false),
		"do not tear down before the latch");
	expect(!should_teardown_leftover_clip_at_latch(false, true, true),
		"no leftover clip is nothing to tear down");
}

}  // namespace

int main()
{
	a_healthy_ability_start_binds_the_clip_during_artstart();
	a_chained_ability_whose_clip_does_not_activate_has_not_started();
	an_unconsumed_artstart_has_not_started();
	a_leftover_clip_reactivating_is_not_this_start();
	the_same_clip_after_deactivate_is_this_start();
	artstart_is_not_notified_while_the_clip_is_still_active();
	a_latch_mash_holds_until_leftover_deactivates();
	leftover_is_torn_down_at_the_latch_not_after_its_own_exit();

	if (g_failures > 0) {
		std::cerr << g_failures << " failure(s)\n";
		return EXIT_FAILURE;
	}
	std::cout << "ability_start_test passed\n";
	return EXIT_SUCCESS;
}
