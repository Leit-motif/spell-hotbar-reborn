#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
#include "../input/keybinds.h"
#include "../bar/hotbar.h"
#include "combo_cache.h"
#include "art_deadline.h"

namespace SpellHotbar::casts::CastingController {

	/**
	* Casttime at which ritual spells play the fast anim
	*/
	constexpr float fast_cast_threshold{ 1.51f };

	class BaseCastingInstance {
	public:
		BaseCastingInstance(RE::TESForm* form, float casttime);
		virtual ~BaseCastingInstance() = default;

		float get_current_casttime() const;
		virtual float get_current_gcd_progress() const;
		virtual float get_current_gcd_duration() const;

		virtual bool advance_time(float delta);
		virtual bool is_gcd_expired() const;

		/**
		* Seconds of lockout that have run since this instance was created -- i.e. since the
		* press. The GCD is measured against this, not against cast completion, so an instance
		* owns the player's button for `m_gcd` seconds however long its clip runs.
		*/
		inline float get_lockout_elapsed() const {
			return m_press_elapsed;
		}

		virtual const std::string_view get_start_anim() const;

		virtual bool update(RE::PlayerCharacter* pc, float delta) = 0;

		/**
		* Does this cast hold a shtb cast state that may be ended early?
		*
		* False by default, so only a kind of cast that says otherwise is ever cut. A power, a
		* shout, and a potion never enter the state at all, and ending one on their behalf would
		* be a send for a state they do not own. A concentration channel holds its own looping
		* state for the whole hold; ending a channel early is `cut_channel_for_attack` instead.
		*/
		virtual bool has_cuttable_cast_state() const;

		virtual void on_reset();

		void consume_items();

		inline void set_casted() {
			m_casted = true;
		}
		inline bool casted() const {
			return m_casted;
		}
		inline RE::TESForm* get_form() {
			return m_form;
		}
		/**
		* Re-aim the lockout so it ends `new_val` seconds from now. The shout re-arm at fire is the
		* only caller. Anchored on the press clock, like the rest of the lockout.
		*/
		inline void updateGCD(float new_val) {
			m_gcd = m_press_elapsed + new_val;
		}

		void apply_cooldown();

	protected:
		RE::TESForm* m_form;
		float m_cast_timer;
		float m_total_casttime;
		/**
		* The whole lockout this instance owns, measured from the press. Derived constructors set
		* it; it is not a tail added after the cast time.
		*/
		float m_gcd;
		// Seconds since construction, i.e. since the press. The press-anchored clock.
		float m_press_elapsed;
		bool m_casted;
	};

	//Abstract base for Spell casts
	class CastingInstance : public BaseCastingInstance {
	public:
		CastingInstance(RE::SpellItem* spell, float casttime, float manacost, hand_mode used_hand, uint16_t casteffect, bool is_spell_proc);
		virtual ~CastingInstance() = default;

		virtual bool is_anim_ok(RE::PlayerCharacter* pc) const;
		virtual const std::string_view get_end_anim() const;
		virtual const std::string_view get_cancel_anim() const;

		//A spell cast is one clip that ends, and it owns the state it entered.
		virtual bool has_cuttable_cast_state() const override;

		virtual void apply_cast_start_spell(RE::PlayerCharacter* pc);

		virtual void on_reset() override;

		/**
		 * Tear down FX and post-cast callbacks without leaving the shtb state. A follow-up
		 * Driver Cast notifies the next clip event from inside the live state; CastExit
		 * would route through ready and the next notify would miss.
		 */
		void on_reset_keep_graph();
		/*
		* Game loop update logic, return if cast should be cleared afterwards. 
		*/
		virtual bool update(RE::PlayerCharacter* pc, float delta) override;

		void play_charge_sound();
		void stop_charge_sound();
		void play_release_sound() const ;
		void play_cast_loop_sound();
		void stop_cast_loop_sound();
		void deliver_payload(RE::PlayerCharacter* pc);

		virtual bool is_first_time_update() const;

		bool should_play_release_anim();

		inline RE::SpellItem* get_spell() {
			return m_form->As<RE::SpellItem>();
		}

		inline const RE::SpellItem* get_spell() const {
			return m_form->As<RE::SpellItem>();
		}

		inline void set_release_played() {
			m_played_release = true;
		}

		inline bool is_procced() const {
			return m_spell_proc;
		}

