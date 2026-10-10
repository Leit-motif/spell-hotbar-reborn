#pragma once
#include "../casts/casting_controller.h"
#include "../casts/combo_cache.h"
#include "../logger/logger.h"
#include "../runtime_hooks.h"


namespace SpellHotbar::events {

	//credits https://github.com/D7ry/PayloadInterpreter/
	class Animation_event_hook
	{
	public:
		static void install()
		{
			logger::info("Installing animation event hook...");
			//REL::Relocation<uintptr_t> AnimEventVtbl_NPC{ RE::VTABLE_Character[2] };
			REL::Relocation<uintptr_t> AnimEventVtbl_PC{ RE::VTABLE_PlayerCharacter[2] };

			//_ProcessEvent_NPC = AnimEventVtbl_NPC.write_vfunc(0x1, ProcessEvent_NPC);
			_ProcessEvent_PC = RuntimeHooks::write_verified_vfunc(AnimEventVtbl_PC, 0x1,
				ProcessEvent_PC, "PlayerCharacter::ProcessAnimationEvent");
			logger::info("...animation event hook installed");
		}

	private:
		// The SpellFire hand, the arming snapshot, and whether a driver cast holds the graph
		// are read once by ProcessEvent_PC and handed down, so isolation and commitment answer
		// from the same read.
		static void ProcessEvent(RE::BSAnimationGraphEvent* a_event,
			RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_eventSource,
			casts::SpellFireHand a_spellfire_hand,
			casts::CastingController::SpellFireArming a_arming,
			bool a_driver_cast_active);

		static RE::BSEventNotifyControl ProcessEvent_PC(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_sink, RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_eventSource);
		//static RE::BSEventNotifyControl ProcessEvent_NPC(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_sink, RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_eventSource);
		
		//static inline REL::Relocation<decltype(ProcessEvent_NPC)> _ProcessEvent_NPC;
		static inline REL::Relocation<decltype(ProcessEvent_PC)> _ProcessEvent_PC;
	};

	void install();
}
