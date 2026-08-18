#pragma once

#include "GlobalContext/GlobalContext.h"
#include "Hooks/PeepEventsHook.h"
#include "Utils/ReturnAddress.h"

[[NOINLINE]] void finishInit(auto& hookContext)
{
    hookContext.entityClassifier().init(hookContext);
    if (const auto mainMenu{hookContext.patternSearchResults().template get<MainMenuPanelPointer>()}; mainMenu && *mainMenu)
        hookContext.template make<PanoramaGUI>().init(hookContext.template make<PanoramaUiPanel>((*mainMenu)->uiPanel));
    hookContext.config().init();
    hookContext.config().scheduleLoad();
    hookContext.hooks().peepEventsHook.disable();
    hookContext.hooks().viewRenderHook.install();
}

int SDLHook_PeepEvents(void* events, int numevents, int action, unsigned minType, unsigned maxType) noexcept
{
    const auto initInProgress = !HookContext<GlobalContext>::isGlobalContextComplete();
    if (initInProgress)
        HookContext<GlobalContext>::initCompleteGlobalContextFromGameThread();

    HookContext<GlobalContext> hookContext;

    if (initInProgress)
        finishInit(hookContext);

    return hookContext.hooks().peepEventsHook.original(events, numevents, action, minType, maxType);
}

[[NOINLINE]] void unload(auto& hookContext) noexcept
{
    hookContext.template make<BombTimer>().onUnload();
    hookContext.template make<DefusingAlert>().onUnload();
    hookContext.template make<PostRoundTimer>().onUnload();
    hookContext.template make<OutlineGlow>().onUnload();
    hookContext.template make<BombStatusPanel>().onUnload();
    hookContext.template make<InWorldPanels>().onUnload();
    hookContext.template make<PanoramaGUI>().onUnload();
    hookContext.hooks().viewRenderHook.uninstall();
    hookContext.template make<ClientModeHooks>().restoreGetViewmodelFov();
    hookContext.template make<NoScopeInaccuracyVis>().onUnload();
    hookContext.template make<BombPlantAlert>().onUnload();
}

void ViewRenderHook_onRenderStart(cs2::CViewRender* thisptr) noexcept
{
    HookContext<GlobalContext> hookContext;
    hookContext.clearRenderHookState();
    hookContext.hooks().viewRenderHook.getOriginalOnRenderStart()(thisptr);
    hookContext.make<InWorldPanels>().updateState();
    SoundWatcher<decltype(hookContext)> soundWatcher{hookContext.soundWatcherState(), hookContext};
    soundWatcher.update();
    SoundFeatures{hookContext.soundWatcherState(), hookContext.hooks().viewRenderHook, hookContext}.runOnViewMatrixUpdate();

    hookContext.make<NoScopeInaccuracyVis>().update();
    hookContext.make<RenderingHookEntityLoop>().run();
    hookContext.make<GlowSceneObjects>().removeUnreferencedObjects();
    hookContext.make<DefusingAlert>().run();
    hookContext.make<KillfeedPreserver>().run();
    hookContext.make<BombStatusPanelManager>().run();
    hookContext.make<InWorldPanels>().hideUnusedPanels();

    UnloadFlag unloadFlag;
    hookContext.make<PanoramaGUI>().run(unloadFlag);
    hookContext.config().update();
    hookContext.config().performFileOperation();

    if (unloadFlag) {
        unload(hookContext);
        HookContext<GlobalContext>::destroyGlobalContext();
    }  
}

float ClientModeHook_getViewmodelFov(cs2::ClientModeCSNormal* clientMode) noexcept
{
    HookContext<GlobalContext> hookContext;
    const auto originalFov = hookContext.hooks().originalGetViewmodelFov(clientMode);
    if (auto&& viewmodelMod = hookContext.template make<ViewmodelMod>(); viewmodelMod.shouldModifyViewmodelFov())
        return viewmodelMod.viewmodelFov();
    return originalFov;
}