	protected:
		RE::BGSSoundDescriptorForm* m_charge_sound;
		RE::BGSSoundDescriptorForm* m_release_sound;
		RE::BGSSoundDescriptorForm* m_cast_loop_sound;
		RE::BSSoundHandle m_charge_sound_instance;
		RE::BSSoundHandle m_loop_sound_instance;
		bool m_played_release;
		float m_release_anim_time;
		bool m_played_pre_release;
		float m_pre_release_anim_time;
		float m_manacost;
		hand_mode m_used_hand;
		RE::SpellItem* m_equip_ability;
		uint16_t m_casteffect;
		bool m_spell_proc;
		bool m_spell_started;
		// Grace window after the SH2_CastRight send during which a not-yet-seen
		// SH2_CastEnter must not cancel the cast; the graph needs a few frames to
		// enter the state.
		float m_entry_grace;
		bool m_last_anim_ok;
	};

	// Regular FNF spell. Translation blocking is owned by the behavior graph -- the
	// shtb cast states are exclusive full-body states that leave the locomotion
	// subtree; the DLL does not participate.
	class CastingInstanceSpell : public CastingInstance {
	public:
		CastingInstanceSpell(RE::SpellItem* spell, float casttime, float manacost, hand_mode used_hand, uint16_t casteffect, bool spell_proc);
		virtual ~CastingInstanceSpell() = default;
	};

	//Ritual cast with blocked movement
	class CastingInstanceRitual : public CastingInstance {
	public:
		CastingInstanceRitual(RE::SpellItem* spell, float casttime, float manacost, hand_mode used_hand, uint16_t casteffect, bool spell_proc);
		virtual ~CastingInstanceRitual() = default;
	};

	//Single handed concentration spell with movement
	class CastingInstanceSpellConcentration : public CastingInstance {
	public:
		CastingInstanceSpellConcentration(RE::SpellItem* spell, float casttime, float manacost, hand_mode used_hand, uint16_t casteffect, bool spell_proc, const Input::KeyBind& keybind, int slot);
		virtual ~CastingInstanceSpellConcentration() = default;

		virtual void apply_cast_start_spell(RE::PlayerCharacter* pc) override;
		virtual void on_reset() override;

		virtual bool advance_time(float delta) override;
		virtual bool is_first_time_update() const override;
		virtual bool update(RE::PlayerCharacter* pc, float delta) override;
		virtual bool is_gcd_expired() const override;
		virtual bool has_cuttable_cast_state() const override;

		virtual bool has_duration() const;
		virtual float get_current_gcd_progress() const override;
		/**
		 * A channel is not on the press-anchored clock -- it owns the button for as long as the
		 * player holds it, and `m_gcd` is only the 0.25s tail after release. Report the same
		 * denominator its own progress uses.
		 */
		virtual float get_current_gcd_duration() const override;

		/**
		 * Is the channel past its commitment point and streaming the spell? Before that there
		 * is no channel to hand off, only a charge that a cut would throw away.
		 */
		inline bool is_streaming() const { return m_spell_started; }

		/**
		 * End the hold: stop the loop sound, interrupt the caster, drop the animation globals
		 * so the OAR idle loop reverts, hand the combo position on, and pay the cooldown. The
		 * player releasing the key and an attack chaining out both come through here.
		 */
		void end_channel(RE::PlayerCharacter* pc);
	protected:
		const Input::KeyBind& m_keybind;
		int m_slot;
	};

	//Ritual style concentration spell, 2hands and blocked movement
	class CastingInstanceSpellRitualConcentration : public CastingInstanceSpellConcentration {
	public:
		CastingInstanceSpellRitualConcentration(RE::SpellItem* spell, float casttime, float manacost, hand_mode used_hand, uint16_t casteffect, bool spell_proc, const Input::KeyBind& keybind, int slot, float pre_release_anim_time);
		virtual ~CastingInstanceSpellRitualConcentration() = default;
	};

	class CastingInstancePower : public BaseCastingInstance {
	public:
		CastingInstancePower(RE::TESForm* form);
		virtual ~CastingInstancePower() = default;

		virtual void on_reset() override;

		virtual bool reequip_old_power();

		virtual bool update(RE::PlayerCharacter* pc, float delta) override;
	protected:
		/**
		* May this instance hand its selectedPower write-back to the controller instead of
		* performing it now? A plain power restores immediately, as it always has.
		*/
		virtual bool defer_power_write_back(RE::PlayerCharacter*) const { return false; }

		RE::TESForm* m_old_form;
		bool m_reequiped;
	};

	class CastingInstanceShout : public CastingInstancePower {
	public:
		CastingInstanceShout(RE::TESForm* form);
		virtual ~CastingInstanceShout() = default;
		virtual bool reequip_old_power() override;
	protected:
		/**
		* A shout's clip outlives its cast instance: the instance dies at GCD expiry while the
		* graph is still shouting, and words two and three echo after that. Restoring
		* selectedPower there gives those echoes the *equipped* shout's identity. Defer while the
		* graph says IsShouting.
		*/
		virtual bool defer_power_write_back(RE::PlayerCharacter* pc) const override;
	};

