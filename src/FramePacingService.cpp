#include "pch.h"

#if defined(_MSC_VER)
#  include <intrin.h>
#endif

#include "FramePacingService.h"

#include "Diagnostics.h"
#include "PresentationService.h"

namespace HolyFramework
{
    namespace
    {
        inline constexpr auto kSpinMargin = std::chrono::microseconds{ 750 };
    }

    FramePacingService& FramePacingService::GetSingleton() noexcept
    {
        static FramePacingService* instance = new FramePacingService();
        return *instance;
    }

    bool FramePacingService::NamesEqualInsensitive(
        const std::string_view a_left,
        const std::string_view a_right) noexcept
    {
        if (a_left.size() != a_right.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a_left.size(); ++i) {
            const auto left = static_cast<unsigned char>(a_left[i]);
            const auto right = static_cast<unsigned char>(a_right[i]);
            if (std::tolower(left) != std::tolower(right)) {
                return false;
            }
        }
        return true;
    }

    bool FramePacingService::ValidTarget(const std::uint32_t a_targetFPS) noexcept
    {
        return a_targetFPS == 0 ||
            (a_targetFPS >= HF_FRAME_PACING_MIN_FPS && a_targetFPS <= HF_FRAME_PACING_MAX_FPS);
    }

    bool FramePacingService::IsAvailable() noexcept
    {
        return PresentationService::GetSingleton().IsAvailable();
    }

    bool FramePacingService::GetState(HF_FramePacingStateV1& a_outState) noexcept
    {
        a_outState = {};
        a_outState.structSize = sizeof(HF_FramePacingStateV1);
        if (IsAvailable()) {
            a_outState.flags |= HF_FRAME_PACING_STATE_AVAILABLE;
        }
        a_outState.activeLimitFPS = _activeLimitFPS.load(std::memory_order_acquire);
        a_outState.requestCount = _requestCount.load(std::memory_order_acquire);
        a_outState.pacedPresentCount = _pacedPresentCount.load(std::memory_order_acquire);
        a_outState.lastWaitMicroseconds = _lastWaitMicroseconds.load(std::memory_order_acquire);
        if (a_outState.activeLimitFPS != 0) {
            a_outState.flags |= HF_FRAME_PACING_STATE_LIMIT_ACTIVE;
        }
        return (a_outState.flags & HF_FRAME_PACING_STATE_AVAILABLE) != 0;
    }

    void FramePacingService::RecomputeActiveLimitLocked() noexcept
    {
        std::uint32_t target = 0;
        const auto frameworkStateTarget = _frameworkStateLimitFPS.load(std::memory_order_acquire);
        if (frameworkStateTarget != 0) {
            target = frameworkStateTarget;
        }
        for (const auto& record : _limits) {
            if (record.targetFPS == 0) {
                continue;
            }
            if (target == 0 || record.targetFPS < target) {
                target = record.targetFPS;
            }
        }

        _requestCount.store(static_cast<std::uint32_t>(_limits.size()), std::memory_order_release);
        const auto previous = _activeLimitFPS.exchange(target, std::memory_order_acq_rel);
        if (previous != target) {
            _policyGeneration.fetch_add(1, std::memory_order_acq_rel);
        }
        if ((previous == 0) != (target == 0)) {
            PresentationService::GetSingleton().SetFrameworkPresentMaintenance(
                FrameworkPresentMaintenanceReason::FramePacing,
                target != 0);
        }
    }

