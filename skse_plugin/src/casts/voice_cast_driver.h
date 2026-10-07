#pragma once

namespace SpellHotbar::casts::VoiceCastDriver {

	/**
	 * Resolve the harmless shipped lesser power used to enter Skyrim's real voice-control path.
	 * Must run after Spell Hotbar's forms and equip slots have loaded.
	 */
	bool initialize();

	/**
	 * Temporarily select the proxy power for one Direct Cast.
	 */
	bool begin(RE::PlayerCharacter* pc);

	/**
	 * Press the native voice button after the proxy has been selected.
	 */
	bool press();

	/**
	 * Emit the per-frame held-button repeat while a press is outstanding. Call once per
	 * game-loop update; a physical key produces these and the shout charge logic needs them.
	 */
	void tick(float delta);

	/**
	 * Release the native voice button after the authored charge window.
	 */
	bool release(float held_duration);

	/**
	 * Run another native voice cycle for a sustained concentration animation.
	 */
	bool replay(float held_duration);

	/**
	 * Restore the power that was selected before the Direct Cast began.
	 */
	void restore(RE::PlayerCharacter* pc = nullptr);

	/**
	 * Stop an uncommitted native voice state, then restore the selected power.
	 */
	void cancel(RE::PlayerCharacter* pc);

	bool is_active();
}
