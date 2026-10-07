#pragma once

#include <cstdint>

#include "../game_data/equipped_type.h"

namespace SpellHotbar::Bars {

// Pure stance routing shared by the runtime selector and its regression tests. A single
// one-handed weapon is the Melee parent stance; only two melee weapons select Dual Wield.
[[nodiscard]] constexpr std::uint32_t stance_bar_for_equipped_type(GameData::EquippedType equipped) noexcept
{
    using GameData::EquippedType;
    switch (equipped) {
    case EquippedType::ONEHAND_EMPTY:
        return static_cast<std::uint32_t>('MELE');
    case EquippedType::ONEHAND_SHIELD:
        return static_cast<std::uint32_t>('1HSD');
    case EquippedType::ONEHAND_SPELL:
        return static_cast<std::uint32_t>('1HSP');
    case EquippedType::DUAL_WIELD:
        return static_cast<std::uint32_t>('1HDW');
    case EquippedType::TWOHAND:
        return static_cast<std::uint32_t>('2HND');
    case EquippedType::BOW:
    case EquippedType::CROSSBOW:
        return static_cast<std::uint32_t>('RNGD');
    case EquippedType::SPELL:
    case EquippedType::STAFF_SHIELD:
    case EquippedType::STAFF_LEFT:
        return static_cast<std::uint32_t>('MAGC');
    case EquippedType::FIST:
    default:
        return static_cast<std::uint32_t>('MAIN');
    }
}

}  // namespace SpellHotbar::Bars
