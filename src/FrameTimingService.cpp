#include "pch.h"
#include "FrameTimingService.h"

#include "Diagnostics.h"
#include "RuntimeState.h"

namespace HolyFramework
{
    namespace
    {
        inline constexpr REL::ID kFrameTimerID{ 4803789 };
        inline constexpr std::ptrdiff_t kFrameTimerOffset{ 0x21C };
    }

    FrameTimingService& FrameTimingService::GetSingleton() noexcept
    {
        static FrameTimingService* instance = new FrameTimingService();
        return *instance;
    }

    void FrameTimingService::Resolve() noexcept
    {
        std::call_once(_resolveOnce, [this]() noexcept {
            try {
                const REL::Relocation<std::uintptr_t> frameTimer{
                    kFrameTimerID,
                    kFrameTimerOffset
                };
                _frameTimer = reinterpret_cast<const float*>(frameTimer.address());
                if (!_frameTimer) {
                    Diagnostics::ReportFrameworkFailureForModule(
                        "HolyFramework",
                        HF_INVALID_LOG_HANDLE,
                        HF_ERROR_FRAME_TIMING_RELOCATION_FAILED);
                }
            } catch (const std::exception&) {
                _frameTimer = nullptr;
                Diagnostics::ReportFrameworkFailureForModule(
                    "HolyFramework",
                    HF_INVALID_LOG_HANDLE,
                    HF_ERROR_FRAME_TIMING_RELOCATION_FAILED);
            } catch (...) {
                _frameTimer = nullptr;
                Diagnostics::ReportFrameworkFailureForModule(
                    "HolyFramework",
                    HF_INVALID_LOG_HANDLE,
                    HF_ERROR_FRAME_TIMING_RELOCATION_FAILED);
            }
        });
    }

    bool FrameTimingService::ReadSample(float& a_outDeltaSeconds) noexcept
    {
        a_outDeltaSeconds = 0.0F;
        Resolve();
        if (!_frameTimer) {
            return false;
        }

        const auto sample = *_frameTimer;
        if (!std::isfinite(sample) || sample <= 0.0F) {
            return false;
        }

        a_outDeltaSeconds = sample;
        return true;
    }

    bool FrameTimingService::IsAvailable() noexcept
    {
        Resolve();
        return _frameTimer != nullptr;
    }

    bool FrameTimingService::GetState(HF_FrameTimingStateV1& a_outState) noexcept
    {
        a_outState = {};
        a_outState.structSize = sizeof(HF_FrameTimingStateV1);

        Resolve();
        if (_frameTimer) {
            a_outState.flags |= HF_FRAME_TIMING_STATE_AVAILABLE;
        }

        const auto& runtime = RuntimeState::GetSingleton();
        const auto runtimeFlags = runtime.GetFlags();
        if ((runtimeFlags & HF_RUNTIME_STATE_SESSION_ACTIVE) != 0) {
            a_outState.flags |= HF_FRAME_TIMING_STATE_SESSION_ACTIVE;
        }
        if ((runtimeFlags & HF_RUNTIME_STATE_SESSION_READY) != 0) {
            a_outState.flags |= HF_FRAME_TIMING_STATE_SESSION_READY;
        }
        a_outState.sessionGeneration = runtime.GetSessionGeneration();

        float deltaSeconds = 0.0F;
        if (!ReadSample(deltaSeconds)) {
            return _frameTimer != nullptr;
        }

        a_outState.flags |= HF_FRAME_TIMING_STATE_VALID_SAMPLE;
        a_outState.deltaSeconds = deltaSeconds;
        a_outState.deltaMilliseconds = deltaSeconds * 1000.0F;
        a_outState.framesPerSecond = 1.0F / deltaSeconds;
        return true;
    }

    bool FrameTimingService::GetDeltaSeconds(float& a_outDeltaSeconds) noexcept
    {
        return ReadSample(a_outDeltaSeconds);
    }

    bool FrameTimingService::GetFramesPerSecond(float& a_outFramesPerSecond) noexcept
    {
        a_outFramesPerSecond = 0.0F;
        float deltaSeconds = 0.0F;
        if (!ReadSample(deltaSeconds)) {
            return false;
        }
        a_outFramesPerSecond = 1.0F / deltaSeconds;
        return true;
    }
}
