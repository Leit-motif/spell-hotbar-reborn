#pragma once

// RE::ControlMap's tail, read at the right offset for the running game.
//
// CommonLibSSE-NG (pin b93280e) declares ControlMap with the 1.5.97 layout: the AE-only
// `kMarketplace` input context sits behind `#ifdef SKYRIM_SUPPORT_AE`, which NG never defines, so
// `controlMap[]` is one slot short and every member after it is 8 bytes early from 1.6.1130 on.
// There `enabledControls` (0x118 in the header) actually holds `contextPriorityStack`'s size, so
// `IsMovementControlsEnabled()` passed only while that size happened to be odd -- every hotbar press
// and the HUD bar's visibility gated on it. Upstream's own comments call the Fighting check "not
// working in 1170" and the context-stack read a crash on 1170; this is that bug.
//
// alandtse/CommonLibVR `ng`, include/RE/C/ControlMap.h, places the same RUNTIME_DATA block (linked
// mappings, priority stack, enabled controls, text-entry count) at 0xE8 below 1.6.1130, 0xF0 from
// 1.6.1130, and 0x108 on VR. Read every ControlMap member after `controlMap[]` through here, never
// directly, so the header's SE offsets never reach a running AE game.
namespace SpellHotbar::Input::ControlMapCompat {
    inline constexpr std::size_t kSeDataOffset = 0xE8;
    inline constexpr std::size_t kAe1130DataOffset = 0xF0;
    inline constexpr std::size_t kVrDataOffset = 0x108;
    inline constexpr std::size_t kEnabledControls = 0x30;  // within the runtime data
    inline constexpr std::size_t kTextEntryCount = 0x38;

    // The header is the SE layout; if a CommonLib update fixes it, these fail and this file goes.
    static_assert(offsetof(RE::ControlMap, enabledControls) == kSeDataOffset + kEnabledControls);
    static_assert(offsetof(RE::ControlMap, textEntryCount) == kSeDataOffset + kTextEntryCount);

    [[nodiscard]] inline std::size_t data_offset() noexcept
    {
        if (REL::Module::IsVR()) {
            return kVrDataOffset;
        }
        return REL::Module::get().version() >= REL::Version(1, 6, 1130, 0) ? kAe1130DataOffset : kSeDataOffset;
    }

    template <class T>
    [[nodiscard]] inline T read(const RE::ControlMap* a_map, std::size_t a_field) noexcept
    {
        return *reinterpret_cast<const T*>(reinterpret_cast<std::uintptr_t>(a_map) + data_offset() + a_field);
    }

    [[nodiscard]] inline bool movement_controls_enabled(const RE::ControlMap* a_map) noexcept
    {
        const auto movement = static_cast<std::uint32_t>(RE::UserEvents::USER_EVENT_FLAG::kMovement);
        return (read<std::uint32_t>(a_map, kEnabledControls) & movement) == movement;
    }

    [[nodiscard]] inline std::int8_t text_entry_count(const RE::ControlMap* a_map) noexcept
    {
        return read<std::int8_t>(a_map, kTextEntryCount);
    }
}
