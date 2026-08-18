#pragma once

#include <CS2/Panorama/CUIPanel.h>
#include <GlobalContext/UnloadFlag.h>
#include <GameClient/Panorama/PanoramaUiEngine.h>
#include <GameClient/Panorama/PanoramaUiPanel.h>

#include "PanoramaCommandDispatcher.h"
#include "CombatTab.h"
#include "HudTab.h"
#include "SoundTab.h"
#include "VisualsTab.h"
#include "Tabs/VisualsTab/ViewmodelModPreviewPanel.h"

template <typename HookContext>
class PanoramaGUI {
public:
    explicit PanoramaGUI(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void init(auto&& mainMenu) noexcept
    {
        if (!mainMenu)
            return;

        // ensure settings tab is loaded because we use CSS classes from settings
        // TODO: replace use of settings CSS classes with raw style properties
        uiEngine().runScript(mainMenu, "if (!$('#JsSettings')) MainMenu.NavigateToTab('JsSettings', 'settings/settings');");

        const auto settings = mainMenu.findChildInLayoutFile("JsSettings");
        if (settings)
            state().settingsPanelHandle = settings.getHandle();

        uiEngine().runScript(settings, reinterpret_cast<const char*>(
#include "CreateGUI.js"
));

        uiEngine().runScript(mainMenu, R"(
(function () {
  $('#JsSettings').FindChildInLayoutFile('OsirisMenuTab').SetParent($('#JsMainMenuContent'));

  var openMenuButton = $.CreatePanel('RadioButton', $.GetContextPanel().FindChildTraverse('MainMenuNavBarSettings').GetParent(), 'OsirisOpenMenuButton', {
    class: "mainmenu-top-navbar__radio-iconbtn",
    group: "NavBar",
    onactivate: "MainMenu.NavigateToTab('OsirisMenuTab', '');"
  });

  $.CreatePanel('Image', openMenuButton, '', {
    class: "mainmenu-top-navbar__radio-btn__icon",
    src: "s2r://panorama/images/icons/ui/vacnet.vsvg"
  });

  $.DispatchEvent('Activated', $.GetContextPanel().FindChildTraverse("MainMenuNavBarHome"), 'mouse');
})();
)");

        if (const auto guiButtonPanel = mainMenu.findChildInLayoutFile("OsirisOpenMenuButton"))
            state().guiButtonHandle = guiButtonPanel.getHandle();

        if (const auto guiPanel = mainMenu.findChildInLayoutFile("OsirisMenuTab")) {
            state().guiPanelHandle = guiPanel.getHandle();
            state().viewmodelPreviewPanelHandle = guiPanel.findChildInLayoutFile("ViewmodelPreview").getHandle();

            hookContext.template make<CombatTab>().init(guiPanel);
            hookContext.template make<HudTab>().init(guiPanel);
            hookContext.template make<VisualsTab>().init(guiPanel);
            hookContext.template make<SoundTab>().init(guiPanel);
        }

        updateFromConfig();
    }

    template <typename ConfigVariable>
    void onHueSliderValueChanged(const char* panelId, float value) const
    {
        const auto newVariableValue = handleHueSlider(panelId, value, ConfigVariable::ValueType::kMin, ConfigVariable::ValueType::kMax, GET_CONFIG_VAR(ConfigVariable));
        hookContext.config().template setVariable<ConfigVariable>(typename ConfigVariable::ValueType{newVariableValue});
    }

    template <typename ConfigVariable>
    void onHueSliderTextEntrySubmit(const char* panelId, const char* value) const noexcept
    {
        const auto newVariableValue = handleHueTextEntry(panelId, value, ConfigVariable::ValueType::kMin, ConfigVariable::ValueType::kMax, GET_CONFIG_VAR(ConfigVariable));
        hookContext.config().template setVariable<ConfigVariable>(typename ConfigVariable::ValueType{newVariableValue});
    }

    void run(UnloadFlag& unloadFlag) const noexcept
    {
        auto&& guiPanel = uiEngine().getPanelFromHandle(state().guiPanelHandle);
        if (!guiPanel)
            return;

        const auto cmdSymbol = uiEngine().makeSymbol(0, "cmd");
        const auto cmd = guiPanel.getAttributeString(cmdSymbol, "");
        PanoramaCommandDispatcher{cmd, unloadFlag, hookContext}();
        guiPanel.setAttributeString(cmdSymbol, "");

        auto&& viewmodelModPreviewPanel = uiEngine().getPanelFromHandle(state().viewmodelPreviewPanelHandle).clientPanel().template as<ViewmodelModPreviewPanel>();
        viewmodelModPreviewPanel.setupPreviewModel();
        viewmodelModPreviewPanel.setFov();
    }

    void updateFromConfig() noexcept
    {
        const auto mainMenuPointer = hookContext.patternSearchResults().template get<MainMenuPanelPointer>();
        auto&& mainMenu = hookContext.template make<ClientPanel>(mainMenuPointer ? *mainMenuPointer : nullptr).uiPanel();
        hookContext.template make<CombatTab>().updateFromConfig(mainMenu);
        hookContext.template make<HudTab>().updateFromConfig(mainMenu);
        hookContext.template make<VisualsTab>().updateFromConfig(mainMenu);
        hookContext.template make<SoundTab>().updateFromConfig(mainMenu);
    }

    void onUnload() const noexcept
    {
        uiEngine().deletePanelByHandle(state().guiButtonHandle);
        uiEngine().deletePanelByHandle(state().guiPanelHandle);

        if (auto&& settingsPanel = uiEngine().getPanelFromHandle(state().settingsPanelHandle))
            uiEngine().runScript(settingsPanel, "delete $.Osiris");
    }

private:
    [[nodiscard]] decltype(auto) getHueSlider(const char* sliderId) const noexcept
    {
        auto&& guiPanel = uiEngine().getPanelFromHandle(state().guiPanelHandle);
        return hookContext.template make<HueSlider>(guiPanel.findChildInLayoutFile(sliderId));
    }

    [[nodiscard]] color::HueInteger handleHueTextEntry(const char* sliderId, const char* value, color::HueInteger min, color::HueInteger max, color::HueInteger current) const noexcept
    {
        auto&& hueSlider = getHueSlider(sliderId);
        color::HueInteger::UnderlyingType hueIntegral;
        if (!StringParser{value}.parseInt(hueIntegral) || hueIntegral < min || hueIntegral > max) {
            hueSlider.updateTextEntry(current);
            return current;
        }

        if (hueIntegral == current)
            return current;

        const color::HueInteger hue{hueIntegral};
        hueSlider.updateSlider(hue);
        hueSlider.updateColorPreview(hue);
        return hue;
    }

    [[nodiscard]] color::HueInteger handleHueSlider(const char* sliderId, float value, color::HueInteger min, color::HueInteger max, color::HueInteger current) const noexcept
    {
        const auto hueIntegral = static_cast<color::HueInteger::UnderlyingType>(value);
        if (hueIntegral < min || hueIntegral > max || hueIntegral == current)
            return current;

        const auto hue = color::HueInteger{hueIntegral};
        auto&& hueSlider = getHueSlider(sliderId);
        hueSlider.updateTextEntry(hue);
        hueSlider.updateColorPreview(hue);
        return hue;
    }

    [[nodiscard]] decltype(auto) uiEngine() const noexcept
    {
        return hookContext.template make<PanoramaUiEngine>();
    }

    [[nodiscard]] auto& state() const noexcept
    {
        return hookContext.panoramaGuiState();
    }

    HookContext& hookContext;
};
