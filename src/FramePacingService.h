#pragma once

namespace HolyFramework
{
    class FramePacingService final
    {
    public:
        static FramePacingService& GetSingleton() noexcept;

        [[nodiscard]] bool IsAvailable() noexcept;
        [[nodiscard]] bool GetState(HF_FramePacingStateV1& a_outState) noexcept;

        HF_FrameLimitHandle AcquireLimitOwned(
            std::uint32_t a_targetFPS,
            std::string_view a_moduleName,
            HF_LogHandle a_logger) noexcept;
        bool UpdateLimitOwned(
            HF_FrameLimitHandle a_handle,
            std::uint32_t a_targetFPS,
            std::string_view a_moduleName,
            std::string* a_outActualOwner = nullptr) noexcept;
        bool ReleaseLimitOwned(
            HF_FrameLimitHandle a_handle,
            std::string_view a_moduleName,
            std::string* a_outActualOwner = nullptr) noexcept;
        std::uint32_t ReleaseOwnedBy(std::string_view a_moduleName) noexcept;

        // Internal state-selected limit. Not counted as a module request.
        void SetFrameworkStateLimit(std::uint32_t a_targetFPS) noexcept;

        // Called only by PresentationService for non-TEST Presents.
        void MaintainOnPresent() noexcept;

    private:
        struct LimitRecord
        {
            HF_FrameLimitHandle handle{ HF_INVALID_FRAME_LIMIT_HANDLE };
            std::uint32_t targetFPS{ 0 };
            std::string moduleName;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
        };

        FramePacingService() = default;

        [[nodiscard]] static bool NamesEqualInsensitive(
            std::string_view a_left,
            std::string_view a_right) noexcept;
        [[nodiscard]] static bool ValidTarget(std::uint32_t a_targetFPS) noexcept;
        void RecomputeActiveLimitLocked() noexcept;
        void ResetTimeline() noexcept;

        mutable std::mutex _limitLock;
        std::vector<LimitRecord> _limits;
        std::atomic<HF_FrameLimitHandle> _nextHandle{ 1 };
        std::atomic_uint32_t _activeLimitFPS{ 0 };
        std::atomic_uint32_t _frameworkStateLimitFPS{ 0 };
        std::atomic_uint32_t _requestCount{ 0 };
        std::atomic_uint64_t _policyGeneration{ 0 };
        std::atomic_uint64_t _pacedPresentCount{ 0 };
        std::atomic_uint64_t _lastWaitMicroseconds{ 0 };

        // Present-thread-owned timing state. Policy changes are observed through
        // _activeLimitFPS and reset naturally when the target changes.
        std::uint64_t _observedPolicyGeneration{ 0 };
        std::uint32_t _timelineFPS{ 0 };
        std::chrono::steady_clock::duration _frameDuration{};
        std::chrono::steady_clock::time_point _nextFrame{};
    };
}
