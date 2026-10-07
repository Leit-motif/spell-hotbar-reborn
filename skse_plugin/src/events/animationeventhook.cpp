#include "animationeventhook.h"
#include "attack_seam_hook.h"
#include "../casts/casting_controller.h"
#include "../casts/combo_cache.h"
#include "../casts/msco_cast_driver.h"
#include "../casts/art_driver.h"
#include "../logger/logger.h"

using namespace std::literals;

namespace SpellHotbar::events {

	namespace {
		// Every chaining question this integration has turns on the ORDER of a handful of
		// events on the player's stream -- the state's own SH2_*, the commitment point, the
		// MCO/MSCO annotations on the clip it borrows, and whether an attack follows a cut.
		// The trace records that order directly, so it is kept rather than improvised again.
		//
		// Bounded twice over, because this runs on the animation thread and the logger flushes
		// on every line: to a cast window (see should_trace_graph_events), and to the tags this
		// integration turns on. An ordinary MCO swing raises `MCO_*` and `attack*` several
		// times a second and must never reach the file.
		//
		// `BFCO_` is here because leaving it out makes the trace blind to the exact event this
		// integration keys its window release on. `is_mco_swing_window_open_event` matches BFCO's
		// own `BFCO_NextWinStart` / `BFCO_NextPowerWinStart` alongside MCO's names; a filter that
		// matches only MCO's means that on a BFCO-native moveset the window opens -- provably, the
		// latch released 1.164 s into a 1.83 s swing -- with no trace line to show it, and that
		// silence reads, wrongly, as "BFCO raises no window events".
		bool is_traced_tag(const RE::BSFixedString& a_tag)
		{
			const char* raw = a_tag.c_str();
			if (!raw) {
				return false;
			}
			const std::string_view tag{ raw };
			return tag.starts_with("SH2_"sv) || tag.starts_with("MSCO_"sv) || tag.starts_with("MCO_"sv) ||
				   tag.starts_with("BFCO_"sv) || tag.starts_with("SBF_"sv) || tag.starts_with("attack"sv) ||
				   tag.contains("SpellFire"sv);
		}
	}

	void Animation_event_hook::ProcessEvent(RE::BSAnimationGraphEvent* a_event,
		RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_eventSource,
		casts::SpellFireHand a_spellfire_hand,
		casts::CastingController::SpellFireArming a_arming,
		bool a_driver_cast_active)
	{
		{
			if (!a_event || !a_event->holder) {
				return;
			}

			auto eventHolder = const_cast<RE::TESObjectREFR*>(a_event->holder);
			//auto animationGraph = static_cast<RE::BShkbAnimationGraph*>(a_eventSource);

			// THE COMMITMENT POINT.
			//
			// The events this hook exists for. MCBO's casting clips carry a SpellFire annotation
			// at the frame the hand throws the spell, and the graph raises it as this event; a
			// cast that has heard it delivers its spell even if the casting state is torn away
			// afterwards. Vanilla casting anims raise the same events on real casts -- the flag
			// they set is cleared whenever a cast begins, so a real cast cannot leave a later
			// hotbar cast pre-committed.
			//
			// This runs on the animation thread. It sets an atomic and touches nothing else.
			if (eventHolder->IsPlayerRef()) {
				// Traced before the observer runs, so the event that ends the state appears in
				// its own trace rather than being swallowed by the state it closes.
				if (is_traced_tag(a_event->tag) &&
					(casts::MscoCastDriver::should_trace_graph_events() ||
					 casts::ArtDriver::should_trace_graph_events())) {
					logger::trace("SH2 graph event: {}", a_event->tag.c_str());
				}

				// The driver's active flag is cleared from here, on SH2_CastExit / SH2_ArtExit.
				//
				// The PAYLOAD travels with the tag. A clip annotation written as
				// `PIE.@SGVI|MCO_nextattack|3` is split by the engine at the first `.`: the event
				// NAME is `PIE` and everything after it is the payload. Forwarding only the tag
				// means MCO's own combo advance -- the one value the whole rolling cache exists to
				// keep -- arrives on every swing and is dropped on the floor here, which reads as
				// "these packs emit no SGVI". ArtDriver keeps its two-argument signature: it
				// matches on event names only and has no payload to read.
				// Re-read the arming word immediately before the graph-side commitment can
				// mutate combo state: an arming that landed since the entry snapshot belongs
				// to a later cast, and this event must not open ITS window or advance ITS
				// index. On the snapshot alone, the commitment point would run ahead of the
				// generation-aware notify, with no rollback.
				const std::uint8_t commit_mask =
					casts::CastingController::spellfire_arming().generation == a_arming.generation
						? a_arming.mask
						: 0U;
				casts::MscoCastDriver::observe_graph_event(
					eventHolder->As<RE::Actor>(), a_event->tag, a_event->payload,
					a_spellfire_hand, commit_mask);
				casts::ArtDriver::observe_graph_event(eventHolder->As<RE::Actor>(), a_event->tag);

				if (a_spellfire_hand != casts::SpellFireHand::none) {
					// The driver snapshot travels with the arming. Without it the mask alone
					// accepts a vanilla release in an armed hand — a staff's own attack after a
					// hotbar cast — and writes it into the commitment log as ours.
					casts::CastingController::notify_spellfire(
						a_spellfire_hand, a_arming.generation, a_driver_cast_active);
				}
			}

			//if (eventHolder->GetFormID() == 0x14) { //playerref
			//	logger::debug("AnimationGraphEvent: {}, {}, {}", std::string(a_event->tag), eventHolder->GetFormID(), std::string(a_event->payload));
			//}
		}
	}

