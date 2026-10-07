#include "cast_intent.h"
#include <array>
#include <string>
#include "art_driver.h"
#include "casting_controller.h"
#include "combo_cache.h"
#include "msco_cast_driver.h"
#include "unpaused_clock.h"
#include "../extern/ShoutMCO_CastIntent.h"
#include "../game_data/game_data.h"
#include "../logger/logger.h"
#include "../rendering/render_manager.h"

namespace SpellHotbar::casts::CastIntent {

	namespace {
		// What the pressed slot meant at the moment of the press. Held by value, and by FormID
		// rather than by pointer, so nothing here can dangle while the intent waits.
		struct Payload {
			size_t slot;
			RE::FormID formID;
			uint32_t art_id;
			slot_type type;
			hand_mode hand;
			// Points into Input::key_spells / key_oblivion_*, which are globals: safe to keep.
			const Input::KeyBind* keybind;
		};

		const ShoutMCO_CastIntentApi* api{ nullptr };
		Status status{ Status::unavailable };

		// The one intent this mod can own. `live_handle` is invalid exactly when no payload is
		// retained; the two are always cleared together.
		ShoutMCO_CastHandle live_handle{ SHOUTMCO_CAST_HANDLE_INVALID };
		Payload payload{};
		bool payload_retained{ false };

		// When the local latch took the press, on the unpaused clock rather than on wall time,
		// so a menu visit does not spend the cap. The latch's only drain is a driver
		// saying it is done, and a driver whose graph never answers never says so, so the press
		// needs a deadline of its own -- one that belongs to this press and is refreshed when
		// another replaces it. Meaningless while `payload_retained` is false or a ShoutMCO handle
		// is live; that path is bounded by ShoutMCO's own caps.
		double retained_at_ms{ 0.0 };

		// True only while the release callback re-attempts the press. The seam reads it and
		// declines to offer again, which is what makes a release attempt exactly one attempt
		// instead of a defer/refuse ping-pong.
		bool attempting_release{ false };

		// One attempt per cancel window. The window stays open for seconds, so a press the graph
		// refuses inside it must not be re-attempted every frame for the rest of the swing; it
		// goes back on the latch and waits for `attackStop`. Cleared whenever a press is freshly
		// retained.
		bool window_attempt_spent{ false };

		void clear_payload()
		{
			live_handle = SHOUTMCO_CAST_HANDLE_INVALID;
			payload_retained = false;
			payload = {};
			retained_at_ms = 0.0;
		}

		const char* cause_name(uint32_t a_cause)
		{
			// Diagnostic only -- never branch cast behaviour on the cause. Unknown values are
			// generic by contract, because a minor version may append to the enum.
			switch (a_cause) {
			case SHOUTMCO_CAUSE_READY:        return "ready";
			case SHOUTMCO_CAUSE_REPLACED:     return "replaced";
			case SHOUTMCO_CAUSE_CANCELLED:    return "cancelled";
			case SHOUTMCO_CAUSE_CONTEXT_LOST: return "context lost";
			case SHOUTMCO_CAUSE_WATCHDOG:     return "watchdog";
			default:                          return "unspecified";
			}
		}

		/**
		 * A one-line snapshot of the graph variables that say whether the player is still busy
		 * swinging. Diagnostic only, at debug level: it is the difference between "the release
		 * came too early" as an inference and as an observation, on both sides of the boundary.
		 */
		std::string graph_snapshot(RE::PlayerCharacter* pc)
		{
			if (!pc) {
				return "no player";
			}
			constexpr std::array vars{ "IsAttacking"sv, "IsBashing"sv, "IsBlocking"sv,
									   "IsRecoiling"sv, "IsStaggering"sv, "IsCastingRight"sv,
									   "IsEquipping"sv, "bAnimationDriven"sv };
			std::string out;
			for (const auto& name : vars) {
				bool value{ false };
				if (pc->GetGraphVariableBool(RE::BSFixedString(name), value)) {
					if (!out.empty()) {
						out += ' ';
					}
					out += std::format("{}={}", name, value ? 1 : 0);
				}
			}
			return out.empty() ? "no readable graph vars" : out;
		}

