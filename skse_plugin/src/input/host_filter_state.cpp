#include "host_filter_state.h"
#include "../runtime_hooks.h"
#include "../flick/flick_watch.h"
#include <bcrypt.h>
#include <array>
#include <atomic>

namespace SpellHotbar::Input::HostFilterState {
    namespace {
        std::uintptr_t host_begin{}, host_end{};
        const std::uint8_t* blocking_byte{};
        std::atomic_bool host_present{false}, observed{false}, blocked{true};

        // Stock FLICK distributed DLL SHA256:
        // 927df9ada96df7a298628814e3153a4a06ab0180501cb6f27c06d166d52e658b.
        // The complete input thunk has no base relocations, so ASLR preserves its digest.
        // A different build deliberately disables buffered delivery; ordinary filtered
        // presses continue to work. Never guess another build's private state layout.
        constexpr std::uintptr_t thunk_rva = 0xAB760;
        constexpr std::size_t thunk_size = 0x752;
        constexpr std::array<std::uint8_t, 32> digest{
            0x11,0x0f,0x84,0xb6,0x80,0x2f,0xf8,0xb0,0x8f,0x52,0x07,0xaf,0x93,0x98,0xc7,0x17,
            0x3f,0x7a,0x31,0x73,0xc9,0xbd,0x61,0x5f,0x59,0x31,0x87,0xc2,0x1f,0xd2,0x87,0xd8};

        bool matches_digest(std::uintptr_t address)
        {
            BCRYPT_ALG_HANDLE algorithm{};
            if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
            std::array<std::uint8_t, 32> actual{};
            BCRYPT_HASH_HANDLE hash{};
            auto status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
            if (status >= 0) status = BCryptHashData(hash, reinterpret_cast<PUCHAR>(address), static_cast<ULONG>(thunk_size), 0);
            if (status >= 0) status = BCryptFinishHash(hash, actual.data(), static_cast<ULONG>(actual.size()), 0);
            if (hash) BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return status >= 0 && actual == digest;
        }

        std::uintptr_t rip_target(std::uintptr_t instruction, std::size_t displacement_offset,
            std::size_t instruction_size)
        {
            std::int32_t displacement{};
            std::memcpy(&displacement, reinterpret_cast<void*>(instruction + displacement_offset), 4);
            return instruction + instruction_size + displacement;
        }
    }

    void install()
    {
        blocking_byte = nullptr;
        observed.store(false);
        blocked.store(true);
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"FUCK.dll"));
        host_present.store(base != 0);
        if (!base) return;
        const auto begin = base + thunk_rva;
        if (RuntimeHooks::readable(begin, thunk_size) && RuntimeHooks::executable(begin) && matches_digest(begin)) {
            // Decode all three independent references, rather than trusting a data RVA.
            const auto target = rip_target(begin + 0x9C, 2, 6);
            const auto compare = rip_target(begin + 0xB3, 2, 7);
            const auto set = rip_target(begin + 0xC4, 2, 7);
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            const auto* sections = IMAGE_FIRST_SECTION(nt);
            bool in_data = false;
            for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
                const auto& section = sections[i];
                if (std::memcmp(section.Name, ".data", 5) == 0 &&
                    (section.Characteristics & IMAGE_SCN_MEM_WRITE) &&
                    target >= base + section.VirtualAddress &&
                    target < base + section.VirtualAddress + section.Misc.VirtualSize) in_data = true;
            }
            if (target == compare && target == set && in_data && RuntimeHooks::readable(target, 1)) {
                host_begin = begin;
                host_end = begin + thunk_size;
                blocking_byte = reinterpret_cast<const std::uint8_t*>(target);
                SKSE::log::info("SH2 input: stock FLICK capture snapshot verified (read-only)");
                return;
            }
        }
        SKSE::log::warn("SH2 input: unfamiliar FLICK input hook; buffered casts disabled. Draw weapons before pressing a spell slot. Fresh filtered presses remain available.");
    }

    void observe(std::uintptr_t return_address)
    {
        if (!host_present.load()) return;
        // The stock host must actually be on this dispatch stack. Direct calls from other
        // sources cannot refresh the policy snapshot. Host memory is read only here, on
        // its own dispatch thread; other callers only read our atomic snapshot.
        const bool admitted_stack = blocking_byte && return_address >= host_begin && return_address < host_end;
        const bool now_blocked = !admitted_stack || *blocking_byte != 0;
        const bool was_blocked = blocked.exchange(now_blocked);
        observed.store(true);
        if (now_blocked && !was_blocked) Flick::invalidate_input_context();
    }

    bool allows_buffered_input()
    {
        return !host_present.load() || (blocking_byte && observed.load() && !blocked.load());
    }
}