	class CastingInstancePotionUse : public BaseCastingInstance {
	public:
		CastingInstancePotionUse(RE::TESForm* form);
		virtual ~CastingInstancePotionUse() = default;

		virtual bool update(RE::PlayerCharacter* pc, float delta) override;
	protected:
	};

	/**
	 * A lightweight Action GCD holder. Actions inject their target input directly and never enter
	 * SH2's ArtDriver or behavior-graph state; this instance exists only while a configured GCD
	 * keeps the ordinary hotbar press gate closed.
	 */
	class CastingInstanceAction : public BaseCastingInstance {
	public:
		explicit CastingInstanceAction(float gcd);
		virtual ~CastingInstanceAction() = default;

		virtual bool update(RE::PlayerCharacter* pc, float delta) override;
	};

	class CastingInstanceWeaponArt : public BaseCastingInstance {
	public:
		CastingInstanceWeaponArt(uint32_t art_id, float gcd);
		virtual ~CastingInstanceWeaponArt() = default;

		virtual bool update(RE::PlayerCharacter* pc, float delta) override;
		virtual void on_reset() override;
	protected:
		uint32_t m_art_id;
		ArtDriver::ArtInstanceCap m_deadline;
	};

	/**
	* Wraps the shared information needed for all spell cast types
	*/
	struct CastingInstanceSpellData
	{
		CastingInstanceSpellData(RE::SpellItem* spell, float casttime, float manacost, hand_mode hand, bool dual_cast, int animamtion, int animation2, uint16_t casteffect, bool is_spell_proc);

		RE::SpellItem* m_spell;
		float m_casttime;
		float m_manacost; 
		hand_mode m_hand;
		bool m_dual_cast;
		int m_animation;
		int m_animation2;
		uint16_t m_casteffect;
		bool m_spellproc;
	};


	void update_cast(float delta);

	extern std::unique_ptr<BaseCastingInstance> current_cast;

	/**
	* Which hands may commit the current cast, and which cast armed them. Read once per
	* graph event by the animation-event hook, which then answers every per-hand question
	* about that event from the same snapshot.
	*/
	struct SpellFireArming {
		std::uint8_t mask;
		std::uint32_t generation;
	};

	SpellFireArming spellfire_arming();

	/**
	* The graph raised a `MLh/MRh_SpellFire_Event`: if the current cast throws with that
	* hand, it is committed from here and will deliver its spell whatever happens to the
	* casting state. Called from the animation-event hook, on the animation thread.
	*
	* `generation` is the one `spellfire_arming()` returned for this event. An arming that
	* landed since then belongs to a later cast, and this event is dropped rather than
	* committing a cast whose own clip has not reached its throw frame.
	*
	* `driver_cast_active` is the hook's own snapshot, the same one isolation answered from.
	* The mask outlives the clip that armed it, so without this term a vanilla release in an
	* armed hand is accepted as a driver cast's own event.
	*/
	void notify_spellfire(SpellFireHand hand, std::uint32_t generation, bool driver_cast_active);

	/**
	* Disarm the latch: forget any commitment AND the armed hands. Called at cast teardown;
	* arm_spellfire rebuilds the word at the next start. The generation is preserved.
	*/
	void clear_spellfire();

	/**
	* Has the graph passed the commitment point for the current cast?
	*/
	bool is_cast_committed();

	/**
	* Is a committed cast holding the graph right now — a live cast instance, its shtb state
	* active, and the commitment point already passed?
	*
	* The attack-press chain-out and a consecutive-cast follow-up both read this. Before the
	* commitment point a cut costs the player the spell (real for the clip's first 0.483s),
	* so a press then must not cut the cast.
	*/
	bool is_committed_cast_holding_graph();

	/**
	 * Is the graph in cuttable follow-through — the cast instance already retired at GCD expiry
	 * but its clip still playing on a live shtb state?
	 *
	 * The attack chain-out reads this beside `is_committed_cast_holding_graph()`: between them
	 * they cover the whole cuttable span, from the commitment point to clip end. A charging cast
	 * still has a live instance and is refused here, so it stays protected.
	 */
	bool is_cuttable_follow_through();

	/**
	 * Pay out an armed-but-undelivered payload now, naming where from. Safe to call with nothing
	 * armed. Every seam that takes the graph away from a clip that still owes a payload calls
	 * this first, so there is one delivery story.
	 */
	void deliver_armed_payload(std::string_view reason);

