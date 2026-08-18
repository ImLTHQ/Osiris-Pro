#pragma once

#include <tuple>

#include "ActiveWeaponAmmo/PlayerActiveWeaponAmmoPanel.h"
#include "PlayerHealth/PlayerHealthPanel.h"
#include "PlayerStateIcons/PlayerStateIconsPanel.h"
#include "PlayerWeaponIcon/PlayerWeaponIconPanel.h"

template <typename HookContext>
using PlayerInfoPanelTypes = std::tuple<
    PlayerHealthPanel<HookContext>,
    PlayerWeaponIconPanel<HookContext>,
    PlayerActiveWeaponAmmoPanel<HookContext>,
    PlayerStateIconsPanel<HookContext>>;
