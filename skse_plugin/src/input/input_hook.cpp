#include "input_hook.h"

#include "input.h"
#include "../logger/logger.h"
#include "../runtime_hooks.h"
#include <MinHook.h>
#include "../flick/flick_watch.h"
#include "host_filter_state.h"
#include <intrin.h>

namespace SpellHotbar::Input {
    namespace {
        // From https://github.com/SlavicPotato/ied-dev and https://github.com/D7ry/wheeler: the
        // engine dispatches one frame's input as a linked list, so a filter that unlinks a node
        // hides that event from every handler downstream of this call.
        template <class Processor>
        void filter_events(RE::InputEvent** a_events, Processor process)
        {
            if (!a_events) return;
            RE::InputEvent* prev = nullptr;
            auto* event = *a_events;
            while (event) {
                const bool capture = process(event);
                auto* next = event->next;
                if (capture) {
                    if (prev) prev->next = next;
                    else *a_events = next;
                } else {
                    prev = event;
                }
                event = next;
            }
        }

        // The engine's dispatcher entry is reached by the existing FLICK hook after its
        // filtering. A real prologue detour preserves the displaced instructions; a
        // write_branch at a function entry would not. No host code or API table is patched.
        struct FilteredInputHook {
            static void thunk(RE::BSTEventSource<RE::InputEvent*>* dispatcher, RE::InputEvent** events)
            {
                HostFilterState::observe(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
                filter_events(events, process_event);
                func(dispatcher, events);
            }
            static inline decltype(&thunk) func = nullptr;
        };

        struct DispatchInputEventHook {
            static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent** a_events)
            {
                (void)Flick::input_context_revision();
                filter_events(a_events, process_raw_event);
                func(a_dispatcher, a_events);
                (void)Flick::input_context_revision();
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };
    }

    void install_hook()
    {
        HostFilterState::install();
        const REL::Relocation<std::uintptr_t> caller{ RELOCATION_ID(67315, 68617) };
        const REL::Relocation<std::uintptr_t> callee{ RELOCATION_ID(67355, 68655) };
        auto status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
            RuntimeHooks::fail("Input::FilteredDispatch", MH_StatusToString(status));
        }
        status = MH_CreateHook(reinterpret_cast<void*>(callee.address()),
            reinterpret_cast<void*>(&FilteredInputHook::thunk),
            reinterpret_cast<void**>(&FilteredInputHook::func));
        if (status != MH_OK) RuntimeHooks::fail("Input::FilteredDispatch", MH_StatusToString(status));
        status = MH_EnableHook(reinterpret_cast<void*>(callee.address()));
        if (status != MH_OK) RuntimeHooks::fail("Input::FilteredDispatch", MH_StatusToString(status));
        logger::info("SH2 input: gameplay installed after host filtering at dispatcher RVA {:X}",
            callee.address() - REL::Module::get().base());
        DispatchInputEventHook::func = RuntimeHooks::write_verified_call(
            caller.address() + 0x7B, callee.address(), DispatchInputEventHook::thunk,
            "Input::Dispatch");
        logger::info("SH2 input: hooked the input dispatch site (67315+0x7B)");
    }

    void dispatch_test_event(RE::InputEvent* event)
    {
        DispatchInputEventHook::thunk(RE::BSInputDeviceManager::GetSingleton(), &event);
    }

    bool host_allows_gameplay_input()
    {
        return HostFilterState::allows_buffered_input();
    }
}