		void inject_hotbar_shout_button()
		{
			auto queue = RE::BSInputEventQueue::GetSingleton();
			auto user_events = RE::UserEvents::GetSingleton();
			if (!queue || !user_events) {
				logger::warn("SH2 cast intent: cannot inject shout button (no queue)");
				return;
			}
			auto [device, key] = Input::get_shout_key_and_device();
			auto event = RE::ButtonEvent::Create(device, user_events->shout, key, 1.0f, 0.0f);
			if (!event) {
				return;
			}
			queue->PushOntoInputQueue(event);
			logger::debug("SH2 cast intent: injected shout ButtonEvent on release");
		}

		bool payload_still_bound(const Payload& p, const SlottedSkill& skill)
		{
			return skill.formID == p.formID && skill.type == p.type && skill.hand == p.hand &&
				   skill.art_id == p.art_id;
		}

		void retain_local_payload(size_t slot, const Input::KeyBind& keybind, bool refresh_cap)
		{
			const auto skill = GameData::get_current_spell_info_in_slot(slot);
			if (api && live_handle != SHOUTMCO_CAST_HANDLE_INVALID) {
				api->Cancel(live_handle);
			}
			live_handle = SHOUTMCO_CAST_HANDLE_INVALID;
			payload = Payload{ slot, skill.formID, skill.art_id, skill.type, skill.hand, &keybind };
			payload_retained = true;
			if (refresh_cap) {
				retained_at_ms = UnpausedClock::now_ms();
			}
			window_attempt_spent = false;
		}

		// `a_report_refusal` is false for the one speculative attempt this stack makes: a release
		// at the swing's cancel window that the graph may refuse while the press is still owed.
		// Flashing the slot red there would report a dropped press that is about to fire on its
		// own.
		bool fire_payload(const Payload& p, bool a_report_refusal)
		{
			if (!p.keybind) {
				return false;
			}
			bool success{ false };
			if (p.type == slot_type::weapon_art) {
				success = CastingController::try_start_art(p.art_id, p.slot, *p.keybind);
			} else if (p.type == slot_type::shout) {
				auto form = RE::TESForm::LookupByID(p.formID);
				success = CastingController::try_cast_power(form, *p.keybind, p.slot, p.hand);
				if (success && !payload_retained) {
					inject_hotbar_shout_button();
				}
			} else {
				auto form = RE::TESForm::LookupByID(p.formID);
				success = CastingController::try_start_cast(form, *p.keybind, p.slot, p.hand);
			}
			logger::debug("SH2 cast intent: fired slot {} type {} -> {}", p.slot,
				static_cast<int>(p.type), success);
			if (holding_for_leftover_clip()) {
				return success;
			}
			if (success || a_report_refusal) {
				RenderManager::highlight_skill_slot(static_cast<int>(p.slot), 0.5f, !success);
			}
			return success;
		}

		// What a release attempt actually did. The three are not interchangeable: a payload whose
		// slot changed or whose preconditions lapsed is DEAD and must not come back, while a press
		// the graph merely would not take right now is still owed and can wait for the swing to
		// end.
		enum class ReleaseOutcome {
			fired,
			refused,
			discarded,
		};

