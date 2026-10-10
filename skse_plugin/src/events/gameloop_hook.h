#pragma once
#include "../logger/logger.h"
#include "../runtime_hooks.h"

namespace SpellHotbar::events {
	//Credits to https://github.com/ersh1/OpenAnimationReplacer/

	inline float* deltaTime = nullptr;

	class GameLoopHook {
	public:
		static void hook() {
			logger::info("Hooking Main Loop...");

			const REL::Relocation<uintptr_t> mainHook{ REL::VariantID(35565, 36564, 0x5BAB10) };
			const REL::Relocation<uintptr_t> timerCallee{ RELOCATION_ID(76765, 21873) };
			const auto delta = REL::VariantID(523660, 410199, 0x30C3A08).address();
			if (!RuntimeHooks::readable(delta, sizeof(float))) RuntimeHooks::fail("deltaTime", "global is unreadable");
			deltaTime = reinterpret_cast<float*>(delta);
			logger::info("SH2 hook deltaTime: runtime={} globalRVA={:X}", REL::Module::get().version().string(), delta - REL::Module::get().base());
			// NO AllocTrampoline HERE. `plugin.cpp` allocates the one buffer, once, before any
			// hook installs. Allocating again does not extend the buffer -- it REPLACES it, and
			// when the old buffer came from CommonLib's own fallback allocation it is freed,
			// leaving every stub already written into it dangling.
			//
			// A second allocation here is harmless only while every other hook in this build is
			// a vtable write and nothing occupies the trampoline. With a real detour on
			// `AIProcess::SetupSpecialIdle` installed, a second call here crashed on the first
			// non-player special idle after every load (EXCEPTION_ACCESS_VIOLATION executing a
			// dead pointer), not save-specific.
			//
			// `trampoline_alloc_test` fails the build if a second call reappears anywhere.
			_Timinghook = RuntimeHooks::write_verified_call(mainHook.address() + RuntimeHooks::main_loop_offset(),
				timerCallee.address(), Timinghook, "MainLoop::Timer");

			logger::info("...done");
		}

	private:
		static void Timinghook();

		static inline REL::Relocation<decltype(Timinghook)> _Timinghook;
	};


}
