#pragma once

namespace HolyFramework
{
    class PlayerMovementService final
    {
    public:
        static PlayerMovementService& GetSingleton() noexcept;

        [[nodiscard]] bool IsAvailable() const noexcept;
        bool GetLatest(HF_PlayerMovementSampleV1& a_outSample) const noexcept;

        HF_PlayerMovementSubscriptionHandle Subscribe(
            HF_PlayerMovementCallback a_callback,
            void* a_userData,
            std::string_view a_moduleName,
            HF_LogHandle a_logger) noexcept;
        bool Unsubscribe(
            HF_PlayerMovementSubscriptionHandle a_handle,
            std::string_view a_moduleName,
            std::string* a_outActualOwner = nullptr) noexcept;
        std::uint32_t UnsubscribeOwnedBy(std::string_view a_moduleName) noexcept;

    private:
        using Movement_t = void (*)(
            RE::IMovementPlayerControls*,
            std::uint32_t,
            RE::PlayerControlsMovementData&);

        struct Runtime
        {
            std::atomic_bool enabled{ true };
            std::atomic_uint32_t inFlight{ 0 };
            std::mutex waitLock;
            std::condition_variable waitCv;
        };

        struct Subscription
        {
            HF_PlayerMovementSubscriptionHandle handle{
                HF_INVALID_PLAYER_MOVEMENT_SUBSCRIPTION_HANDLE
            };
            HF_PlayerMovementCallback callback{ nullptr };
            void* userData{ nullptr };
            std::string owner;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
            std::shared_ptr<Runtime> runtime;
        };

        using SubscriptionList = std::vector<Subscription>;

        PlayerMovementService() = default;

        [[nodiscard]] static bool NamesEqual(
            std::string_view a_left,
            std::string_view a_right) noexcept;
        bool EnsureInstalled() noexcept;
        void TryRestoreIfUnused() noexcept;
        static void Thunk(
            RE::IMovementPlayerControls* a_controls,
            std::uint32_t a_numericID,
            RE::PlayerControlsMovementData& a_output) noexcept;
        void Dispatch(RE::IMovementPlayerControls* a_controls) noexcept;
        static void LeaveCallback(const std::shared_ptr<Runtime>& a_runtime) noexcept;
        static void WaitForQuiescence(
            const std::shared_ptr<Runtime>& a_runtime,
            HF_PlayerMovementSubscriptionHandle a_handle) noexcept;
        void PublishSnapshot(std::shared_ptr<const SubscriptionList> a_snapshot) noexcept;
        [[nodiscard]] std::shared_ptr<const SubscriptionList> LoadSnapshot() const noexcept;

        mutable std::mutex _subscriptionLock;
        std::atomic<std::shared_ptr<const SubscriptionList>> _subscriptions{ std::make_shared<const SubscriptionList>() };
        std::atomic<std::uint64_t> _nextHandle{ 1 };

        mutable std::mutex _sampleLock;
        HF_PlayerMovementSampleV1 _latest{};
        bool _hasLatest{ false };
        std::atomic<std::uint64_t> _sequence{ 0 };

        std::mutex _installLock;
        std::atomic_bool _installed{ false };
        std::atomic_bool _compromised{ false };
        std::atomic<Movement_t> _original{ nullptr };
        std::atomic<std::uintptr_t> _slotAddress{ 0 };
    };
}