		/**
		 * The release attempt. The intent may have waited hundreds of milliseconds, so every
		 * precondition the original press passed is checked again here, and a payload that no
		 * longer holds is discarded once with no retry.
		 */
		ReleaseOutcome attempt_release(const Payload& p, bool a_fallback_remains = false)
		{
			auto pc = RE::PlayerCharacter::GetSingleton();
			if (!pc || pc->IsDead() || !Input::in_ingame_state()) {
				logger::debug("SH2 cast intent: released into a state that cannot cast, discarded");
				return ReleaseOutcome::discarded;
			}

			auto skill = GameData::get_current_spell_info_in_slot(p.slot);
			if (!payload_still_bound(p, skill)) {
				logger::debug("SH2 cast intent: slot {} no longer holds the pressed skill, discarded", p.slot);
				return ReleaseOutcome::discarded;
			}

			if (!Input::allowed_to_instantcast(skill.formID)) {
				logger::debug("SH2 cast intent: preconditions no longer met on release, discarded");
				RenderManager::highlight_skill_slot(static_cast<int>(p.slot), 0.5f, true);
				return ReleaseOutcome::discarded;
			}
			// allowed_to_cast treats a live MagicCaster as busy. That is correct for a ShoutMCO
			// release into idle, but wrong when we are releasing into our own Driver Cast latch:
			// IsCasting is still true for the clip that just opened the window.
			const bool releasing_behind_our_shtb =
				ArtDriver::is_active() || MscoCastDriver::is_active();
			if (p.type == slot_type::spell && !releasing_behind_our_shtb &&
				!Input::allowed_to_cast(skill.formID)) {
				logger::debug("SH2 cast intent: spell restrictions no longer met on release, discarded");
				RenderManager::highlight_skill_slot(static_cast<int>(p.slot), 0.5f, true);
				return ReleaseOutcome::discarded;
			}

			logger::debug("SH2 cast intent: graph at release: {}", graph_snapshot(pc));

			attempting_release = true;
			const bool fired = fire_payload(p, !a_fallback_remains);
			attempting_release = false;
			return fired ? ReleaseOutcome::fired : ReleaseOutcome::refused;
		}

		/**
		 * ShoutMCO's single callback, fired exactly once per deferred intent, on the main thread.
		 *
		 * It is never re-entrant: ShoutMCO retires the intent under its own mutex but only queues
		 * the callback as a task, so this always arrives on a later drain — after the `Request` or
		 * `Cancel` that retired it has long returned. The handle check is what makes that safe.
		 * A callback for an intent this mod has already replaced or withdrawn names a handle that
		 * is no longer the live one, and must not touch the payload that took its place.
		 */
		void on_intent(ShoutMCO_CastHandle a_handle, ShoutMCO_CastOutcome a_outcome, uint32_t a_cause, void*)
		{
			if (!payload_retained || a_handle != live_handle) {
				// An intent this mod already retired. Nothing of ours is left to clear.
				return;
			}

			const Payload p = payload;
			clear_payload();

			if (a_outcome == SHOUTMCO_CAST_RELEASE) {
				logger::debug("SH2 cast intent: RELEASE for handle {} ({})", a_handle, cause_name(a_cause));
				attempt_release(p);
			}
			else {
				logger::debug("SH2 cast intent: ABANDON for handle {} ({}), payload on slot {} dropped",
					a_handle, cause_name(a_cause), p.slot);
				// A dropped payload is a refused press. The player pressed the slot and
				// nothing ever came of it, so it reports like every other refusal rather than
				// vanishing into the log.
				RenderManager::highlight_skill_slot(static_cast<int>(p.slot), 0.5f, true);
			}
		}
	}

	void negotiate()
	{
		if (auto dll = GetModuleHandleA("ShoutMCO.dll")) {
			auto get_api = reinterpret_cast<ShoutMCO_GetCastIntentApi_t>(
				GetProcAddress(dll, SHOUTMCO_CAST_INTENT_EXPORT));
			if (get_api) {
				// A null return is the incompatible case, not an error: ShoutMCO refuses a major
				// version it does not implement, and we fall back to native behaviour.
				api = get_api(SHOUTMCO_CAST_INTENT_VERSION_MAJOR);
				status = api ? Status::active : Status::incompatible;
			}
		}
		logger::info("SpellHotbar2 ShoutMCO cast-intent API: {}", status_name());
	}

	Status get_status()
	{
		return status;
	}

