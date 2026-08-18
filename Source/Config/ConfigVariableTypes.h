#pragma once

#include <Features/Combat/SniperRifles/NoScopeInaccuracyVis/NoScopeInaccuracyVisConfigVariables.h>
#include <Features/Hud/BombPlantAlert/BombPlantAlertConfigVariables.h>
#include <Features/Hud/BombTimer/BombTimerConfigVariables.h>
#include <Features/Hud/DefusingAlert/DefusingAlertConfigVariables.h>
#include <Features/Hud/KillfeedPreserver/KillfeedPreserverConfigVariables.h>
#include <Features/Hud/PostRoundTimer/PostRoundTimerConfigVariables.h>
#include <Features/Sound/SoundVisualizationConfigVariables.h>
#include <Features/Visuals/OutlineGlow/OutlineGlowConfigVariables.h>
#include <Features/Visuals/PlayerInfoInWorld/PlayerInfoInWorldConfigVariables.h>
#include <Features/Visuals/ViewmodelMod/ViewmodelModConfigVariables.h>
#include <Utils/TypeList.h>

using ConfigVariableTypes = TypeList<
    BombTimerEnabled,
    DefusingAlertEnabled,
    KillfeedPreserverEnabled,
    PostRoundTimerEnabled,
    BombBeepSoundVisualizationEnabled,
    BombDefuseSoundVisualizationEnabled,
    BombPlantSoundVisualizationEnabled,
    FootstepSoundVisualizationEnabled,
    WeaponReloadSoundVisualizationEnabled,
    WeaponScopeSoundVisualizationEnabled,
    outline_glow_vars::Enabled,
    outline_glow_vars::GlowDefuseKits,
    outline_glow_vars::GlowDroppedBomb,
    outline_glow_vars::GlowGrenadeProjectiles,
    outline_glow_vars::GlowHostages,
    outline_glow_vars::GlowPlayers,
    outline_glow_vars::GlowOnlyEnemies,
    outline_glow_vars::PlayerGlowColorMode,
    outline_glow_vars::GlowTickingBomb,
    outline_glow_vars::GlowWeapons,
    outline_glow_vars::PlayerBlueHue,
    outline_glow_vars::PlayerGreenHue,
    outline_glow_vars::PlayerYellowHue,
    outline_glow_vars::PlayerOrangeHue,
    outline_glow_vars::PlayerPurpleHue,
    outline_glow_vars::TeamTHue,
    outline_glow_vars::TeamCTHue,
    outline_glow_vars::LowHealthHue,
    outline_glow_vars::HighHealthHue,
    outline_glow_vars::AllyHue,
    outline_glow_vars::EnemyHue,
    outline_glow_vars::MolotovHue,
    outline_glow_vars::FlashbangHue,
    outline_glow_vars::HEGrenadeHue,
    outline_glow_vars::SmokeGrenadeHue,
    outline_glow_vars::DroppedBombHue,
    outline_glow_vars::TickingBombHue,
    outline_glow_vars::DefuseKitHue,
    outline_glow_vars::HostageHue,
    player_info_vars::Enabled,
    player_info_vars::OnlyEnemies,
    player_info_vars::PlayerPositionArrowEnabled,
    player_info_vars::PlayerPositionArrowColorMode,
    player_info_vars::PlayerHealthEnabled,
    player_info_vars::PlayerHealthColorMode,
    player_info_vars::ActiveWeaponIconEnabled,
    player_info_vars::ActiveWeaponAmmoEnabled,
    player_info_vars::BombCarrierIconEnabled,
    player_info_vars::BombPlantIconEnabled,
    player_info_vars::BombDefuseIconEnabled,
    player_info_vars::HostagePickupIconEnabled,
    player_info_vars::HostageRescueIconEnabled,
    player_info_vars::BlindedIconEnabled,
    viewmodel_mod_vars::Enabled,
    viewmodel_mod_vars::ModifyFov,
    viewmodel_mod_vars::Fov,
    no_scope_inaccuracy_vis_vars::Enabled,
    BombPlantAlertEnabled
>;
