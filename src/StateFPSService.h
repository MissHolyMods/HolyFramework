#pragma once

namespace HolyFramework
{
    class StateFPSService final
    {
    public:
        static StateFPSService& GetSingleton() noexcept;

        [[nodiscard]] bool IsAvailable() noexcept;
        [[nodiscard]] bool GetState(HF_StateFPSStateV1& a_outState) noexcept;

        HF_StateFPSPolicyHandle AcquirePolicyOwned(
            const HF_StateFPSPolicyV1& a_policy,
            std::string_view a_moduleName,
            HF_LogHandle a_logger) noexcept;
        bool UpdatePolicyOwned(
            HF_StateFPSPolicyHandle a_handle,
            const HF_StateFPSPolicyV1& a_policy,
            std::string_view a_moduleName,
            std::string* a_outActualOwner = nullptr) noexcept;
        bool ReleasePolicyOwned(
            HF_StateFPSPolicyHandle a_handle,
            std::string_view a_moduleName,
            std::string* a_outActualOwner = nullptr) noexcept;
        std::uint32_t ReleaseOwnedBy(std::string_view a_moduleName) noexcept;

        void MaintainOnPresent() noexcept;

    private:
        struct PolicyRecord
        {
            HF_StateFPSPolicyHandle handle{ HF_INVALID_STATE_FPS_POLICY_HANDLE };
            HF_StateFPSPolicyV1 policy{};
            std::string moduleName;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
        };

        StateFPSService() = default;

        [[nodiscard]] static bool NamesEqualInsensitive(std::string_view a_left, std::string_view a_right) noexcept;
        [[nodiscard]] static bool ValidatePolicy(const HF_StateFPSPolicyV1& a_policy) noexcept;
        [[nodiscard]] static bool ValidTarget(std::uint32_t a_fps) noexcept;
        [[nodiscard]] static std::uint32_t TargetForState(const HF_StateFPSPolicyV1& a_policy, HF_StateFPSActiveState a_state) noexcept;
        [[nodiscard]] static std::uint32_t FlagForState(HF_StateFPSActiveState a_state) noexcept;
        [[nodiscard]] HF_StateFPSActiveState ResolveActiveState(std::uint32_t& a_outStateFlags) noexcept;
        void RecomputeMaintenanceLocked() noexcept;

        mutable std::mutex _lock;
        std::vector<PolicyRecord> _policies;
        std::atomic<HF_StateFPSPolicyHandle> _nextHandle{ 1 };
        std::atomic_uint32_t _requestCount{ 0 };
        std::atomic_uint32_t _activePolicyCount{ 0 };
        std::atomic_uint32_t _activeState{ HF_STATE_FPS_ACTIVE_NONE };
        std::atomic_uint32_t _activeTargetFPS{ 0 };
        std::atomic_uint32_t _lastStateFlags{ 0 };
        std::atomic_uint64_t _generation{ 0 };
    };
}