	const char* status_name()
	{
		switch (status) {
		case Status::active:       return "active";
		case Status::incompatible: return "incompatible";
		default:                   return "unavailable";
		}
	}

	bool is_pending()
	{
		return payload_retained;
	}

	bool is_firing()
	{
		return attempting_release;
	}

	bool our_latch_is_closed()
	{
		return should_retain_local_cast_intent(
			ArtDriver::is_active() || MscoCastDriver::is_active(),
			ArtDriver::is_active() ? ArtDriver::latch_open() : MscoCastDriver::combo_window_open(),
			CastingController::is_live_concentration(), false, false);
	}

	// Which clock this press is waiting on. Read at poll time rather than stored at retain, so a
	// press whose reason changes under it -- our own cast starting while it waits -- expires on
	// the cap that actually applies.
	double hold_cap_ms()
	{
		const bool ours = ArtDriver::is_active() || MscoCastDriver::is_active();
		return press_hold_cap_ms(!ours && MscoCastDriver::swing_open());
	}

	bool should_hold_for_foreign_swing()
	{
		return should_hold_cast_for_foreign_swing(
			ArtDriver::is_active() || MscoCastDriver::is_active(),
			CastingController::is_live_concentration(), MscoCastDriver::swing_open(),
			// Same one-attempt rule as should_retain_now(): once the window's attempt is spent
			// the term reads open again, so the press falls through to the swing's end.
			MscoCastDriver::swing_window_open() && !window_attempt_spent);
	}

	bool should_retain_now()
	{
		return should_retain_local_cast_intent(
			ArtDriver::is_active() || MscoCastDriver::is_active(),
			ArtDriver::is_active() ? ArtDriver::latch_open() : MscoCastDriver::combo_window_open(),
			CastingController::is_live_concentration(), MscoCastDriver::swing_open(),
			// The swing's own cancel window is a release edge, not a reason to keep holding.
			// Spending the one attempt this window gets puts the press back on the swing's end,
			// so the term reads false again for the rest of the swing.
			MscoCastDriver::swing_window_open() && !window_attempt_spent);
	}

	bool offer(size_t slot, const Input::KeyBind& keybind)
	{
		if (attempting_release) {
			return false;
		}

		const auto skill = GameData::get_current_spell_info_in_slot(slot);
		if (should_retain_now()) {
			retain_local_payload(slot, keybind, true);
			logger::debug("SH2 cast intent: slot {} retained on local latch (type={})", slot,
				static_cast<int>(skill.type));
			return true;
		}

		if (!api) {
			return false;
		}

		ShoutMCO_CastRequest req{};
		req.structSize = sizeof(req);
		req.versionMajor = SHOUTMCO_CAST_INTENT_VERSION_MAJOR;
		req.versionMinor = SHOUTMCO_CAST_INTENT_VERSION_MINOR;
		req.flags = 0u;
		req.callback = &on_intent;
		req.context = nullptr;

		// A deferred request replaces whatever intent was pending, whoever held it. The displaced
		// holder's abandon callback is queued, not called here, so if it was one of ours it lands
		// after this returns and finds a handle that no longer matches -- which is exactly what
		// keeps it from clearing the payload taken below.
		ShoutMCO_CastHandle handle{ SHOUTMCO_CAST_HANDLE_INVALID };
		const auto decision = api->Request(&req, &handle);
		if (decision != SHOUTMCO_CAST_DEFERRED) {
			logger::debug("SH2 cast intent: not deferred (decision {}), refusing as before",
				static_cast<int>(decision));
			return false;
		}

		live_handle = handle;
		payload = Payload{ slot, skill.formID, skill.art_id, skill.type, skill.hand, &keybind };
		payload_retained = true;

		logger::debug("SH2 cast intent: slot {} deferred to ShoutMCO (handle {})", slot, handle);
		logger::debug("SH2 cast intent: graph at defer:   {}",
			graph_snapshot(RE::PlayerCharacter::GetSingleton()));
		return true;
	}