	/**
	 * May the public hotbar path attempt a Driver Cast right now? True when no cast is live,
	 * or when a committed cuttable Driver Cast holds the graph inside its
	 * SpellFire-to-WinClose combo window. False pre-spellfire, after WinClose until
	 * CastExit, during concentration, and for potions and shouts.
	 */
	bool can_accept_hotbar_cast();

	bool is_live_concentration();

	/**
	 * Is a concentration channel live and streaming, so an attack may chain out of it?
	 *
	 * A channel has no clip clock to read: the shtb state ended with its start clip and the
	 * hold is sustained by the OAR idle loop. Its chain-out window is therefore the hold
	 * itself, opened at the commitment point and closed when the player lets go.
	 */
	bool is_channel_chainable();

	/**
	 * End a streaming channel because the player attacked. The press itself is untouched and
	 * reaches the game as an ordinary swing, continuing the combo the channel interrupted.
	 */
	void cut_channel_for_attack(RE::PlayerCharacter* pc);

	/**
	 * What a cut left behind for the caller to wait on. An exit the graph consumed has a
	 * transition still to land, and an attack forwarded into it dies without reaching its cancel
	 * window. The seam holds its own attack on `cut_exit_pending`; a pending cut also leaves the
	 * seam a marker, which holds the attack of a caller that cut before reaching it.
	 */
	enum class CastCut {
		none,              // nothing cuttable was present; the caller's attack was never ours to hold
		cut_exit_pending,  // the state ended and the graph took SH2_CastCut or SH2_CastExit -- the return to ready is still to land
		cut_no_exit,       // the state ended, but the graph refused both; nothing to wait for
	};

	/**
	 * End the cuttable Driver Cast span for an attack-shaped Action or physical attack press.
	 * The payload is delivered before the graph state is cancelled, matching the input hook's
	 * existing ordering. Returns `none` when no committed/follow-through cast is present.
	 */
	CastCut cut_committed_cast_for_attack(RE::PlayerCharacter* pc);

	bool try_start_cast(RE::TESForm* form, const Input::KeyBind& keybind, size_t slot, hand_mode hand);

	bool try_start_art(uint32_t art_id, size_t slot, const Input::KeyBind& keybind);

	bool try_start_action(uint32_t action_id, size_t slot, const Input::KeyBind& keybind);

	/** Mirror a held physical Action source event onto its frozen target. */
	bool mirror_action_hold(uint32_t trigger_dx_scancode, float value, float held_duration);
	/** Release Actions owned by the original physical source; safe across menus and mode changes. */
	bool release_action_for_trigger(uint32_t trigger_dx_scancode, float held_duration);
	/** Release Actions on `slot` whose generation is at or past `first_generation`, the marker
	 *  taken before the press. Used by the bounded Papyrus castSlot seam. */
	void release_action_for_slot(size_t slot, std::uint64_t first_generation, float held_duration);
	/** The generation the next accepted Action input will carry. Take this before a press and
	 *  hand it back to release_action_for_slot to name only what that press admitted. */
	std::uint64_t next_action_generation();
	/** Mark all injected Action targets for release during load/mode invalidation.
	 *  `defer` keeps the record instead of emitting the up now: a game load may reset the native
	 *  input queue before the event is dispatched, so the up is left to retry_action_releases. */
	void release_all_action_inputs(bool defer = false);
	/** Retry target-up events that could not fit in the native input queue. */
	void retry_action_releases();
	
	bool try_cast_power(RE::TESForm* form, const Input::KeyBind& keybind, size_t slot, hand_mode hand);

	bool start_potion_use(RE::TESForm* alch_item);

	bool is_currently_using_power();

	/**
	* Is current spell boosted by a spell proc?, return false when no cast
	*/
	bool is_currently_using_procced_spell();

	float get_current_casttime();

	bool can_start_new_cast();

	void drop_live_cast();

	/**
	* Perform any selectedPower write-back a finished hotbar shout is still holding, right
	* now. Called before a new swap and before the game serializes, so the swapped value can
	* never reach a save or be captured as the next cast's "old" power.
	*/
	void flush_deferred_power_restore();

	/*
	* check if a power is currently tracked as casting instance, and finish it.
	*/
	void try_finish_power_cast(RE::FormID formID);
	void try_finish_shout_cast(RE::FormID formID);

	float get_current_gcd_progress();
	float get_current_gcd_duration();

	/**
	* Actually casts the spell, do not call directly
	*/
	bool cast_spell(RE::SpellItem* spell, bool dual_cast, bool spell_proc, std::optional<float> concentration_manacost = std::nullopt);

	void cast_spell_on_player(RE::SpellItem* spell, float magnitude = 0.0f, bool no_art = false);

	bool player_has_equip_ability(RE::SpellItem* equip_ability);
}
