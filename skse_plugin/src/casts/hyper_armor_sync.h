#pragma once

#include <string_view>

// The runtime half of Ability hyperarmor; the pure half is `hyper_armor.h`.
//
// One hidden perk in SpellHotbar.esp, `SpellHotbar_AbilityHyperArmorPerk`, carries two
// unconditioned multiply entries authored at 1.0: Mod Incoming Damage and Mod Incoming Stagger.
// The DLL writes their floats while `ArtDriver::is_active()` -- `1 - reduction` and, with the
// checkbox on, 0 -- and 1.0 otherwise. The floats are record data, not save data, so no save
// can carry the armor across a load, and there is no spell to add or remove on any exit path.
namespace SpellHotbar::casts::HyperArmor {

	// Find the perk's two entry values. Call once after the perk is loaded; a perk that is missing
	// either entry is logged here, and `sync` skips what it could not find.
	void bind(RE::BGSPerk* perk);

	// Give the player the perk if they lack it. Called on new game and on every post-load, so a
	// save made before this build gains it on its first load.
	void grant(RE::PlayerCharacter* player);

	// Write the entry's value for the current state, and log when it changes. Every unpaused
	// frame, beside `ArtDriver::poll_deadline`.
	void sync();

	// Called by the player graph hook for every event. True when the hook should drop `event`:
	// the checkbox is on, an Ability is live, and the event is a stagger. `caller` is the hook's
	// return address, logged on the first swallow of each Ability so the sender is named.
	//
	// The perk's Mod Incoming Stagger entry is the primary defence; this is the backstop for
	// senders that do not read perks. Dropping the event alone only postpones a hit stagger: its
	// sender re-sends it every frame and it lands on the frame the Ability ends.
	[[nodiscard]] bool try_swallow_stagger(std::string_view event, const void* caller);
}
