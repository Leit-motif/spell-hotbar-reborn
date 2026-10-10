#include "runtime_hooks.h"
#include <array>
#include <iostream>

int main()
{
    namespace Hooks = SpellHotbar::RuntimeHooks;
    auto* memory = static_cast<std::byte*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!memory) return 1;
    const auto site = reinterpret_cast<std::uintptr_t>(memory);
    const auto expected = site + 64;
    // The first 128 bytes stand in for the game image; anything past them is plugin code.
    const Hooks::Image image{ site, site + 128 };
    const auto encode = [&](std::uintptr_t target) {
        memory[0] = std::byte{0xE8};
        const auto displacement = static_cast<std::int32_t>(target - site - 5);
        std::memcpy(memory + 1, &displacement, 4);
    };
    encode(expected);
    if (!Hooks::call_error(site, expected, image).empty()) return 2;
    memory[0] = std::byte{0x90};
    if (Hooks::call_error(site, expected, image).empty()) return 3;
    encode(site + 96); // An E8 to a different GAME callee MUST fail (1.7 +C26).
    const std::array<std::byte, 5> before{memory[0], memory[1], memory[2], memory[3], memory[4]};
    if (Hooks::call_error(site, expected, image).empty()) return 4;
    if (std::memcmp(before.data(), memory, before.size()) != 0) return 11;
    // An earlier plugin's CommonLib trampoline: E8 -> FF25 [RIP+2] -> hook. Any holder is preserved.
    encode(site + 128);
    memory[128] = std::byte{0xFF}; memory[129] = std::byte{0x25};
    const std::int32_t displacement = 2;
    std::memcpy(memory + 130, &displacement, 4);
    const auto hook = reinterpret_cast<std::uintptr_t>(&main);
    std::memcpy(memory + 136, &hook, sizeof(hook));
    if (!Hooks::call_error(site, expected, image).empty()) return 5;
    if (Hooks::chain_holder(site + 128) != Hooks::module_name(hook)) return 6;
    // Another stub shape outside the image is a chain too; it is named by address when unattributed.
    memory[128] = std::byte{0x48};
    if (!Hooks::call_error(site, expected, image).empty()) return 7;
    if (!Hooks::chain_holder(site + 128).starts_with("unattributed code at ")) return 12;
    DWORD previous{};
    if (!VirtualProtect(memory + 64, 4096 - 64, PAGE_READWRITE, &previous)) return 8;
    if (Hooks::call_error(site, expected, image).empty()) return 9;
    VirtualFree(memory, 0, MEM_RELEASE);
    if (Hooks::call_error(site, expected, image).empty()) return 10;
    std::cout << "PASS hook guard: E8 callee, wrong opcode, wrong game callee, any-holder chain, memory protections\n";
}
