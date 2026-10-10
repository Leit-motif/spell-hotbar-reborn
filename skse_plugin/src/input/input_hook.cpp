#include "input_hook.h"

#include "input.h"
#include "../logger/logger.h"
#include "../runtime_hooks.h"

namespace SpellHotbar::Input {
    namespace {
        // From https://github.com/SlavicPotato/ied-dev and https://github.com/D7ry/wheeler: the
        // engine dispatches one frame's input as a linked list, so a filter that unlinks a node
        // hides that event from every handler downstream of this call.
        struct DispatchInputEventHook {
            static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent** a_events)
            {
                if (a_events != nullptr && *a_events != nullptr) {
                    RE::InputEvent* prev = nullptr;
                    RE::InputEvent* event = *a_events;
                    while (event != nullptr) {
                        // process_event may splice an injected event in right after this one
                        // (input_event_adapter.h), so `next` is read after the call, and a captured
                        // event's successor -- injected or not -- stays on the list.
                        const bool capture = process_event(event);
                        RE::InputEvent* next = event->next;
                        if (capture) {
                            if (prev != nullptr) {
                                prev->next = next;
                            } else {
                                *a_events = next;
                            }
                        } else {
                            prev = event;
                        }
                        event = next;
                    }
                }
                func(a_dispatcher, a_events);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };
    }

    void install_hook()
    {
        const REL::Relocation<std::uintptr_t> caller{ RELOCATION_ID(67315, 68617) };
        const REL::Relocation<std::uintptr_t> callee{ RELOCATION_ID(67355, 68655) };
        DispatchInputEventHook::func = RuntimeHooks::write_verified_call(
            caller.address() + 0x7B, callee.address(), DispatchInputEventHook::thunk,
            "Input::Dispatch");
        logger::info("SH2 input: hooked the input dispatch site (67315+0x7B)");
    }
}
