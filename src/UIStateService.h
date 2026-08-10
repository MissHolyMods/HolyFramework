#pragma once

namespace HolyFramework
{
    struct UIStateSnapshot
    {
        std::uint32_t flags{ HF_UI_STATE_NONE };
        bool paused{ false };
    };

    class UIStateService final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
    public:
        static UIStateService& GetSingleton() noexcept;

        // Safe to call repeatedly. Registration is deferred until RE::UI exists.
        void TryInstall() noexcept;

        [[nodiscard]] bool IsMenuOpen(const char* a_menuName) const noexcept;
        [[nodiscard]] bool IsPaused() const noexcept;
        [[nodiscard]] std::uint32_t GetStateFlags() const noexcept;
        [[nodiscard]] UIStateSnapshot CaptureSnapshot() const noexcept;
        [[nodiscard]] bool GetLoadingMenuState(HF_LoadingMenuStateV1& a_outState) const noexcept;

        HF_UIMenuSubscriptionHandle SubscribeMenuEventsOwned(
            std::string_view a_menuNameFilter,
            HF_UIMenuEventCallback a_callback,
            void* a_userData,
            std::string_view a_moduleName,
            HF_LogHandle a_logger) noexcept;
        bool UnsubscribeMenuEventsOwned(
            HF_UIMenuSubscriptionHandle a_handle,
            std::string_view a_moduleName,
            std::string* a_outActualOwner = nullptr) noexcept;
        std::uint32_t UnsubscribeMenuEventsOwnedBy(std::string_view a_moduleName) noexcept;

        HF_UIPolicyHandle AcquireLoadingMenuPolicyOwned(
            std::uint32_t a_policyFlags,
            std::string_view a_moduleName,
            HF_LogHandle a_logger) noexcept;
        bool ReleaseLoadingMenuPolicyOwned(
            HF_UIPolicyHandle a_handle,
            std::string_view a_moduleName,
            std::string* a_outActualOwner = nullptr) noexcept;
        std::uint32_t ReleaseLoadingMenuPoliciesOwnedBy(std::string_view a_moduleName) noexcept;

        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent& a_event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_source) override;

    private:
        struct MenuSubscription
        {
            HF_UIMenuSubscriptionHandle handle{ HF_INVALID_UI_MENU_SUBSCRIPTION_HANDLE };
            std::string menuNameFilter;
            HF_UIMenuEventCallback callback{ nullptr };
            void* userData{ nullptr };
            std::string moduleName;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
        };

        struct PendingMenuEvent
        {
            std::string menuName;
            bool opening{ false };
            std::uint64_t sequence{ 0 };
            std::uint64_t sessionGeneration{ 0 };
        };

        struct LoadingPolicyRecord
        {
            HF_UIPolicyHandle handle{ HF_INVALID_UI_POLICY_HANDLE };
            std::uint32_t flags{ HF_LOADING_MENU_POLICY_NONE };
            std::string moduleName;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
        };

        struct LoadingMenuOriginalState
        {
            const void* menuIdentity{ nullptr };
            bool valid{ false };
            bool leftButtonDown{ false };
            bool rightButtonDown{ false };
            bool allowRotation{ false };
            bool autoRotate{ false };
            bool leftStickReady{ false };
            bool rightStickReady{ false };
        };

        UIStateService() = default;

        friend class PresentationService;

        static void HF_CALL DispatchStateChanged(void*) noexcept;
        static void HF_CALL DispatchMenuEventTask(void* a_userData) noexcept;

        void DispatchMenuEvent(const PendingMenuEvent& a_event) noexcept;
        [[nodiscard]] std::uint32_t GetActiveLoadingMenuPolicyFlags() const noexcept;
        [[nodiscard]] bool ApplyLoadingMenuPoliciesNow(bool a_captureOriginal) noexcept;
        void RestoreLoadingMenuOriginalState() noexcept;
        void ClearLoadingMenuOriginalState() noexcept;
        void MaintainLoadingMenuPolicyOnPresent() noexcept;
        [[nodiscard]] bool HasMenuSubscribersFor(std::string_view a_menuName) const noexcept;
        [[nodiscard]] static bool NamesEqualInsensitive(std::string_view a_left, std::string_view a_right) noexcept;

        std::atomic_bool _installed{ false };
        std::atomic_bool _dispatchQueued{ false };
        mutable std::mutex _menuSubscriptionLock;
        std::vector<MenuSubscription> _menuSubscriptions;
        std::atomic<HF_UIMenuSubscriptionHandle> _nextMenuSubscriptionHandle{ 1 };
        std::atomic_uint64_t _menuSequence{ 0 };

        mutable std::mutex _loadingPolicyLock;
        std::vector<LoadingPolicyRecord> _loadingPolicies;
        LoadingMenuOriginalState _loadingMenuOriginal;
        std::atomic<HF_UIPolicyHandle> _nextLoadingPolicyHandle{ 1 };
        std::atomic_bool _loadingMenuActive{ false };
    };
}
