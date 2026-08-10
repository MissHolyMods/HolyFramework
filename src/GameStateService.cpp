#include "pch.h"
#include "GameStateService.h"

#include "RuntimeState.h"
#include "UIStateService.h"

namespace HolyFramework
{
    GameStateService& GameStateService::GetSingleton() noexcept
    {
        static GameStateService* instance = new GameStateService();
        return *instance;
    }

    std::uint32_t GameStateService::GetStateFlags() const noexcept
    {
        const auto runtime = RuntimeState::GetSingleton().GetFlags();
        const auto ui = UIStateService::GetSingleton().CaptureSnapshot();

        std::uint32_t flags = HF_GAME_STATE_NONE;
        if ((ui.flags & HF_UI_STATE_AVAILABLE) != 0) {
            flags |= HF_GAME_STATE_UI_AVAILABLE;
        }
        if ((runtime & HF_RUNTIME_STATE_SESSION_ACTIVE) != 0) {
            flags |= HF_GAME_STATE_SESSION_ACTIVE;
        }
        if ((runtime & HF_RUNTIME_STATE_SESSION_READY) != 0) {
            flags |= HF_GAME_STATE_SESSION_READY;
        }
        if ((ui.flags & HF_UI_STATE_LOADING_MENU_OPEN) != 0) {
            flags |= HF_GAME_STATE_LOADING;
        }
        if ((ui.flags & HF_UI_STATE_MAIN_MENU_OPEN) != 0) {
            flags |= HF_GAME_STATE_MAIN_MENU;
        }
        if (ui.paused) {
            flags |= HF_GAME_STATE_PAUSED;
        }

        const bool sessionActive = (flags & HF_GAME_STATE_SESSION_ACTIVE) != 0;
        const bool transitional = (flags & (HF_GAME_STATE_LOADING | HF_GAME_STATE_MAIN_MENU)) != 0;
        if (sessionActive && !transitional) {
            flags |= HF_GAME_STATE_IN_GAME;
        }
        return flags;
    }

    bool GameStateService::HasState(const std::uint32_t a_requiredFlags) const noexcept
    {
        const auto flags = GetStateFlags();
        return (flags & a_requiredFlags) == a_requiredFlags;
    }

    bool GameStateService::IsPaused() const noexcept
    {
        return UIStateService::GetSingleton().CaptureSnapshot().paused;
    }

    bool GameStateService::IsLoading() const noexcept
    {
        return (UIStateService::GetSingleton().CaptureSnapshot().flags & HF_UI_STATE_LOADING_MENU_OPEN) != 0;
    }

    bool GameStateService::IsInGame() const noexcept
    {
        return HasState(HF_GAME_STATE_IN_GAME);
    }
}
