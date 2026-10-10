#include "runtime_hooks.h"
#include <array>
#include <iostream>

// Exercise the actual linked CommonLib and production selectors without launching Skyrim.
int main(int argc, char** argv)
{
    for (const auto version : {REL::Version{1, 5, 97, 0}, REL::Version{1, 6, 1170, 0},
        REL::Version{1, 7, 99, 0}, REL::Version{1, 7, 104, 0}}) {
        if (!REL::Module::mock(version)) return 1;
        const bool ae = version[1] >= 6;
        const bool modern = version >= SKSE::RUNTIME_SSE_1_7_99;
        if (REL::Module::IsAE() != ae || SpellHotbar::RuntimeHooks::main_loop_offset() !=
            (modern ? 0xC38 : ae ? 0xC26 : 0x748)) return 2;
        alignas(16) std::array<std::byte, 0x200> memory{};
        auto* map = reinterpret_cast<RE::ControlMap*>(memory.data());
        auto& data = map->GetRuntimeData();
        if (reinterpret_cast<std::byte*>(&data) - memory.data() != (ae ? 0xF0 : 0xE8)) return 3;
        data.enabledControls = RE::UserEvents::USER_EVENT_FLAG::kMovement;
        data.textEntryCount = 2;
        if (!map->IsMovementControlsEnabled() || data.textEntryCount != 2) return 4;
        data.enabledControls.reset(RE::UserEvents::USER_EVENT_FLAG::kMovement);
        if (map->IsMovementControlsEnabled()) return 5;
        alignas(16) std::array<std::byte, 0x900> queue_memory{};
        auto* queue = reinterpret_cast<RE::BSInputEventQueue*>(queue_memory.data());
        if (reinterpret_cast<std::byte*>(&queue->GetRuntimeData()) - queue_memory.data() != (modern ? 0x28 : 0x20)) return 10;
        if (REL::VersionShift(4, RE::PlayerInputHandler::kAE1799AddedVFuncCount, SKSE::RUNTIME_SSE_1_7_99) != (modern ? 6 : 4)) return 11;
        alignas(16) std::array<std::byte, 0x500> vm_memory{};
        auto* vm = reinterpret_cast<RE::SkyrimVM*>(vm_memory.data());
        if (reinterpret_cast<std::byte*>(&vm->GetVMRuntimeData()) - vm_memory.data() != (modern ? 0x210 : 0x200)) return 12;
        std::cout << "PASS selectors/layout " << version.string() << '\n';
    }
    if (argc == 1) return 0;
    if (argc != 2) return 6;
    for (const auto version : {REL::Version{1, 5, 97, 0}, REL::Version{1, 6, 1170, 0}, REL::Version{1, 7, 104, 0}}) {
        if (!REL::Module::mock(version)) return 7;
        const auto name = std::format("{}-{}.bin", REL::Module::IsAE() ? "versionlib" : "version", version.string());
        if (!REL::IDDB::inject((std::filesystem::path(argv[1]) / name).wstring(), version)) return 8;
        for (const auto& id : {RE::VTABLE_PlayerCharacter[2], RE::VTABLE_PlayerCharacter[3], RE::VTABLE_hkbClipGenerator[0],
            RE::VTABLE_bhkCharProxyController[0], RE::VTABLE_bhkCharProxyController[1],
            RE::VTABLE_bhkCharRigidBodyController[0], RE::VTABLE_bhkCharRigidBodyController[1],
            REL::VariantID(35565, 36564, 0), REL::VariantID(76765, 21873, 0), REL::VariantID(67315, 68617, 0),
            REL::VariantID(67355, 68655, 0), REL::VariantID(523660, 410199, 0)}) {
            if (!id.offset()) return 9;
        }
        std::cout << "PASS linked database " << name << '\n';
    }
}
