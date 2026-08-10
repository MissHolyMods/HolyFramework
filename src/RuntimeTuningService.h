#pragma once

namespace HolyFramework
{
    class RuntimeTuningService final
    {
    public:
        static RuntimeTuningService& GetSingleton() noexcept;

        [[nodiscard]] bool IsAvailable() noexcept;
        [[nodiscard]] bool GetState(HF_RuntimeTuningStateV1& a_outState) noexcept;

        HF_RuntimeTuningHandle AcquirePolicyOwned(
            const HF_RuntimeTuningPolicyV1& a_policy,
            std::string_view a_moduleName,
            HF_LogHandle a_logger) noexcept;
        bool UpdatePolicyOwned(
            HF_RuntimeTuningHandle a_handle,
            const HF_RuntimeTuningPolicyV1& a_policy,
            std::string_view a_moduleName,
            std::string* a_outActualOwner = nullptr) noexcept;
        bool ReleasePolicyOwned(
            HF_RuntimeTuningHandle a_handle,
            std::string_view a_moduleName,
            std::string* a_outActualOwner = nullptr) noexcept;
        std::uint32_t ReleaseOwnedBy(std::string_view a_moduleName) noexcept;

        void MaintainOnPresent() noexcept;

    private:
        struct PolicyRecord
        {
            HF_RuntimeTuningHandle handle{ HF_INVALID_RUNTIME_TUNING_HANDLE };
            HF_RuntimeTuningPolicyV1 policy{};
            std::string moduleName;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
        };

        RuntimeTuningService() = default;
        [[nodiscard]] static bool NamesEqualInsensitive(std::string_view a_left, std::string_view a_right) noexcept;
        [[nodiscard]] static bool ValidatePolicy(const HF_RuntimeTuningPolicyV1& a_policy) noexcept;
        void RecomputeLocked() noexcept;
        void ReleaseClampOwnerLocked() noexcept;
        void ReleasePapyrusOwnerLocked() noexcept;
        bool ActivateClampOwnerLocked(const PolicyRecord* a_owner) noexcept;
        bool ActivatePapyrusOwnerLocked(const PolicyRecord* a_owner) noexcept;

        mutable std::mutex _lock;
        std::vector<PolicyRecord> _policies;
        std::atomic<HF_RuntimeTuningHandle> _nextHandle{ 1 };
        std::atomic_uint32_t _requestCount{ 0 };
        std::atomic_uint64_t _generation{ 0 };
        std::atomic_uint32_t _stateFlags{ HF_RUNTIME_TUNING_STATE_AVAILABLE };

        std::string _clampOwner;
        HF_LogHandle _clampLogger{ HF_INVALID_LOG_HANDLE };
        std::int32_t _originalFPSClamp{ 0 };
        bool _originalFPSClampCaptured{ false };

        std::string _papyrusOwner;
        HF_LogHandle _papyrusLogger{ HF_INVALID_LOG_HANDLE };
        HF_RuntimeTuningPriority _papyrusPriority{ 0 };
        float _papyrusOriginalBudgetMS{ 0.0F };
        float _papyrusBaseBudgetMS{ 0.0F };
        float _currentPapyrusBudgetMS{ 0.0F };
        float _lastIntervalSeconds{ 1.0F / 60.0F };
        std::uint32_t _papyrusMaxFPS{ 0 };
    };
}
