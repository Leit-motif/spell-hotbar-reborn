#include "attack_seam_hook.h"

#include <string_view>

#include "../casts/attack_seam.h"
#include "../casts/hyper_armor_sync.h"
#include "../casts/seam_dispatch.h"
#include "../logger/logger.h"

namespace SpellHotbar::events {

	bool Attack_seam_hook::NotifyAnimationGraph_PC(
		RE::IAnimationGraphManagerHolder* a_holder, const RE::BSFixedString& a_eventName)
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		// `this` is the holder subobject, not the actor, so the comparison has to be made
		// against the same subobject of the player. Everything else on the graph -- every NPC,
		// every creature -- must reach the original untouched.
		const bool is_player =
			pc != nullptr && a_holder == static_cast<RE::IAnimationGraphManagerHolder*>(pc);
		if (!is_player) {
			return _NotifyAnimationGraph_PC(a_holder, a_eventName);
		}

		const char* raw = a_eventName.c_str();
		const std::string_view name{ raw ? raw : "" };

		// Ability hyperarmor. Every stagger on this stack but ragdoll reaches the player's graph
		// through this call -- the engine's hit stagger, Chocolate Poise, the parry mods, For
		// Honor's Papyrus -- so dropping it here is the whole of stagger immunity. Ahead of the
		// seam logic: a stagger that never happens must not tear anything down either.
		if (casts::HyperArmor::try_swallow_stagger(name, _ReturnAddress())) {
			return false;
		}

		if (casts::seam_cut_in_progress()) {
			return _NotifyAnimationGraph_PC(a_holder, a_eventName);
		}

		const auto kind = casts::classify_attack_seam_event(name);
		if (kind == casts::AttackSeamKind::none) {
			return _NotifyAnimationGraph_PC(a_holder, a_eventName);
		}

		// The policy lives in `seam_dispatch` so the idle hook runs the identical teardown; this
		// hook decodes the event and obeys the disposition, nothing more.
		if (casts::apply_attack_seam(pc, name, kind, "virtual") ==
			casts::SeamDisposition::capture) {
			return false;
		}

		const bool accepted = _NotifyAnimationGraph_PC(a_holder, a_eventName);
		casts::note_attack_forwarded(pc, kind, accepted);
		return accepted;
	}

	void Attack_seam_hook::install()
	{
		logger::info("Installing attack seam hook...");
		REL::Relocation<uintptr_t> NotifyVtbl_PC{ RE::VTABLE_PlayerCharacter[3] };
		_NotifyAnimationGraph_PC = NotifyVtbl_PC.write_vfunc(0x1, NotifyAnimationGraph_PC);
		logger::info("...attack seam hook installed");
	}

}