    HF_FrameLimitHandle FramePacingService::AcquireLimitOwned(
        const std::uint32_t a_targetFPS,
        const std::string_view a_moduleName,
        const HF_LogHandle a_logger) noexcept
    {
        if (a_moduleName.empty() || !ValidTarget(a_targetFPS)) {
            if (!a_moduleName.empty()) {
                Diagnostics::ReportFrameworkFailureForModule(
                    a_moduleName,
                    a_logger,
                    HF_ERROR_FRAME_PACING_INVALID_REQUEST);
            }
            return HF_INVALID_FRAME_LIMIT_HANDLE;
        }

        try {
            auto handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
            if (handle == HF_INVALID_FRAME_LIMIT_HANDLE) {
                handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
            }
            {
                std::scoped_lock lock{ _limitLock };
                _limits.push_back(LimitRecord{
                    .handle = handle,
                    .targetFPS = a_targetFPS,
                    .moduleName = std::string{ a_moduleName },
                    .logger = a_logger
                });
                RecomputeActiveLimitLocked();
            }
            return handle;
        } catch (const std::exception&) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName,
                a_logger,
                HF_ERROR_FRAME_PACING_INVALID_REQUEST);
        } catch (...) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName,
                a_logger,
                HF_ERROR_FRAME_PACING_INVALID_REQUEST);
        }
        return HF_INVALID_FRAME_LIMIT_HANDLE;
    }

    bool FramePacingService::UpdateLimitOwned(
        const HF_FrameLimitHandle a_handle,
        const std::uint32_t a_targetFPS,
        const std::string_view a_moduleName,
        std::string* const a_outActualOwner) noexcept
    {
        if (a_handle == HF_INVALID_FRAME_LIMIT_HANDLE || a_moduleName.empty() || !ValidTarget(a_targetFPS)) {
            return false;
        }

        std::scoped_lock lock{ _limitLock };
        const auto it = std::ranges::find_if(_limits, [a_handle](const LimitRecord& a_record) {
            return a_record.handle == a_handle;
        });
        if (it == _limits.end()) {
            return false;
        }
        if (!NamesEqualInsensitive(it->moduleName, a_moduleName)) {
            if (a_outActualOwner) {
                *a_outActualOwner = it->moduleName;
            }
            return false;
        }
        it->targetFPS = a_targetFPS;
        RecomputeActiveLimitLocked();
        return true;
    }

    bool FramePacingService::ReleaseLimitOwned(
        const HF_FrameLimitHandle a_handle,
        const std::string_view a_moduleName,
        std::string* const a_outActualOwner) noexcept
    {
        if (a_handle == HF_INVALID_FRAME_LIMIT_HANDLE || a_moduleName.empty()) {
            return false;
        }

        std::scoped_lock lock{ _limitLock };
        const auto it = std::ranges::find_if(_limits, [a_handle](const LimitRecord& a_record) {
            return a_record.handle == a_handle;
        });
        if (it == _limits.end()) {
            return false;
        }
        if (!NamesEqualInsensitive(it->moduleName, a_moduleName)) {
            if (a_outActualOwner) {
                *a_outActualOwner = it->moduleName;
            }
            return false;
        }
        _limits.erase(it);
        RecomputeActiveLimitLocked();
        return true;
    }

    std::uint32_t FramePacingService::ReleaseOwnedBy(const std::string_view a_moduleName) noexcept
    {
        if (a_moduleName.empty()) {
            return 0;
        }

        std::scoped_lock lock{ _limitLock };
        const auto oldSize = _limits.size();
        std::erase_if(_limits, [&](const LimitRecord& a_record) {
            return NamesEqualInsensitive(a_record.moduleName, a_moduleName);
        });
        const auto removed = oldSize - _limits.size();
        if (removed != 0) {
            RecomputeActiveLimitLocked();
        }
        return static_cast<std::uint32_t>(removed);
    }

    void FramePacingService::ResetTimeline() noexcept
    {
        _timelineFPS = 0;
        _frameDuration = {};
        _nextFrame = {};
        _lastWaitMicroseconds.store(0, std::memory_order_release);
    }


    void FramePacingService::SetFrameworkStateLimit(const std::uint32_t a_targetFPS) noexcept
    {
        if (!ValidTarget(a_targetFPS)) {
            return;
        }
        const auto previous = _frameworkStateLimitFPS.exchange(a_targetFPS, std::memory_order_acq_rel);
        if (previous == a_targetFPS) {
            return;
        }
        std::scoped_lock lock{ _limitLock };
        RecomputeActiveLimitLocked();
    }

    void FramePacingService::MaintainOnPresent() noexcept
    {
        using clock = std::chrono::steady_clock;

        const auto generation = _policyGeneration.load(std::memory_order_acquire);
        const auto targetFPS = _activeLimitFPS.load(std::memory_order_acquire);
        if (_observedPolicyGeneration != generation) {
            ResetTimeline();
            _observedPolicyGeneration = generation;
        }
        if (targetFPS == 0) {
            return;
        }

        if (_timelineFPS != targetFPS) {
            _timelineFPS = targetFPS;
            _frameDuration = std::chrono::duration_cast<clock::duration>(
                std::chrono::duration<double>(1.0 / static_cast<double>(targetFPS)));
            _nextFrame = {};
        }

        const auto waitStart = clock::now();
        auto now = waitStart;
        if (_nextFrame.time_since_epoch().count() == 0 ||
            now > _nextFrame + (_frameDuration * 4)) {
            _nextFrame = now + _frameDuration;
        }

        const auto remaining = _nextFrame - now;
        if (remaining > kSpinMargin) {
            std::this_thread::sleep_for(remaining - kSpinMargin);
        }
        while (clock::now() < _nextFrame) {
#if defined(_MSC_VER)
            _mm_pause();
#else
            std::this_thread::yield();
#endif
        }

        const auto waitEnd = clock::now();
        _lastWaitMicroseconds.store(
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(waitEnd - waitStart).count()),
            std::memory_order_release);
        _pacedPresentCount.fetch_add(1, std::memory_order_relaxed);

        // Keep a fixed timeline; small scheduler overshoots are recovered by
        // later frames rather than permanently lowering the average frame rate.
        _nextFrame += _frameDuration;
    }
}
