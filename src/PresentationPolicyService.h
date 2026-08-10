#pragma once

namespace HolyFramework
{
    class PresentationPolicyService final
    {
    public:
        static PresentationPolicyService& GetSingleton() noexcept;

        [[nodiscard]] bool IsAvailable() noexcept;
        [[nodiscard]] bool GetState(HF_PresentationPolicyStateV1& a_outState) noexcept;

        HF_PresentationPolicyHandle AcquirePolicyOwned(
            const HF_PresentationPolicyRequestV1& a_request,
            std::string_view a_moduleName,
            HF_LogHandle a_logger) noexcept;
        bool UpdatePolicyOwned(
            HF_PresentationPolicyHandle a_handle,
            const HF_PresentationPolicyRequestV1& a_request,
            std::string_view a_moduleName,
            std::string* a_outActualOwner = nullptr) noexcept;
        bool ReleasePolicyOwned(
            HF_PresentationPolicyHandle a_handle,
            std::string_view a_moduleName,
            std::string* a_outActualOwner = nullptr) noexcept;
        std::uint32_t ReleaseOwnedBy(std::string_view a_moduleName) noexcept;

        // Internal final policy passes owned by PresentationService.
        void ApplyPresentPolicy(HF_PresentContextV1& a_context) noexcept;
        void ApplyResizeBuffersPolicy(HF_ResizeBuffersContextV1& a_context) noexcept;
        void ApplySwapChainCreatePolicy(HF_SwapChainCreateContextV1& a_context) noexcept;
        void ObserveSwapChain(HF_NativeHandle a_swapChain) noexcept;

    private:
        struct PolicyRecord final
        {
            HF_PresentationPolicyHandle handle{ HF_INVALID_PRESENTATION_POLICY_HANDLE };
            HF_PresentationPolicyRequestV1 request{};
            std::string moduleName;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
        };

        enum EffectiveFlags : std::uint32_t
        {
            EffectiveNone = 0,
            EffectiveSyncInterval = 1u << 0,
            EffectiveAllowTearing = 1u << 1,
            EffectivePreferFlipModel = 1u << 2,
            EffectiveBufferCount = 1u << 3
        };

        PresentationPolicyService() = default;

        [[nodiscard]] static bool NamesEqualInsensitive(
            std::string_view a_left,
            std::string_view a_right) noexcept;
        [[nodiscard]] static bool ValidateRequest(const HF_PresentationPolicyRequestV1& a_request) noexcept;
        [[nodiscard]] static bool IsFlipModel(std::uint32_t a_swapEffect) noexcept;
        void RecomputeEffectiveLocked() noexcept;
        void UpdateHookDemandLocked() noexcept;
        [[nodiscard]] bool WinsScalar(
            const PolicyRecord& a_candidate,
            const PolicyRecord* a_current) const noexcept;

        mutable std::mutex _lock;
        std::vector<PolicyRecord> _policies;
        std::atomic<HF_PresentationPolicyHandle> _nextHandle{ 1 };
        std::atomic_uint32_t _effectiveFlags{ 0 };
        std::atomic_uint32_t _effectiveSyncInterval{ 0 };
        std::atomic_uint32_t _effectiveBufferCount{ 0 };
        std::atomic_uint32_t _effectiveAllowTearing{ 0 };
        std::atomic_uint32_t _requestCount{ 0 };
        std::atomic_uint64_t _generation{ 0 };
        std::atomic_uint32_t _swapChainFlags{ 0 };
        std::atomic<HF_NativeHandle> _observedSwapChainHandle{ 0 };
    };
}
