#include "pch.h"
#include "RuntimeState.h"

namespace HolyFramework
{
    RuntimeState& RuntimeState::GetSingleton() noexcept
    {
        static RuntimeState* instance = new RuntimeState();
        return *instance;
    }

    void RuntimeState::SetFlag(const std::uint32_t a_flag, const bool a_enabled) noexcept
    {
        if (a_enabled) {
            _flags.fetch_or(a_flag, std::memory_order_release);
        } else {
            _flags.fetch_and(~a_flag, std::memory_order_release);
        }
    }

    void RuntimeState::SetFrameworkReady(const bool a_ready) noexcept
    {
        SetFlag(HF_RUNTIME_STATE_FRAMEWORK_READY, a_ready);
    }

    void RuntimeState::OnEvent(const HF_Event a_event) noexcept
    {
        switch (a_event) {
        case HF_EVENT_GAME_DATA_READY:
            SetFlag(HF_RUNTIME_STATE_GAME_DATA_READY, true);
            break;
        case HF_EVENT_INPUT_LOADED:
            SetFlag(HF_RUNTIME_STATE_INPUT_READY, true);
            break;
        case HF_EVENT_GAME_LOADED:
            SetFlag(HF_RUNTIME_STATE_GAME_LOADED, true);
            break;
        case HF_EVENT_PRE_LOAD_GAME:
            SetFlag(HF_RUNTIME_STATE_SESSION_ACTIVE, false);
            SetFlag(HF_RUNTIME_STATE_SESSION_READY, false);
            break;
        case HF_EVENT_POST_LOAD_GAME:
        case HF_EVENT_NEW_GAME:
            _sessionGeneration.fetch_add(1, std::memory_order_acq_rel);
            SetFlag(HF_RUNTIME_STATE_SESSION_ACTIVE, true);
            SetFlag(HF_RUNTIME_STATE_SESSION_READY, false);
            break;
        default:
            break;
        }
    }

    void RuntimeState::MarkSessionReady() noexcept
    {
        SetFlag(HF_RUNTIME_STATE_SESSION_READY, true);
    }

    std::uint32_t RuntimeState::GetFlags() const noexcept
    {
        return _flags.load(std::memory_order_acquire);
    }

    std::uint64_t RuntimeState::GetSessionGeneration() const noexcept
    {
        return _sessionGeneration.load(std::memory_order_acquire);
    }

    bool RuntimeState::HasState(const std::uint32_t a_requiredFlags) const noexcept
    {
        const auto flags = GetFlags();
        return (flags & a_requiredFlags) == a_requiredFlags;
    }
}
