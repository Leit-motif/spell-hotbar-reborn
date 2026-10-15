#pragma once
#include <cstdint>

namespace SpellHotbar::Input::HostFilterState {
    // API 4 has no aggregate capture query. Observe an audited stock host's existing
    // dispatch state, read-only, instead of sending fabricated events through its UI.
    void install();
    void observe(std::uintptr_t return_address);
    bool allows_buffered_input();
}
