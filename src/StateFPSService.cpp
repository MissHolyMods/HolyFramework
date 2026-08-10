#include "pch.h"
#include "StateFPSService.h"

#include "Diagnostics.h"
#include "FramePacingService.h"
#include "PresentationService.h"
#include "UIStateService.h"
#include "WindowService.h"

namespace HolyFramework
{
    namespace
    {
        inline constexpr std::uint32_t kKnownPolicyFlags =
            HF_STATE_FPS_POLICY_GAMEPLAY |
            HF_STATE_FPS_POLICY_MAIN_MENU |
            HF_STATE_FPS_POLICY_LOADING |
            HF_STATE_FPS_POLICY_LOCKPICKING |
            HF_STATE_FPS_POLICY_PIPBOY |
            HF_STATE_FPS_POLICY_BACKGROUND;
    }

    StateFPSService& StateFPSService::GetSingleton() noexcept
    {
        static StateFPSService* instance = new StateFPSService();
        return *instance;
    }

    bool StateFPSService::NamesEqualInsensitive(const std::string_view a_left, const std::string_view a_right) noexcept
    {
        if (a_left.size() != a_right.size()) return false;
        for (std::size_t i = 0; i < a_left.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a_left[i])) !=
                std::tolower(static_cast<unsigned char>(a_right[i]))) return false;
        }
        return true;
    }

    bool StateFPSService::ValidTarget(const std::uint32_t a_fps) noexcept
    {
        return a_fps == 0 || (a_fps >= HF_FRAME_PACING_MIN_FPS && a_fps <= HF_FRAME_PACING_MAX_FPS);
    }

    bool StateFPSService::ValidatePolicy(const HF_StateFPSPolicyV1& a_policy) noexcept
    {
        if (a_policy.structSize < sizeof(HF_StateFPSPolicyV1) || (a_policy.flags & ~kKnownPolicyFlags) != 0) return false;
        if ((a_policy.flags & HF_STATE_FPS_POLICY_GAMEPLAY) && !ValidTarget(a_policy.gameplayFPS)) return false;
        if ((a_policy.flags & HF_STATE_FPS_POLICY_MAIN_MENU) && !ValidTarget(a_policy.mainMenuFPS)) return false;
        if ((a_policy.flags & HF_STATE_FPS_POLICY_LOADING) && !ValidTarget(a_policy.loadingFPS)) return false;
        if ((a_policy.flags & HF_STATE_FPS_POLICY_LOCKPICKING) && !ValidTarget(a_policy.lockpickingFPS)) return false;
        if ((a_policy.flags & HF_STATE_FPS_POLICY_PIPBOY) && !ValidTarget(a_policy.pipboyFPS)) return false;
        if ((a_policy.flags & HF_STATE_FPS_POLICY_BACKGROUND) && !ValidTarget(a_policy.backgroundFPS)) return false;
        return true;
    }

    bool StateFPSService::IsAvailable() noexcept
    {
        return FramePacingService::GetSingleton().IsAvailable();
    }

    std::uint32_t StateFPSService::FlagForState(const HF_StateFPSActiveState a_state) noexcept
    {
        switch (a_state) {
        case HF_STATE_FPS_ACTIVE_GAMEPLAY: return HF_STATE_FPS_POLICY_GAMEPLAY;
        case HF_STATE_FPS_ACTIVE_MAIN_MENU: return HF_STATE_FPS_POLICY_MAIN_MENU;
        case HF_STATE_FPS_ACTIVE_LOADING: return HF_STATE_FPS_POLICY_LOADING;
        case HF_STATE_FPS_ACTIVE_LOCKPICKING: return HF_STATE_FPS_POLICY_LOCKPICKING;
        case HF_STATE_FPS_ACTIVE_PIPBOY: return HF_STATE_FPS_POLICY_PIPBOY;
        case HF_STATE_FPS_ACTIVE_BACKGROUND: return HF_STATE_FPS_POLICY_BACKGROUND;
        default: return 0;
        }
    }

    std::uint32_t StateFPSService::TargetForState(const HF_StateFPSPolicyV1& a_policy, const HF_StateFPSActiveState a_state) noexcept
    {
        switch (a_state) {
        case HF_STATE_FPS_ACTIVE_GAMEPLAY: return a_policy.gameplayFPS;
        case HF_STATE_FPS_ACTIVE_MAIN_MENU: return a_policy.mainMenuFPS;
        case HF_STATE_FPS_ACTIVE_LOADING: return a_policy.loadingFPS;
        case HF_STATE_FPS_ACTIVE_LOCKPICKING: return a_policy.lockpickingFPS;
        case HF_STATE_FPS_ACTIVE_PIPBOY: return a_policy.pipboyFPS;
        case HF_STATE_FPS_ACTIVE_BACKGROUND: return a_policy.backgroundFPS;
        default: return 0;
        }
    }

    HF_StateFPSActiveState StateFPSService::ResolveActiveState(std::uint32_t& a_outStateFlags) noexcept
    {
        a_outStateFlags = 0;
        const auto ui = UIStateService::GetSingleton().CaptureSnapshot();
        if ((ui.flags & HF_UI_STATE_AVAILABLE) != 0) a_outStateFlags |= HF_STATE_FPS_STATE_UI_AVAILABLE;

        const auto& window = WindowService::GetSingleton();
        const bool foreground = !window.IsAvailable() || window.IsForeground();
        if (foreground) a_outStateFlags |= HF_STATE_FPS_STATE_FOREGROUND;
        if (!foreground) return HF_STATE_FPS_ACTIVE_BACKGROUND;
        if ((ui.flags & HF_UI_STATE_LOADING_MENU_OPEN) != 0) return HF_STATE_FPS_ACTIVE_LOADING;
        if ((ui.flags & HF_UI_STATE_LOCKPICKING_MENU_OPEN) != 0) return HF_STATE_FPS_ACTIVE_LOCKPICKING;
        if ((ui.flags & HF_UI_STATE_PIPBOY_MENU_OPEN) != 0) return HF_STATE_FPS_ACTIVE_PIPBOY;
        if ((ui.flags & HF_UI_STATE_MAIN_MENU_OPEN) != 0) return HF_STATE_FPS_ACTIVE_MAIN_MENU;
        return HF_STATE_FPS_ACTIVE_GAMEPLAY;
    }

    bool StateFPSService::GetState(HF_StateFPSStateV1& a_outState) noexcept
    {
        a_outState = {};
        a_outState.structSize = sizeof(a_outState);
        if (IsAvailable()) a_outState.flags |= HF_STATE_FPS_STATE_AVAILABLE;
        a_outState.flags |= _lastStateFlags.load(std::memory_order_acquire);
        a_outState.activeState = _activeState.load(std::memory_order_acquire);
        a_outState.activeTargetFPS = _activeTargetFPS.load(std::memory_order_acquire);
        a_outState.requestCount = _requestCount.load(std::memory_order_acquire);
        a_outState.generation = _generation.load(std::memory_order_acquire);
        if (_activePolicyCount.load(std::memory_order_acquire) != 0) a_outState.flags |= HF_STATE_FPS_STATE_POLICY_ACTIVE;
        if (a_outState.activeTargetFPS != 0) a_outState.flags |= HF_STATE_FPS_STATE_TARGET_ACTIVE;
        return (a_outState.flags & HF_STATE_FPS_STATE_AVAILABLE) != 0;
    }

    void StateFPSService::RecomputeMaintenanceLocked() noexcept
    {
        const auto count = static_cast<std::uint32_t>(_policies.size());
        std::uint32_t activeCount = 0;
        for (const auto& record : _policies) {
            if (record.policy.flags != HF_STATE_FPS_POLICY_NONE) ++activeCount;
        }
        _requestCount.store(count, std::memory_order_release);
        _activePolicyCount.store(activeCount, std::memory_order_release);
        PresentationService::GetSingleton().SetFrameworkPresentMaintenance(
            FrameworkPresentMaintenanceReason::StateFPS, activeCount != 0);
        if (activeCount == 0) {
            _activeState.store(HF_STATE_FPS_ACTIVE_NONE, std::memory_order_release);
            _activeTargetFPS.store(0, std::memory_order_release);
            _lastStateFlags.store(0, std::memory_order_release);
            FramePacingService::GetSingleton().SetFrameworkStateLimit(0);
        }
        _generation.fetch_add(1, std::memory_order_acq_rel);
    }

    HF_StateFPSPolicyHandle StateFPSService::AcquirePolicyOwned(
        const HF_StateFPSPolicyV1& a_policy,
        const std::string_view a_moduleName,
        const HF_LogHandle a_logger) noexcept
    {
        if (a_moduleName.empty() || !ValidatePolicy(a_policy)) {
            if (!a_moduleName.empty()) Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName, a_logger, HF_ERROR_STATE_FPS_INVALID_REQUEST);
            return HF_INVALID_STATE_FPS_POLICY_HANDLE;
        }
        try {
            auto handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
            if (handle == 0) handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
            std::scoped_lock lock{ _lock };
            _policies.push_back(PolicyRecord{ handle, a_policy, std::string{ a_moduleName }, a_logger });
            RecomputeMaintenanceLocked();
            return handle;
        } catch (...) {
            return HF_INVALID_STATE_FPS_POLICY_HANDLE;
        }
    }

    bool StateFPSService::UpdatePolicyOwned(
        const HF_StateFPSPolicyHandle a_handle,
        const HF_StateFPSPolicyV1& a_policy,
        const std::string_view a_moduleName,
        std::string* const a_outActualOwner) noexcept
    {
        if (a_outActualOwner) a_outActualOwner->clear();
        if (a_handle == 0 || !ValidatePolicy(a_policy)) return false;
        std::scoped_lock lock{ _lock };
        const auto it = std::ranges::find_if(_policies, [&](const PolicyRecord& r) { return r.handle == a_handle; });
        if (it == _policies.end()) return false;
        if (!NamesEqualInsensitive(it->moduleName, a_moduleName)) {
            if (a_outActualOwner) *a_outActualOwner = it->moduleName;
            return false;
        }
        it->policy = a_policy;
        RecomputeMaintenanceLocked();
        return true;
    }

    bool StateFPSService::ReleasePolicyOwned(
        const HF_StateFPSPolicyHandle a_handle,
        const std::string_view a_moduleName,
        std::string* const a_outActualOwner) noexcept
    {
        if (a_outActualOwner) a_outActualOwner->clear();
        if (a_handle == 0) return false;
        std::scoped_lock lock{ _lock };
        const auto it = std::ranges::find_if(_policies, [&](const PolicyRecord& r) { return r.handle == a_handle; });
        if (it == _policies.end()) return false;
        if (!NamesEqualInsensitive(it->moduleName, a_moduleName)) {
            if (a_outActualOwner) *a_outActualOwner = it->moduleName;
            return false;
        }
        _policies.erase(it);
        RecomputeMaintenanceLocked();
        return true;
    }

    std::uint32_t StateFPSService::ReleaseOwnedBy(const std::string_view a_moduleName) noexcept
    {
        if (a_moduleName.empty()) return 0;
        std::scoped_lock lock{ _lock };
        const auto before = _policies.size();
        std::erase_if(_policies, [&](const PolicyRecord& r) { return NamesEqualInsensitive(r.moduleName, a_moduleName); });
        const auto removed = static_cast<std::uint32_t>(before - _policies.size());
        if (removed != 0) RecomputeMaintenanceLocked();
        return removed;
    }

    void StateFPSService::MaintainOnPresent() noexcept
    {
        std::uint32_t stateFlags = 0;
        const auto activeState = ResolveActiveState(stateFlags);
        const auto requiredFlag = FlagForState(activeState);
        std::uint32_t target = 0;
        bool found = false;
        HF_StateFPSPriority bestPriority = (std::numeric_limits<HF_StateFPSPriority>::min)();
        HF_StateFPSPolicyHandle bestHandle = (std::numeric_limits<HF_StateFPSPolicyHandle>::max)();
        {
            std::scoped_lock lock{ _lock };
            for (const auto& record : _policies) {
                if ((record.policy.flags & requiredFlag) == 0) continue;
                if (!found || record.policy.priority > bestPriority ||
                    (record.policy.priority == bestPriority && record.handle < bestHandle)) {
                    found = true;
                    bestPriority = record.policy.priority;
                    bestHandle = record.handle;
                    target = TargetForState(record.policy, activeState);
                }
            }
        }

        const auto previousState = _activeState.exchange(activeState, std::memory_order_acq_rel);
        const auto previousTarget = _activeTargetFPS.exchange(found ? target : 0, std::memory_order_acq_rel);
        _lastStateFlags.store(stateFlags, std::memory_order_release);
        if (previousState != activeState || previousTarget != (found ? target : 0)) {
            _generation.fetch_add(1, std::memory_order_acq_rel);
        }
        FramePacingService::GetSingleton().SetFrameworkStateLimit(found ? target : 0);
    }
}
