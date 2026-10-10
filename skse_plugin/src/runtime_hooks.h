#pragma once

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
#include <Windows.h>
#include <cstring>

namespace SpellHotbar::RuntimeHooks {
    [[noreturn]] inline void fail(std::string_view name, std::string_view reason)
    {
        SKSE::stl::report_and_fail(std::format("SpellHotbar2 hook {}: {} (Skyrim {})",
            name, reason, REL::Module::get().version().string()));
    }

    inline bool readable(std::uintptr_t address, std::size_t size)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        const auto end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        return address < end && size <= end - address;
    }

    inline bool executable(std::uintptr_t address)
    {
        MEMORY_BASIC_INFORMATION info{};
        constexpr DWORD flags = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        return VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) &&
            info.State == MEM_COMMIT && (info.Protect & flags) && !(info.Protect & PAGE_GUARD);
    }

    inline std::string module_name(std::uintptr_t address)
    {
        HMODULE module{};
        wchar_t path[MAX_PATH]{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address), &module) || !GetModuleFileNameW(module, path, MAX_PATH)) return {};
        return std::filesystem::path(path).filename().string();
    }

    template <class F>
    std::uintptr_t write_verified_vfunc(REL::Relocation<std::uintptr_t>& table,
        std::size_t slot, F hook, std::string_view name)
    {
        const auto address = table.address() + slot * sizeof(std::uintptr_t);
        if (!readable(address, sizeof(std::uintptr_t))) fail(name, "vtable slot is unreadable");
        std::uintptr_t original{};
        std::memcpy(&original, reinterpret_cast<void*>(address), sizeof(original));
        if (!executable(original)) fail(name, "vtable original is not executable");
        SKSE::log::info("SH2 hook {}: runtime={} vtableRVA={:X} slot={:X} original={:X} module={}",
            name, REL::Module::get().version().string(), table.address() - REL::Module::get().base(),
            slot, original, module_name(original));
        return table.write_vfunc(slot, hook);
    }

    // The 1.7.104 main loop inserted 0x12 bytes before the legacy AE timer call.
    // +0xC26 is still E8, but calls a different, argument-taking function (ID 68088).
    inline std::size_t main_loop_offset()
    {
        if (REL::Module::IsSE()) return 0x748;
        return REL::Module::get().version() >= SKSE::RUNTIME_SSE_1_7_99 ? 0xC38 : 0xC26;
    }

    // Decode E8 rel32 explicitly. A five-byte write is unsafe for any other instruction.
    inline std::uintptr_t call_target(std::uintptr_t site)
    {
        std::int32_t displacement{};
        std::memcpy(&displacement, reinterpret_cast<void*>(site + 1), sizeof(displacement));
        return site + 5 + displacement;
    }

    // CommonLib's write_call<5> uses E8 -> FF25 [RIP+disp32] -> pointer to the hook.
    // Used only to name an earlier holder in the log; acceptance does not depend on the shape.
    inline std::uintptr_t trampoline_target(std::uintptr_t address)
    {
        if (!readable(address, 6) || !executable(address) ||
            *reinterpret_cast<const std::uint16_t*>(address) != 0x25FF) return 0;
        std::int32_t displacement{};
        std::memcpy(&displacement, reinterpret_cast<void*>(address + 2), sizeof(displacement));
        const auto pointer = address + 6 + displacement;
        if (!readable(pointer, sizeof(std::uintptr_t))) return 0;
        std::uintptr_t target{};
        std::memcpy(&target, reinterpret_cast<void*>(pointer), sizeof(target));
        return executable(target) ? target : 0;
    }

    struct Image { std::uintptr_t begin{}, end{}; };

    inline Image game_image()
    {
        const auto& module = REL::Module::get();
        const auto base = module.base();
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        return { base, base + nt->OptionalHeader.SizeOfImage };
    }

    // An earlier plugin's hook leaves the site calling OUT of the game image; that is a chain
    // to preserve, whatever plugin or stub shape wrote it. A callee INSIDE the image that is not
    // the audited function is a wrong site (1.7's +0xC26 called ID 68088) and is refused.
    // Pure validation result lets host tests exercise refusal without opening a fatal dialog.
    // The writer below performs NO mutation until this has succeeded.
    inline std::string_view call_error(std::uintptr_t site, std::uintptr_t expected, Image image)
    {
        if (!readable(site, 5) || !executable(site)) return "call site is not readable executable memory";
        if (*reinterpret_cast<const std::uint8_t*>(site) != 0xE8) return "expected E8 rel32 call before patching";
        const auto original = call_target(site);
        if (!executable(expected) || !executable(original)) return "call target is not executable";
        if (original != expected && original >= image.begin && original < image.end)
            return "E8 calls a different game function than the audited target";
        return {};
    }

    inline std::string chain_holder(std::uintptr_t original)
    {
        const auto chained = trampoline_target(original);
        auto holder = module_name(chained ? chained : original);
        return holder.empty() ? std::format("unattributed code at {:X}", chained ? chained : original) : holder;
    }

    inline void verify_call(std::uintptr_t site, std::uintptr_t expected, std::string_view name)
    {
        if (const auto error = call_error(site, expected, game_image()); !error.empty()) fail(name, error);
        const auto original = call_target(site);
        std::string holder = module_name(original);
        if (original != expected) {
            holder = chain_holder(original);
            SKSE::log::info("SH2 hook {}: preserving earlier hook chain owned by {}; pristine calleeRVA={:X}",
                name, holder, expected - REL::Module::get().base());
        }
        SKSE::log::info("SH2 hook {}: runtime={} callRVA={:X} original={:X} module={}",
            name, REL::Module::get().version().string(), site - REL::Module::get().base(), original, holder);
    }

    template <class F>
    std::uintptr_t write_verified_call(std::uintptr_t site, std::uintptr_t expected,
        F hook, std::string_view name)
    {
        verify_call(site, expected, name);
        return SKSE::GetTrampoline().write_call<5>(site, hook);
    }
}