	void poll_local_release()
	{
		if (attempting_release || !payload_retained || live_handle != SHOUTMCO_CAST_HANDLE_INVALID) {
			return;
		}
		// Ahead of the retain bail, because a latch that never opens is exactly the case the
		// retain bail cannot see past. The press was consumed on our word that it would fire
		// later; when that word expires, say so loudly on both channels rather than quietly
		// keeping it.
		const double cap = hold_cap_ms();
		if (payload.type == slot_type::weapon_art &&
			ArtDriver::should_hold_ability_for_leftover_clip(ArtDriver::clip_is_active(),
				ArtDriver::is_active(), ArtDriver::latch_open())) {
			ArtDriver::try_teardown_leftover_at_latch(RE::PlayerCharacter::GetSingleton());
			if (ArtDriver::should_hold_ability_for_leftover_clip(ArtDriver::clip_is_active(),
					ArtDriver::is_active(), ArtDriver::latch_open())) {
				return;
			}
		}
		if (local_latch_hold_expired(UnpausedClock::now_ms(), retained_at_ms, cap)) {
			const size_t slot = payload.slot;
			const double held = UnpausedClock::now_ms() - retained_at_ms;
			clear_payload();
			logger::warn("SH2 cast intent: local latch dropped slot {} after {:.0f}ms ({:.0f}ms cap)",
				slot, held, cap);
			RenderManager::highlight_skill_slot(static_cast<int>(slot), 0.5f, true);
			return;
		}
		if (should_retain_now()) {
			return;
		}
		// Which edge is releasing this press, read before the payload is cleared. A window
		// release is speculative -- the graph may still refuse a cast inside BFCO's cancel
		// window -- so a refusal there is not a dropped press: the payload goes back on the latch
		// with its original cap and fires at `attackStop` if that comes before the cap. A
		// swing-end release has no fallback left, so a refusal there is reported as one.
		const bool at_window = MscoCastDriver::swing_open() && MscoCastDriver::swing_window_open();
		const Payload p = payload;
		const double held_since = retained_at_ms;
		clear_payload();
		logger::debug("SH2 cast intent: local latch releasing slot {} ({})", p.slot,
			at_window ? "at the swing's cancel window" : "on the swing's end");
		if (attempt_release(p, at_window) == ReleaseOutcome::refused && at_window &&
			MscoCastDriver::swing_open()) {
			payload = p;
			payload_retained = true;
			// The ORIGINAL retain time, not now: the press keeps its one cap across the window
			// attempt, so a graph that refuses every frame cannot hold it forever.
			retained_at_ms = held_since;
			window_attempt_spent = true;
			logger::debug(
				"SH2 cast intent: graph refused slot {} inside the window; held for the swing's end",
				p.slot);
		}
	}

	void cancel()
	{
		window_attempt_spent = false;
		if (api && live_handle != SHOUTMCO_CAST_HANDLE_INVALID) {
			api->Cancel(live_handle);
		}
		// Drop local ownership now rather than waiting for the abandon callback, which ShoutMCO
		// queues onto a later drain. Waiting would leave a withdrawn payload looking pending for
		// the rest of the frame — and on a game load, past the point the bar it names still
		// exists. The queued callback then finds a stale handle and does nothing, as intended.
		clear_payload();
	}

	bool hold_for_leftover_clip(size_t slot, const Input::KeyBind& keybind)
	{
		const bool already_retained = payload_retained;
		retain_local_payload(slot, keybind, !already_retained);
		logger::debug("SH2 art: leftover SH2_Art_Clip still Active; holding until Deactivate");
		return true;
	}

	bool holding_for_leftover_clip()
	{
		return payload_retained &&
			ArtDriver::should_hold_ability_for_leftover_clip(
				ArtDriver::clip_is_active(), ArtDriver::is_active(), ArtDriver::latch_open());
	}
}
