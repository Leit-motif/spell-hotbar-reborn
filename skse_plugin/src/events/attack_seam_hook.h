#pragma once

namespace SpellHotbar::events {

	/**
	 * The send-side attack seam.
	 *
	 * Every attack source ends up in the same place: the mod's handler decides "that press was
	 * an attack" and then starts it on the player's behaviour graph. Vanilla's
	 * `AttackBlockHandler`, One Click Power Attack, MCO's directional power attacks and TK Dodge
	 * RE all do that by sending a graph event, which arrives at
	 * `IAnimationGraphManagerHolder::NotifyAnimationGraph` -- vfunc 0x1 on the holder base at
	 * offset 0x38, index 3 in `VTABLE_PlayerCharacter` (index 2 is the
	 * `BSTEventSink<BSAnimationGraphEvent>` base `Animation_event_hook` uses).
	 *
	 * Hooking there replaces reading OCPA's and TK Dodge's ini files to guess which key was an
	 * attack: the seam sees the decision, not the key, so a source SH2 has never heard of works
	 * for free.
	 *
	 * BFCO is covered here too. BFCO starts its attacks by playing a `TESIdleForm` from
	 * `SCSI-ACTbfco-Main.esp` through `AIProcess::SetupSpecialIdle`, which looks like a path
	 * that bypasses this virtual. It does not: the engine plays a special idle by sending the
	 * leaf's ENAM through this very `NotifyAnimationGraph`, so a live BFCO swing arrives here as
	 * `attackStart` + `BFCOAttackstart_1`, the power attack as `attackPowerStartInPlace`, and the
	 * block as `blockStart` -- measured with a one-run census in this hook. Do not detour
	 * `SetupSpecialIdle` with CommonLib's `write_branch<5>`: it is a call-site patcher, not a
	 * prologue detour (it reads the callee's first bytes as a rel32 and forwards to garbage), and
	 * such a detour crashes on load. The teardown policy itself lives in `casts/seam_dispatch`.
	 */
	class Attack_seam_hook
	{
	public:
		static void install();

	private:
		static bool NotifyAnimationGraph_PC(
			RE::IAnimationGraphManagerHolder* a_holder, const RE::BSFixedString& a_eventName);

		static inline REL::Relocation<decltype(NotifyAnimationGraph_PC)> _NotifyAnimationGraph_PC;
	};

}
