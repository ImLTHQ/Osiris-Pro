#pragma once

#include <cstdint>

#include <CS2/Classes/Color.h>

#include "WeaponPaintKitRarityTable.h"

namespace weapon_rarity
{

enum class WeaponRarity : std::uint8_t {
    Default,
    Consumer,
    Industrial,
    MilSpec,
    Restricted,
    Classified,
    Covert,
    Extraordinary,
    Gold
};

// Exact rarity colors taken from CS2's items_game.txt "colors" section:
//   desc_default   #ded6cc  (Stock)
//   desc_common    #b0c3d9  (Consumer Grade)
//   desc_uncommon  #5e98d9  (Industrial Grade)
//   desc_rare      #4b69ff  (Mil-Spec Grade)
//   desc_mythical  #8847ff  (Restricted)
//   desc_legendary #d32ce6  (Classified)
//   desc_ancient   #eb4b4b  (Covert)
//   desc_immortal  #e4ae39  (Extraordinary / Contraband / gloves)
//   desc_unusual   #ffd700  (Gold, "unusual" craft class - knives)
[[nodiscard]] constexpr cs2::Color weaponRarityColor(WeaponRarity rarity) noexcept
{
    switch (rarity) {
        using enum WeaponRarity;
        case Default: return cs2::Color{0xDE, 0xD6, 0xCC};
        case Consumer: return cs2::Color{0xB0, 0xC3, 0xD9};
        case Industrial: return cs2::Color{0x5E, 0x98, 0xD9};
        case MilSpec: return cs2::Color{0x4B, 0x69, 0xFF};
        case Restricted: return cs2::Color{0x88, 0x47, 0xFF};
        case Classified: return cs2::Color{0xD3, 0x2C, 0xE6};
        case Covert: return cs2::Color{0xEB, 0x4B, 0x4B};
        case Extraordinary: return cs2::Color{0xE4, 0xAE, 0x39};
        case Gold: return cs2::Color{0xFF, 0xD7, 0x00};
    }
    return cs2::Color{0xDE, 0xD6, 0xCC};
}

[[nodiscard]] constexpr WeaponRarity paintKitRarity(std::int32_t paintKit) noexcept
{
    if (paintKit < 0 || static_cast<std::size_t>(paintKit) >= kPaintKitRarities.size())
        return WeaponRarity::Default;
    return static_cast<WeaponRarity>(kPaintKitRarities[static_cast<std::size_t>(paintKit)]);
}

// Weapon names with the "weapon_" prefix stripped, e.g. "knife", "knifegg", "knife_bayonet".
[[nodiscard]] constexpr bool isKnifeWeaponName(const char* weaponName) noexcept
{
    return weaponName
        && weaponName[0] == 'k'
        && weaponName[1] == 'n'
        && weaponName[2] == 'i'
        && weaponName[3] == 'f'
        && weaponName[4] == 'e';
}

}
