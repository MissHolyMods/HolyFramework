#pragma once

namespace HolyFramework
{
    class RenderPipelineService final
    {
    public:
        static RenderPipelineService& GetSingleton() noexcept;

        [[nodiscard]] bool IsStageSupported(HF_RenderStage a_stage) const noexcept;
        HF_RenderSubscriptionHandle SubscribeOwned(
            HF_RenderStage a_stage,
            std::int32_t a_priority,
            HF_RenderStageCallback a_callback,
            void* a_userData,
            std::string_view a_moduleName,
            HF_LogHandle a_logger) noexcept;
        bool UnsubscribeOwned(
            HF_RenderSubscriptionHandle a_handle,
            std::string_view a_moduleName,
            std::string* a_outActualOwner = nullptr) noexcept;
        std::uint32_t UnsubscribeOwnedBy(std::string_view a_moduleName) noexcept;
        [[nodiscard]] bool AuditHook() noexcept;

    private:
        using Render_t = void (*)(
            RE::ImageSpaceEffectHDR*,
            RE::BSTriShape*,
            RE::ImageSpaceEffectParam*);

        struct SubscriptionRuntime final
        {
            std::atomic_bool enabled{ true };
            std::atomic_uint32_t inFlight{ 0 };
            std::atomic_uint64_t calls{ 0 };
            std::atomic_int64_t lastSlowWarningMilliseconds{ 0 };
            std::mutex waitLock;
            std::condition_variable waitCv;
        };

        struct Subscription final
        {
            HF_RenderSubscriptionHandle handle{ HF_INVALID_RENDER_SUBSCRIPTION_HANDLE };
            HF_RenderStage stage{ HF_RENDER_STAGE_POST_HDR_WORLD };
            std::int32_t priority{ HF_RENDER_PRIORITY_NORMAL };
            HF_RenderStageCallback callback{ nullptr };
            void* userData{ nullptr };
            std::string moduleName;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
            std::shared_ptr<SubscriptionRuntime> runtime;
        };

        using SubscriptionList = std::vector<Subscription>;

        RenderPipelineService() = default;

        static void RenderThunk(
            RE::ImageSpaceEffectHDR* a_self,
            RE::BSTriShape* a_geometry,
            RE::ImageSpaceEffectParam* a_param) noexcept;

        [[nodiscard]] bool EnsureHookInstalled() noexcept;
        void Dispatch(HF_RenderStage a_stage) noexcept;
        void PublishSnapshot(std::shared_ptr<const SubscriptionList> a_snapshot) noexcept;
        [[nodiscard]] std::shared_ptr<const SubscriptionList> LoadSnapshot() const noexcept;
        static bool NamesEqual(std::string_view a_left, std::string_view a_right) noexcept;
        static std::int64_t SteadyMilliseconds() noexcept;
        static void WaitForQuiescence(
            const std::shared_ptr<SubscriptionRuntime>& a_runtime,
            HF_RenderSubscriptionHandle a_handle) noexcept;
        static void LeaveCallback(const std::shared_ptr<SubscriptionRuntime>& a_runtime) noexcept;

        mutable std::mutex _subscriptionLock;
        std::atomic<std::shared_ptr<const SubscriptionList>> _subscriptions{ std::make_shared<const SubscriptionList>() };
        std::atomic<HF_RenderSubscriptionHandle> _nextHandle{ 1 };

        std::mutex _installLock;
        std::atomic_bool _installed{ false };
        std::atomic_bool _compromised{ false };
        std::atomic<std::uintptr_t> _slotAddress{ 0 };
        std::atomic<Render_t> _originalRender{ nullptr };
        std::atomic_uint64_t _sequence{ 0 };
    };
}
