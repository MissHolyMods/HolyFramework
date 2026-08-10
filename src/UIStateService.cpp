#include "pch.h"
#include "UIStateService.h"

#include "Diagnostics.h"
#include "EventBus.h"
#include "ModuleContext.h"
#include "PerformanceMonitor.h"
#include "PresentationService.h"
#include "RuntimeState.h"
#include "TaskScheduler.h"

namespace HolyFramework
{
    namespace
    {
        inline constexpr std::uint32_t kMenuEventCoalesceMs = 16;
        inline constexpr std::uint32_t kKnownLoadingPolicyFlags = HF_LOADING_MENU_POLICY_DISABLE_MODEL_INTERACTION;

        [[nodiscard]] bool MenuOpen(RE::UI* const a_ui, const char* const a_name) noexcept
        {
            if (!a_ui || !a_name || !*a_name) {
                return false;
            }

            try {
                return a_ui->GetMenuOpen(RE::BSFixedString{ a_name });
            } catch (...) {
                return false;
            }
        }
    }

    UIStateService& UIStateService::GetSingleton() noexcept
    {
        static UIStateService* instance = new UIStateService();
        return *instance;
    }

    bool UIStateService::NamesEqualInsensitive(
        const std::string_view a_left,
        const std::string_view a_right) noexcept
    {
        if (a_left.size() != a_right.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a_left.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a_left[i])) !=
                std::tolower(static_cast<unsigned char>(a_right[i]))) {
                return false;
            }
        }
        return true;
    }

    void UIStateService::TryInstall() noexcept
    {
        if (_installed.load(std::memory_order_acquire)) {
            return;
        }

        auto* const ui = RE::UI::GetSingleton();
        if (!ui) {
            return;
        }

        bool expected = false;
        if (!_installed.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return;
        }

        try {
            ui->RegisterSink<RE::MenuOpenCloseEvent>(this);
            REX::INFO("HolyFramework UI state observer installed");
        } catch (...) {
            _installed.store(false, std::memory_order_release);
            REX::WARN("HolyFramework UI state observer could not be installed yet");
        }
    }

    bool UIStateService::IsMenuOpen(const char* const a_menuName) const noexcept
    {
        return MenuOpen(RE::UI::GetSingleton(), a_menuName);
    }

    UIStateSnapshot UIStateService::CaptureSnapshot() const noexcept
    {
        UIStateSnapshot snapshot{};
        auto* const ui = RE::UI::GetSingleton();
        if (!ui) {
            return snapshot;
        }

        snapshot.flags = HF_UI_STATE_AVAILABLE;

        try {
            RE::BSAutoReadLock lock{ RE::UI::GetMenuMapRWLock() };
            for (const auto& menu : ui->menuStack) {
                if (!menu || !menu->OnStack()) {
                    continue;
                }
                snapshot.flags |= HF_UI_STATE_ANY_MENU_OPEN;
                if (menu->menuFlags.all(RE::UI_MENU_FLAGS::kPausesGame)) {
                    snapshot.paused = true;
                }
            }
        } catch (...) {
            // Keep the availability bit and continue with named menu queries.
        }

        if (MenuOpen(ui, "LoadingMenu")) {
            snapshot.flags |= HF_UI_STATE_LOADING_MENU_OPEN;
        }
        if (MenuOpen(ui, "MainMenu")) {
            snapshot.flags |= HF_UI_STATE_MAIN_MENU_OPEN;
        }
        if (MenuOpen(ui, "PauseMenu")) {
            snapshot.flags |= HF_UI_STATE_PAUSE_MENU_OPEN;
        }
        if (MenuOpen(ui, "PipboyMenu")) {
            snapshot.flags |= HF_UI_STATE_PIPBOY_MENU_OPEN;
        }
        if (MenuOpen(ui, "Console")) {
            snapshot.flags |= HF_UI_STATE_CONSOLE_OPEN;
        }
        if (MenuOpen(ui, "DialogueMenu")) {
            snapshot.flags |= HF_UI_STATE_DIALOGUE_MENU_OPEN;
        }
        if (MenuOpen(ui, "ContainerMenu")) {
            snapshot.flags |= HF_UI_STATE_CONTAINER_MENU_OPEN;
        }
        if (MenuOpen(ui, "BarterMenu")) {
            snapshot.flags |= HF_UI_STATE_BARTER_MENU_OPEN;
        }
        if (MenuOpen(ui, "WorkshopMenu")) {
            snapshot.flags |= HF_UI_STATE_WORKSHOP_MENU_OPEN;
        }
        if (MenuOpen(ui, "TerminalMenu") || MenuOpen(ui, "TerminalMenuButtons")) {
            snapshot.flags |= HF_UI_STATE_TERMINAL_MENU_OPEN;
        }
        if (MenuOpen(ui, "LockpickingMenu")) {
            snapshot.flags |= HF_UI_STATE_LOCKPICKING_MENU_OPEN;
        }
        return snapshot;
    }

    bool UIStateService::IsPaused() const noexcept
    {
        return CaptureSnapshot().paused;
    }

    std::uint32_t UIStateService::GetStateFlags() const noexcept
    {
        return CaptureSnapshot().flags;
    }

    std::uint32_t UIStateService::GetActiveLoadingMenuPolicyFlags() const noexcept
    {
        std::scoped_lock lock{ _loadingPolicyLock };
        std::uint32_t flags = HF_LOADING_MENU_POLICY_NONE;
        for (const auto& policy : _loadingPolicies) {
            flags |= policy.flags;
        }
        return flags;
    }

    bool UIStateService::GetLoadingMenuState(HF_LoadingMenuStateV1& a_outState) const noexcept
    {
        a_outState = {};
        a_outState.structSize = sizeof(a_outState);
        a_outState.activePolicyFlags = GetActiveLoadingMenuPolicyFlags();
        if (a_outState.activePolicyFlags != HF_LOADING_MENU_POLICY_NONE) {
            a_outState.flags |= HF_LOADING_MENU_STATE_POLICY_ACTIVE;
        }

        auto* const ui = RE::UI::GetSingleton();
        if (!ui) {
            return false;
        }
        a_outState.flags |= HF_LOADING_MENU_STATE_AVAILABLE;

        try {
            const auto menu = ui->GetMenu<RE::LoadingMenu>();
            if (!menu) {
                return true;
            }
            if (menu->OnStack()) {
                a_outState.flags |= HF_LOADING_MENU_STATE_OPEN;
            }
            if (menu->autoRotate) {
                a_outState.flags |= HF_LOADING_MENU_STATE_AUTO_ROTATE;
            }
            if (menu->allowRotation) {
                a_outState.flags |= HF_LOADING_MENU_STATE_ALLOW_ROTATION;
            }
            if (menu->leftButtonDown) {
                a_outState.flags |= HF_LOADING_MENU_STATE_LEFT_BUTTON_DOWN;
            }
            if (menu->rightButtonDown) {
                a_outState.flags |= HF_LOADING_MENU_STATE_RIGHT_BUTTON_DOWN;
            }
            if (menu->leftStickReady) {
                a_outState.flags |= HF_LOADING_MENU_STATE_LEFT_STICK_READY;
            }
            if (menu->rightStickReady) {
                a_outState.flags |= HF_LOADING_MENU_STATE_RIGHT_STICK_READY;
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    bool UIStateService::ApplyLoadingMenuPoliciesNow(const bool a_captureOriginal) noexcept
    {
        const auto activeFlags = GetActiveLoadingMenuPolicyFlags();
        if (activeFlags == HF_LOADING_MENU_POLICY_NONE) {
            return false;
        }

        auto* const ui = RE::UI::GetSingleton();
        if (!ui) {
            return false;
        }

        try {
            const auto menu = ui->GetMenu<RE::LoadingMenu>();
            if (!menu) {
                return false;
            }

            if (a_captureOriginal) {
                std::scoped_lock lock{ _loadingPolicyLock };
                if (!_loadingMenuOriginal.valid || _loadingMenuOriginal.menuIdentity != menu.get()) {
                    _loadingMenuOriginal = LoadingMenuOriginalState{
                        .menuIdentity = menu.get(),
                        .valid = true,
                        .leftButtonDown = menu->leftButtonDown,
                        .rightButtonDown = menu->rightButtonDown,
                        .allowRotation = menu->allowRotation,
                        .autoRotate = menu->autoRotate,
                        .leftStickReady = menu->leftStickReady,
                        .rightStickReady = menu->rightStickReady
                    };
                }
            }

            if ((activeFlags & HF_LOADING_MENU_POLICY_DISABLE_MODEL_INTERACTION) != 0) {
                menu->autoRotate = false;
                menu->allowRotation = false;
                menu->leftButtonDown = false;
                menu->rightButtonDown = false;
                menu->leftStickReady = false;
                menu->rightStickReady = false;
            }
            return true;
        } catch (const std::exception&) {
            Diagnostics::ReportFrameworkWarningForModule(
                "HolyFramework",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_UI_LOADING_POLICY_APPLY_FAILED);
        } catch (...) {
            Diagnostics::ReportFrameworkWarningForModule(
                "HolyFramework",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_UI_LOADING_POLICY_APPLY_FAILED);
        }
        return false;
    }

    void UIStateService::RestoreLoadingMenuOriginalState() noexcept
    {
        LoadingMenuOriginalState original{};
        {
            std::scoped_lock lock{ _loadingPolicyLock };
            if (!_loadingMenuOriginal.valid) {
                return;
            }
            original = _loadingMenuOriginal;
            _loadingMenuOriginal = {};
        }

        auto* const ui = RE::UI::GetSingleton();
        if (!ui) {
            return;
        }
        try {
            const auto menu = ui->GetMenu<RE::LoadingMenu>();
            if (!menu || menu.get() != original.menuIdentity) {
                return;
            }
            menu->leftButtonDown = original.leftButtonDown;
            menu->rightButtonDown = original.rightButtonDown;
            menu->allowRotation = original.allowRotation;
            menu->autoRotate = original.autoRotate;
            menu->leftStickReady = original.leftStickReady;
            menu->rightStickReady = original.rightStickReady;
        } catch (...) {
            Diagnostics::ReportFrameworkWarningForModule(
                "HolyFramework",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_UI_LOADING_POLICY_APPLY_FAILED);
        }
    }

    void UIStateService::ClearLoadingMenuOriginalState() noexcept
    {
        std::scoped_lock lock{ _loadingPolicyLock };
        _loadingMenuOriginal = {};
    }

    void UIStateService::MaintainLoadingMenuPolicyOnPresent() noexcept
    {
        if (!_loadingMenuActive.load(std::memory_order_acquire) ||
            GetActiveLoadingMenuPolicyFlags() == HF_LOADING_MENU_POLICY_NONE) {
            return;
        }

        // LoadingMenu rewrites its interaction fields during rendered frames.
        // Present continues to run throughout loading, unlike game-queue tasks,
        // so keep the active policy authoritative on the presentation path.
        (void)ApplyLoadingMenuPoliciesNow(true);
    }

    HF_UIPolicyHandle UIStateService::AcquireLoadingMenuPolicyOwned(
        const std::uint32_t a_policyFlags,
        const std::string_view a_moduleName,
        const HF_LogHandle a_logger) noexcept
    {
        if (a_moduleName.empty() || a_policyFlags == HF_LOADING_MENU_POLICY_NONE ||
            (a_policyFlags & ~kKnownLoadingPolicyFlags) != 0) {
            if (!a_moduleName.empty()) {
                Diagnostics::ReportFrameworkFailureForModule(
                    a_moduleName,
                    a_logger,
                    HF_ERROR_UI_LOADING_POLICY_INVALID);
            }
            return HF_INVALID_UI_POLICY_HANDLE;
        }

        try {
            auto handle = _nextLoadingPolicyHandle.fetch_add(1, std::memory_order_relaxed);
            if (handle == HF_INVALID_UI_POLICY_HANDLE) {
                handle = _nextLoadingPolicyHandle.fetch_add(1, std::memory_order_relaxed);
            }
            {
                std::scoped_lock lock{ _loadingPolicyLock };
                const bool wasEmpty = _loadingPolicies.empty();
                _loadingPolicies.push_back(LoadingPolicyRecord{
                    .handle = handle,
                    .flags = a_policyFlags,
                    .moduleName = std::string{ a_moduleName },
                    .logger = a_logger
                });
                if (wasEmpty) {
                    PresentationService::GetSingleton().SetFrameworkPresentMaintenance(
                        FrameworkPresentMaintenanceReason::LoadingMenu, true);
                }
            }

            if (_loadingMenuActive.load(std::memory_order_acquire) || IsMenuOpen("LoadingMenu")) {
                _loadingMenuActive.store(true, std::memory_order_release);
                (void)ApplyLoadingMenuPoliciesNow(true);
            }
            return handle;
        } catch (const std::exception&) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName,
                a_logger,
                HF_ERROR_UI_LOADING_POLICY_INVALID);
        } catch (...) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName,
                a_logger,
                HF_ERROR_UI_LOADING_POLICY_INVALID);
        }
        return HF_INVALID_UI_POLICY_HANDLE;
    }

    bool UIStateService::ReleaseLoadingMenuPolicyOwned(
        const HF_UIPolicyHandle a_handle,
        const std::string_view a_moduleName,
        std::string* const a_outActualOwner) noexcept
    {
        if (a_handle == HF_INVALID_UI_POLICY_HANDLE || a_moduleName.empty()) {
            return false;
        }

        bool restore = false;
        {
            std::scoped_lock lock{ _loadingPolicyLock };
            const auto it = std::ranges::find_if(_loadingPolicies, [a_handle](const LoadingPolicyRecord& a_policy) {
                return a_policy.handle == a_handle;
            });
            if (it == _loadingPolicies.end()) {
                return false;
            }
            if (!NamesEqualInsensitive(it->moduleName, a_moduleName)) {
                if (a_outActualOwner) {
                    *a_outActualOwner = it->moduleName;
                }
                return false;
            }
            _loadingPolicies.erase(it);
            restore = _loadingPolicies.empty();
            if (restore) {
                PresentationService::GetSingleton().SetFrameworkPresentMaintenance(
                    FrameworkPresentMaintenanceReason::LoadingMenu, false);
            }
        }

        if (restore) {
            RestoreLoadingMenuOriginalState();
        } else if (_loadingMenuActive.load(std::memory_order_acquire) || IsMenuOpen("LoadingMenu")) {
            (void)ApplyLoadingMenuPoliciesNow(false);
        }
        return true;
    }

    std::uint32_t UIStateService::ReleaseLoadingMenuPoliciesOwnedBy(const std::string_view a_moduleName) noexcept
    {
        if (a_moduleName.empty()) {
            return 0;
        }

        std::uint32_t removed = 0;
        bool restore = false;
        {
            std::scoped_lock lock{ _loadingPolicyLock };
            const auto oldSize = _loadingPolicies.size();
            std::erase_if(_loadingPolicies, [a_moduleName](const LoadingPolicyRecord& a_policy) {
                return NamesEqualInsensitive(a_policy.moduleName, a_moduleName);
            });
            removed = static_cast<std::uint32_t>(oldSize - _loadingPolicies.size());
            restore = removed != 0 && _loadingPolicies.empty();
            if (restore) {
                PresentationService::GetSingleton().SetFrameworkPresentMaintenance(
                    FrameworkPresentMaintenanceReason::LoadingMenu, false);
            }
        }

        if (restore) {
            RestoreLoadingMenuOriginalState();
        } else if (removed != 0 &&
                   (_loadingMenuActive.load(std::memory_order_acquire) || IsMenuOpen("LoadingMenu"))) {
            (void)ApplyLoadingMenuPoliciesNow(false);
        }
        return removed;
    }

    HF_UIMenuSubscriptionHandle UIStateService::SubscribeMenuEventsOwned(
        const std::string_view a_menuNameFilter,
        const HF_UIMenuEventCallback a_callback,
        void* const a_userData,
        const std::string_view a_moduleName,
        const HF_LogHandle a_logger) noexcept
    {
        if (!a_callback || a_moduleName.empty() ||
            a_menuNameFilter.size() >= HF_UI_MENU_NAME_CAPACITY) {
            if (!a_moduleName.empty()) {
                Diagnostics::ReportFrameworkFailureForModule(
                    a_moduleName,
                    a_logger,
                    HF_ERROR_UI_MENU_SUBSCRIBE_FAILED);
            }
            return HF_INVALID_UI_MENU_SUBSCRIPTION_HANDLE;
        }

        try {
            auto handle = _nextMenuSubscriptionHandle.fetch_add(1, std::memory_order_relaxed);
            if (handle == HF_INVALID_UI_MENU_SUBSCRIPTION_HANDLE) {
                handle = _nextMenuSubscriptionHandle.fetch_add(1, std::memory_order_relaxed);
            }

            std::scoped_lock lock{ _menuSubscriptionLock };
            _menuSubscriptions.push_back(MenuSubscription{
                .handle = handle,
                .menuNameFilter = std::string{ a_menuNameFilter },
                .callback = a_callback,
                .userData = a_userData,
                .moduleName = std::string{ a_moduleName },
                .logger = a_logger
            });
            return handle;
        } catch (const std::exception&) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName,
                a_logger,
                HF_ERROR_UI_MENU_SUBSCRIBE_FAILED);
        } catch (...) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName,
                a_logger,
                HF_ERROR_UI_MENU_SUBSCRIBE_FAILED);
        }
        return HF_INVALID_UI_MENU_SUBSCRIPTION_HANDLE;
    }

    bool UIStateService::UnsubscribeMenuEventsOwned(
        const HF_UIMenuSubscriptionHandle a_handle,
        const std::string_view a_moduleName,
        std::string* const a_outActualOwner) noexcept
    {
        if (a_handle == HF_INVALID_UI_MENU_SUBSCRIPTION_HANDLE || a_moduleName.empty()) {
            return false;
        }

        std::scoped_lock lock{ _menuSubscriptionLock };
        const auto it = std::ranges::find_if(_menuSubscriptions, [a_handle](const MenuSubscription& a_subscription) {
            return a_subscription.handle == a_handle;
        });
        if (it == _menuSubscriptions.end()) {
            return false;
        }
        if (!NamesEqualInsensitive(it->moduleName, a_moduleName)) {
            if (a_outActualOwner) {
                *a_outActualOwner = it->moduleName;
            }
            return false;
        }
        _menuSubscriptions.erase(it);
        return true;
    }

    std::uint32_t UIStateService::UnsubscribeMenuEventsOwnedBy(const std::string_view a_moduleName) noexcept
    {
        if (a_moduleName.empty()) {
            return 0;
        }

        std::scoped_lock lock{ _menuSubscriptionLock };
        const auto oldSize = _menuSubscriptions.size();
        std::erase_if(_menuSubscriptions, [a_moduleName](const MenuSubscription& a_subscription) {
            return NamesEqualInsensitive(a_subscription.moduleName, a_moduleName);
        });
        return static_cast<std::uint32_t>(oldSize - _menuSubscriptions.size());
    }

    bool UIStateService::HasMenuSubscribersFor(const std::string_view a_menuName) const noexcept
    {
        std::scoped_lock lock{ _menuSubscriptionLock };
        return std::ranges::any_of(_menuSubscriptions, [a_menuName](const MenuSubscription& a_subscription) {
            return a_subscription.menuNameFilter.empty() ||
                   NamesEqualInsensitive(a_subscription.menuNameFilter, a_menuName);
        });
    }

    void HF_CALL UIStateService::DispatchMenuEventTask(void* const a_userData) noexcept
    {
        std::unique_ptr<PendingMenuEvent> event{ static_cast<PendingMenuEvent*>(a_userData) };
        if (event) {
            GetSingleton().DispatchMenuEvent(*event);
        }
    }

    void UIStateService::DispatchMenuEvent(const PendingMenuEvent& a_event) noexcept
    {
        std::vector<MenuSubscription> listeners;
        {
            std::scoped_lock lock{ _menuSubscriptionLock };
            listeners.reserve(_menuSubscriptions.size());
            for (const auto& subscription : _menuSubscriptions) {
                if (subscription.menuNameFilter.empty() ||
                    NamesEqualInsensitive(subscription.menuNameFilter, a_event.menuName)) {
                    listeners.push_back(subscription);
                }
            }
        }

        if (listeners.empty()) {
            return;
        }

        HF_UIMenuEventV1 publicEvent{};
        publicEvent.structSize = sizeof(publicEvent);
        publicEvent.stateFlags = GetStateFlags();
        publicEvent.sequence = a_event.sequence;
        publicEvent.sessionGeneration = a_event.sessionGeneration;
        publicEvent.opening = a_event.opening ? HF_TRUE : HF_FALSE;
        std::snprintf(publicEvent.menuName, sizeof(publicEvent.menuName), "%s", a_event.menuName.c_str());

        for (const auto& listener : listeners) {
            ModuleContext::Scope scope{ listener.moduleName.c_str(), listener.logger };
            PerformanceMonitor::Scope perfScope{ listener.moduleName, "ui.menu-event", 16'000 };
            try {
                listener.callback(std::addressof(publicEvent), listener.userData);
            } catch (const std::exception&) {
                Diagnostics::ReportFrameworkFailureForModule(
                    listener.moduleName,
                    listener.logger,
                    HF_ERROR_UI_MENU_CALLBACK_EXCEPTION);
                std::scoped_lock lock{ _menuSubscriptionLock };
                std::erase_if(_menuSubscriptions, [handle = listener.handle](const MenuSubscription& a_subscription) {
                    return a_subscription.handle == handle;
                });
            } catch (...) {
                Diagnostics::ReportFrameworkFailureForModule(
                    listener.moduleName,
                    listener.logger,
                    HF_ERROR_UI_MENU_CALLBACK_EXCEPTION);
                std::scoped_lock lock{ _menuSubscriptionLock };
                std::erase_if(_menuSubscriptions, [handle = listener.handle](const MenuSubscription& a_subscription) {
                    return a_subscription.handle == handle;
                });
            }
        }
    }

    void HF_CALL UIStateService::DispatchStateChanged(void*) noexcept
    {
        auto& self = GetSingleton();
        self._dispatchQueued.store(false, std::memory_order_release);
        EventBus::GetSingleton().Dispatch(HF_EVENT_UI_STATE_CHANGED);
    }

    RE::BSEventNotifyControl UIStateService::ProcessEvent(
        const RE::MenuOpenCloseEvent& a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
    {
        try {
            const char* const rawName = a_event.menuName.c_str();
            const std::string_view menuName = rawName ? std::string_view{ rawName } : std::string_view{};

            if (NamesEqualInsensitive(menuName, "LoadingMenu")) {
                _loadingMenuActive.store(a_event.opening, std::memory_order_release);
                if (a_event.opening) {
                    if (GetActiveLoadingMenuPolicyFlags() != HF_LOADING_MENU_POLICY_NONE) {
                        (void)ApplyLoadingMenuPoliciesNow(true);
                    }
                } else {
                    ClearLoadingMenuOriginalState();
                }
            }

            if (!menuName.empty() && HasMenuSubscribersFor(menuName)) {
                auto payload = std::make_unique<PendingMenuEvent>();
                payload->menuName = std::string{ menuName };
                payload->opening = a_event.opening;
                payload->sequence = _menuSequence.fetch_add(1, std::memory_order_relaxed) + 1;
                payload->sessionGeneration = RuntimeState::GetSingleton().GetSessionGeneration();

                const auto handle = TaskScheduler::GetSingleton().QueueFrameworkDelayed(
                    HF_TASK_QUEUE_GAME,
                    0,
                    DispatchMenuEventTask,
                    payload.get(),
                    HF_TASK_FLAG_NONE);
                if (handle != HF_INVALID_TASK_HANDLE) {
                    (void)payload.release();
                } else {
                    Diagnostics::ReportFrameworkWarningForModule(
                        "HolyFramework",
                        HF_INVALID_LOG_HANDLE,
                        HF_ERROR_UI_MENU_DISPATCH_FAILED);
                }
            }
        } catch (...) {
            Diagnostics::ReportFrameworkWarningForModule(
                "HolyFramework",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_UI_MENU_DISPATCH_FAILED);
        }

        if (!EventBus::GetSingleton().HasSubscribers(HF_EVENT_UI_STATE_CHANGED)) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // Never invoke module callbacks from Fallout's menu event source. A
        // one-frame debounce coalesces the small bursts Fallout emits while a
        // compound menu changes its internal stack, then hands one synthetic
        // event to the normal supervised game-task path.
        bool expected = false;
        if (_dispatchQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            const auto handle = TaskScheduler::GetSingleton().QueueFrameworkDelayed(
                HF_TASK_QUEUE_GAME,
                kMenuEventCoalesceMs,
                DispatchStateChanged,
                nullptr,
                HF_TASK_FLAG_NONE);
            if (handle == HF_INVALID_TASK_HANDLE) {
                _dispatchQueued.store(false, std::memory_order_release);
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }
}
