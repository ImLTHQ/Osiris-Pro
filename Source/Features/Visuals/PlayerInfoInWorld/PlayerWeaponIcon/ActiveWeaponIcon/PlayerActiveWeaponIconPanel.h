#pragma once

#include <utility>

#include <Common/Visibility.h>
#include <GameClient/Entities/C4.h>
#include <GameClient/Panorama/ImagePanel.h>
#include <Utils/CString.h>
#include <Utils/StringBuilder.h>
#include "PlayerActiveWeaponIconPanelContext.h"
#include "WeaponRarity/WeaponRarity.h"

template <typename HookContext, typename Context = PlayerActiveWeaponIconPanelContext<HookContext>>
class PlayerActiveWeaponIconPanel {
public:
    template <typename... Args>
    explicit PlayerActiveWeaponIconPanel(Args&&... args) noexcept
        : context{std::forward<Args>(args)...}
    {
    }

    void update(auto&& playerPawn, Visibility bombIconVisibility) const noexcept
    {
        auto&& activeWeapon = playerPawn.getActiveWeapon();
        if (!context.config().template getVariable<player_info_vars::ActiveWeaponIconEnabled>() || (bombIconVisibility == Visibility::Visible && activeWeapon.template is<C4>())) {
            context.panel().setVisible(false);
            return;
        }

        auto weaponName = CString{activeWeapon.getName()};
        if (!weaponName.string)
            return;
        weaponName.skipPrefix("weapon_");

        context.panel().setVisible(true);

        StringBuilderStorage<100> weaponIconPathStorage;
        auto weaponIconPathBuilder = weaponIconPathStorage.builder();
        weaponIconPathBuilder.put("s2r://panorama/images/icons/equipment/", weaponName.string, ".svg");
        const auto weaponIconPath = weaponIconPathBuilder.cstring();

        auto&& weaponIconImagePanel = context.panel().clientPanel().template as<ImagePanel>();
        const auto iconColor = getWeaponIconColor(weaponName, activeWeapon);
        const bool shouldUpdateImage = shouldUpdateImagePanel(weaponIconImagePanel, weaponIconPath);
        const bool shouldUpdateColor = context.cache().activeWeaponIconColor(iconColor);
        if (shouldUpdateImage || shouldUpdateColor)
            weaponIconImagePanel.setImageSvg(SvgImageParams{.imageUrl = weaponIconPath, .textureHeight = kIconTextureHeight, .fillColor = iconColor});
    }

private:
    [[nodiscard]] static cs2::Color getWeaponIconColor(auto&& weaponName, auto&& activeWeapon) noexcept
    {
        using namespace weapon_rarity;
        if (isKnifeWeaponName(weaponName.string))
            return weaponRarityColor(WeaponRarity::Gold);
        return weaponRarityColor(paintKitRarity(activeWeapon.paintKit().valueOr(0)));
    }

    [[nodiscard]] bool shouldUpdateImagePanel(auto&& imagePanel, const char* newImagePath) const noexcept
    {
        return imagePanel.getImagePath() != newImagePath;
    }

    static constexpr auto kIconTextureHeight{24};

    Context context;
};