	RE::BSEventNotifyControl Animation_event_hook::ProcessEvent_PC(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_sink, RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_eventSource)
	{
		// A Driver Cast's clip raises SpellFire for the hand it was authored for. Vanilla
		// processes that event before this observer, which completes an equipped spell in
		// THAT hand. Isolate first, then skip vanilla for this one event so MSCO and the
		// engine do not also fire; SH2 still delivers via CastSpellImmediate.
		//
		// Per-hand. The event names the hand, and the matching caster is the one to
		// interrupt — otherwise a right-hand clip leaves the right caster free to complete
		// an equipped right-hand spell alongside SH2's payload.
		//
		// Decoded ONCE here and answered against ONE arming snapshot, which every per-hand
		// question downstream reuses — isolation, the graph-side commitment point, and the
		// delivery latch must not disagree about which hands this cast holds because they
		// each read the mask at a different instant.
		const casts::SpellFireHand event_hand = a_event
			? casts::spellfire_hand_for_tag(a_event->tag.c_str() ? a_event->tag.c_str() : "")
			: casts::SpellFireHand::none;
		const auto arming = casts::CastingController::spellfire_arming();
		// Read ONCE, like the arming beside it. Isolation and acceptance are the same question —
		// is this event a driver cast's own? — and answering it from two reads of two different
		// terms lets acceptance say yes where isolation said no. Our own state_active only moves
		// inside `observe_graph_event`, further down this call, so the snapshot holds for the
		// whole event.
		const bool driver_cast_active = casts::MscoCastDriver::is_active();
		const casts::SpellFireHand isolate =
			(a_event && a_event->holder && a_event->holder->IsPlayerRef())
				? casts::isolate_caster_before_vanilla_spellfire(
					  driver_cast_active, event_hand, arming.mask)
				: casts::SpellFireHand::none;
		// Same freshness rule as the commitment gate: interrupt and swallow only if no
		// re-arm landed since the snapshot — a stale event must not silence the NEXT
		// cast's caster. Falls through to the normal chain when stale, exactly as if the
		// hand had not been armed.
		if (isolate != casts::SpellFireHand::none &&
			casts::CastingController::spellfire_arming().generation == arming.generation) {
			const bool is_left = isolate == casts::SpellFireHand::left;
			const auto source = is_left ? RE::MagicSystem::CastingSource::kLeftHand
										: RE::MagicSystem::CastingSource::kRightHand;
			if (auto* pc = const_cast<RE::TESObjectREFR*>(a_event->holder)->As<RE::PlayerCharacter>()) {
				if (auto* caster = pc->GetMagicCaster(source)) {
					caster->InterruptCast(true);
				}
			}
			logger::debug("SH2 cast: isolated {}-hand caster before vanilla SpellFire",
				is_left ? "left" : "right");
			ProcessEvent(a_event, a_eventSource, event_hand, arming, driver_cast_active);
			return RE::BSEventNotifyControl::kContinue;
		}

		// Chain first for every other event: earlier handlers must finish before
		// our observer reads it, so another mod's cleanup cannot land after our
		// state bookkeeping.
		const auto result = _ProcessEvent_PC(a_sink, a_event, a_eventSource);
		ProcessEvent(a_event, a_eventSource, event_hand, arming, driver_cast_active);
		return result;
	}

	/*RE::BSEventNotifyControl Animation_event_hook::ProcessEvent_NPC(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_sink, RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_eventSource)
	{
		ProcessEvent(a_event, a_eventSource);
		return _ProcessEvent_NPC(a_sink, a_event, a_eventSource);
	}*/

	void install()
	{
		Animation_event_hook::install();
		Attack_seam_hook::install();
	}
}
