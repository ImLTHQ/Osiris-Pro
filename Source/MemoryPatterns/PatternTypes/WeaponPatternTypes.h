#pragma once

#include <cstdint>

#include <CS2/Classes/Entities/C_CSWeaponBase.h>
#include <Platform/Macros/IsPlatform.h>
#include <Utils/FieldOffset.h>
#include <Utils/StrongTypeAlias.h>

template <typename FieldType, typename OffsetType>
using WeaponOffset = FieldOffset<cs2::C_CSWeaponBase, FieldType, OffsetType>;

STRONG_TYPE_ALIAS(OffsetToClipAmmo, WeaponOffset<cs2::C_CSWeaponBase::m_iClip1, std::int32_t>);
STRONG_TYPE_ALIAS(OffsetToWeaponPaintKit, WeaponOffset<cs2::C_CSWeaponBase::m_nFallbackPaintKit, std::int32_t>);
STRONG_TYPE_ALIAS(OffsetToWeaponSceneObjectUpdaterHandle, WeaponOffset<cs2::C_CSWeaponBase::sceneObjectUpdaterHandle, std::int32_t>);
STRONG_TYPE_ALIAS(PointerToGetInaccuracyFunction, cs2::C_CSWeaponBase::GetInaccuracy*);
STRONG_TYPE_ALIAS(PointerToGetSpreadFunction, cs2::C_CSWeaponBase::GetSpread*);

// The paint kit offset pattern currently exists only on Windows - a Linux signature has not been
// derived yet. On Linux the feature falls back to the stock rarity color.
inline constexpr bool kWeaponPaintKitOffsetPatternRegistered = IS_WIN64();
