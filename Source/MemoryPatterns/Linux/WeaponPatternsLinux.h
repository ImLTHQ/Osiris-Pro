#pragma once

#include <MemoryPatterns/PatternTypes/WeaponPatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct WeaponPatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        // Note: OffsetToWeaponPaintKit is intentionally not registered on Linux.
        // A Linux signature has not been derived yet, and registering an unverified pattern could
        // silently match the wrong code. See kWeaponPaintKitOffsetPatternRegistered - on Linux the
        // active weapon icon falls back to the stock rarity color.
        return clientPatterns
            .template addPattern<OffsetToClipAmmo, CodePattern{"74 ? 8B 87 ? ? ? ? C3"}.add(4).read()>()
            .template addPattern<OffsetToWeaponSceneObjectUpdaterHandle, CodePattern{"48 89 83 ? ? ? ? BE ? ? ? ? 48 89 DF"}.add(3).read()>()
            .template addPattern<PointerToGetInaccuracyFunction, CodePattern{"55 48 89 E5 41 57 41 56 49 89 ? 41 55 49 89 ? 41 54 53 48 89 FB 48 83 EC ? E8"}>()
            .template addPattern<PointerToGetSpreadFunction, CodePattern{"55 48 89 E5 48 83 EC ? 48 63"}>();
    }
};
