#pragma once

namespace HolyFramework
{
    class CPUSchedulingService final
    {
    public:
        static CPUSchedulingService& GetSingleton() noexcept;

        [[nodiscard]] bool IsAvailable() noexcept;
        [[nodiscard]] bool GetState(HF_CPUSchedulingStateV1& a_outState) noexcept;

        HF_CPUSchedulingHandle AcquireLimitOwned(
            std::uint32_t a_maxLogicalProcessors,
            std::uint32_t a_timeoutMs,
            std::string_view a_moduleName,
            HF_LogHandle a_logger) noexcept;
        bool UpdateLimitOwned(
            HF_CPUSchedulingHandle a_handle,
            std::uint32_t a_maxLogicalProcessors,
            std::uint32_t a_timeoutMs,
            std::string_view a_moduleName,
            std::string* a_outActualOwner = nullptr) noexcept;
        bool ReleaseLimitOwned(
            HF_CPUSchedulingHandle a_handle,
            std::string_view a_moduleName,
            std::string* a_outActualOwner = nullptr) noexcept;
        std::uint32_t ReleaseOwnedBy(std::string_view a_moduleName) noexcept;

        void MaintainOnPresent() noexcept;

    private:
        enum class ApplyMode : std::uint8_t { None, CpuSets, LegacyAffinity };
        struct LimitRecord
        {
            HF_CPUSchedulingHandle handle{ HF_INVALID_CPU_SCHEDULING_HANDLE };
            std::uint32_t maxLogicalProcessors{ 0 };
            std::chrono::steady_clock::time_point expiresAt{};
            bool timed{ false };
            std::string moduleName;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
        };

        using GetProcessDefaultCpuSets_t = REX::W32::BOOL(__stdcall*)(REX::W32::HANDLE, std::uint32_t*, std::uint32_t, std::uint32_t*);
        using SetProcessDefaultCpuSets_t = REX::W32::BOOL(__stdcall*)(REX::W32::HANDLE, const std::uint32_t*, std::uint32_t);
        using GetSystemCpuSetInformation_t = REX::W32::BOOL(__stdcall*)(void*, std::uint32_t, std::uint32_t*, REX::W32::HANDLE, std::uint32_t);
        using GetProcessAffinityMask_t = REX::W32::BOOL(__stdcall*)(REX::W32::HANDLE, std::uintptr_t*, std::uintptr_t*);
        using SetProcessAffinityMask_t = REX::W32::BOOL(__stdcall*)(REX::W32::HANDLE, std::uintptr_t);

        CPUSchedulingService() = default;
        void Prepare() noexcept;
        [[nodiscard]] static bool NamesEqualInsensitive(std::string_view a_left, std::string_view a_right) noexcept;
        [[nodiscard]] static bool ValidRequest(std::uint32_t a_maxLogicalProcessors, std::uint32_t a_timeoutMs) noexcept;
        void RecomputeLocked() noexcept;
        bool ApplyTargetLocked(std::uint32_t a_target) noexcept;
        bool RestoreLocked() noexcept;
        [[nodiscard]] std::uintptr_t BuildLegacyMask(std::uint32_t a_count) const noexcept;

        std::once_flag _prepareOnce;
        REX::W32::HANDLE _processHandle{ nullptr };
        SetProcessDefaultCpuSets_t _setProcessDefaultCpuSets{ nullptr };
        SetProcessAffinityMask_t _setProcessAffinityMask{ nullptr };
        std::vector<std::uint32_t> _originalDefaultCpuSets;
        std::vector<std::uint32_t> _eligibleCpuSetIds;
        bool _originalDefaultCpuSetsCaptured{ false };
        std::uintptr_t _originalAffinityMask{ 0 };
        bool _legacyAffinityAvailable{ false };

        mutable std::mutex _lock;
        std::vector<LimitRecord> _limits;
        std::atomic<HF_CPUSchedulingHandle> _nextHandle{ 1 };
        std::atomic_uint32_t _activeMax{ 0 };
        std::atomic_uint32_t _requestCount{ 0 };
        std::atomic_uint32_t _appliedCount{ 0 };
        std::atomic_uint32_t _availableCount{ 0 };
        std::atomic_uint32_t _stateFlags{ 0 };
        std::atomic_uint64_t _generation{ 0 };
        ApplyMode _appliedMode{ ApplyMode::None };
    };
}
