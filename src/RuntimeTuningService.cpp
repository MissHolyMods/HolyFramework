#include "pch.h"
#include "RuntimeTuningService.h"

#include "Diagnostics.h"
#include "FrameTimingService.h"
#include "GameSettingsManager.h"
#include "ModuleContext.h"
#include "PresentationService.h"

namespace HolyFramework
{
    namespace
    {
        inline constexpr std::uint32_t kKnownFlags =
            HF_RUNTIME_TUNING_POLICY_DISABLE_FPS_CLAMP |
            HF_RUNTIME_TUNING_POLICY_DYNAMIC_PAPYRUS_BUDGET;
        inline constexpr std::string_view kFPSClampName{ "iFPSClamp:General" };
        inline constexpr std::string_view kPapyrusBudgetName{ "fUpdateBudgetMS:Papyrus" };
    }

    RuntimeTuningService& RuntimeTuningService::GetSingleton() noexcept
    {
        static RuntimeTuningService* instance = new RuntimeTuningService();
        return *instance;
    }

    bool RuntimeTuningService::NamesEqualInsensitive(const std::string_view a_left, const std::string_view a_right) noexcept
    {
        if (a_left.size() != a_right.size()) return false;
        for (std::size_t i = 0; i < a_left.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a_left[i])) != std::tolower(static_cast<unsigned char>(a_right[i]))) return false;
        }
        return true;
    }

    bool RuntimeTuningService::ValidatePolicy(const HF_RuntimeTuningPolicyV1& a_policy) noexcept
    {
        if (a_policy.structSize < sizeof(HF_RuntimeTuningPolicyV1) || (a_policy.flags & ~kKnownFlags) != 0) return false;
        if ((a_policy.flags & HF_RUNTIME_TUNING_POLICY_DYNAMIC_PAPYRUS_BUDGET) != 0) {
            if (!std::isfinite(a_policy.papyrusBaseBudgetMS) || a_policy.papyrusBaseBudgetMS < 0.0F || a_policy.papyrusBaseBudgetMS > 100.0F) return false;
            if (a_policy.papyrusMaxFPS < HF_FRAME_PACING_MIN_FPS || a_policy.papyrusMaxFPS > HF_FRAME_PACING_MAX_FPS) return false;
        }
        return true;
    }

    bool RuntimeTuningService::IsAvailable() noexcept
    {
        return true;
    }

    void RuntimeTuningService::ReleaseClampOwnerLocked() noexcept
    {
        if (_clampOwner.empty()) return;
        ModuleContext::Scope scope{ _clampOwner.c_str(), _clampLogger };
        if (!GameSettingsManager::GetSingleton().Release(HF_GAME_SETTING_SOURCE_FALLOUT_INI, kFPSClampName, true)) {
            (void)GameSettingsManager::GetSingleton().Release(HF_GAME_SETTING_SOURCE_FALLOUT_INI, kFPSClampName, false);
        }
        _clampOwner.clear();
        _clampLogger = HF_INVALID_LOG_HANDLE;
        _originalFPSClampCaptured = false;
        _stateFlags.fetch_and(~HF_RUNTIME_TUNING_STATE_FPS_CLAMP_MANAGED, std::memory_order_acq_rel);
    }

    void RuntimeTuningService::ReleasePapyrusOwnerLocked() noexcept
    {
        if (_papyrusOwner.empty()) return;
        ModuleContext::Scope scope{ _papyrusOwner.c_str(), _papyrusLogger };
        if (!GameSettingsManager::GetSingleton().Release(HF_GAME_SETTING_SOURCE_FALLOUT_INI, kPapyrusBudgetName, true)) {
            (void)GameSettingsManager::GetSingleton().Release(HF_GAME_SETTING_SOURCE_FALLOUT_INI, kPapyrusBudgetName, false);
        }
        _papyrusOwner.clear();
        _papyrusLogger = HF_INVALID_LOG_HANDLE;
        _papyrusPriority = 0;
        _papyrusOriginalBudgetMS = 0.0F;
        _papyrusBaseBudgetMS = 0.0F;
        _currentPapyrusBudgetMS = 0.0F;
        _papyrusMaxFPS = 0;
        _lastIntervalSeconds = 1.0F / 60.0F;
        _stateFlags.fetch_and(~(HF_RUNTIME_TUNING_STATE_PAPYRUS_MANAGED | HF_RUNTIME_TUNING_STATE_FRAME_SAMPLE_VALID), std::memory_order_acq_rel);
    }

    bool RuntimeTuningService::ActivateClampOwnerLocked(const PolicyRecord* const a_owner) noexcept
    {
        if (!a_owner) return false;
        if (!_clampOwner.empty() && NamesEqualInsensitive(_clampOwner, a_owner->moduleName)) return true;
        ReleaseClampOwnerLocked();
        std::int32_t current = 0;
        if (!GameSettingsManager::GetSingleton().GetInt32(HF_GAME_SETTING_SOURCE_FALLOUT_INI, kFPSClampName, current)) {
            Diagnostics::ReportFrameworkFailureForModule(a_owner->moduleName, a_owner->logger, HF_ERROR_RUNTIME_TUNING_SETTING_UNAVAILABLE);
            return false;
        }
        ModuleContext::Scope scope{ a_owner->moduleName.c_str(), a_owner->logger };
        if (!GameSettingsManager::GetSingleton().SetInt32(HF_GAME_SETTING_SOURCE_FALLOUT_INI, kFPSClampName, 0)) return false;
        _clampOwner = a_owner->moduleName;
        _clampLogger = a_owner->logger;
        _originalFPSClamp = current;
        _originalFPSClampCaptured = true;
        _stateFlags.fetch_or(HF_RUNTIME_TUNING_STATE_FPS_CLAMP_MANAGED, std::memory_order_acq_rel);
        return true;
    }

    bool RuntimeTuningService::ActivatePapyrusOwnerLocked(const PolicyRecord* const a_owner) noexcept
    {
        if (!a_owner) return false;
        const bool sameOwner = !_papyrusOwner.empty() && NamesEqualInsensitive(_papyrusOwner, a_owner->moduleName);
        if (!sameOwner) ReleasePapyrusOwnerLocked();

        float current = 0.0F;
        if (!GameSettingsManager::GetSingleton().GetFloat(HF_GAME_SETTING_SOURCE_FALLOUT_INI, kPapyrusBudgetName, current)) {
            Diagnostics::ReportFrameworkFailureForModule(a_owner->moduleName, a_owner->logger, HF_ERROR_RUNTIME_TUNING_SETTING_UNAVAILABLE);
            return false;
        }
        if (!FrameTimingService::GetSingleton().IsAvailable()) {
            Diagnostics::ReportFrameworkFailureForModule(a_owner->moduleName, a_owner->logger, HF_ERROR_RUNTIME_TUNING_FRAME_TIMING_UNAVAILABLE);
            return false;
        }
        if (!sameOwner) {
            ModuleContext::Scope scope{ a_owner->moduleName.c_str(), a_owner->logger };
            if (!GameSettingsManager::GetSingleton().SetFloat(HF_GAME_SETTING_SOURCE_FALLOUT_INI, kPapyrusBudgetName, current)) return false;
            _papyrusOwner = a_owner->moduleName;
            _papyrusLogger = a_owner->logger;
            _papyrusOriginalBudgetMS = current;
        }
        const auto requestedBase = a_owner->policy.papyrusBaseBudgetMS;
        const auto base = requestedBase > 0.0F ? requestedBase : (_papyrusOriginalBudgetMS > 0.0F ? _papyrusOriginalBudgetMS : current);
        if (!std::isfinite(base) || base <= 0.0F) {
            Diagnostics::ReportFrameworkFailureForModule(a_owner->moduleName, a_owner->logger, HF_ERROR_RUNTIME_TUNING_INVALID_REQUEST);
            if (!sameOwner) ReleasePapyrusOwnerLocked();
            return false;
        }
        _papyrusPriority = a_owner->policy.priority;
        _papyrusBaseBudgetMS = base;
        _currentPapyrusBudgetMS = current;
        _papyrusMaxFPS = a_owner->policy.papyrusMaxFPS;
        _lastIntervalSeconds = 1.0F / 60.0F;
        _stateFlags.fetch_or(HF_RUNTIME_TUNING_STATE_PAPYRUS_MANAGED, std::memory_order_acq_rel);
        return true;
    }

    void RuntimeTuningService::RecomputeLocked() noexcept
    {
        const PolicyRecord* clampOwner = nullptr;
        const PolicyRecord* papyrusOwner = nullptr;
        for (const auto& record : _policies) {
            if ((record.policy.flags & HF_RUNTIME_TUNING_POLICY_DISABLE_FPS_CLAMP) != 0) {
                if (!clampOwner || record.policy.priority > clampOwner->policy.priority ||
                    (record.policy.priority == clampOwner->policy.priority && record.handle < clampOwner->handle)) clampOwner = &record;
            }
            if ((record.policy.flags & HF_RUNTIME_TUNING_POLICY_DYNAMIC_PAPYRUS_BUDGET) != 0) {
                if (!papyrusOwner || record.policy.priority > papyrusOwner->policy.priority ||
                    (record.policy.priority == papyrusOwner->policy.priority && record.handle < papyrusOwner->handle)) papyrusOwner = &record;
            }
        }

        if (clampOwner) (void)ActivateClampOwnerLocked(clampOwner); else ReleaseClampOwnerLocked();
        if (papyrusOwner) (void)ActivatePapyrusOwnerLocked(papyrusOwner); else ReleasePapyrusOwnerLocked();

        _requestCount.store(static_cast<std::uint32_t>(_policies.size()), std::memory_order_release);
        _generation.fetch_add(1, std::memory_order_acq_rel);
        PresentationService::GetSingleton().SetFrameworkPresentMaintenance(
            FrameworkPresentMaintenanceReason::RuntimeTuning, !_papyrusOwner.empty());
    }

    HF_RuntimeTuningHandle RuntimeTuningService::AcquirePolicyOwned(
        const HF_RuntimeTuningPolicyV1& a_policy,
        const std::string_view a_moduleName,
        const HF_LogHandle a_logger) noexcept
    {
        if (a_moduleName.empty() || !ValidatePolicy(a_policy)) {
            if (!a_moduleName.empty()) Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName, a_logger, HF_ERROR_RUNTIME_TUNING_INVALID_REQUEST);
            return HF_INVALID_RUNTIME_TUNING_HANDLE;
        }
        try {
            auto handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
            if (handle == 0) handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
            std::scoped_lock lock{ _lock };
            _policies.push_back(PolicyRecord{ handle, a_policy, std::string{ a_moduleName }, a_logger });
            RecomputeLocked();
            return handle;
        } catch (...) {
            return HF_INVALID_RUNTIME_TUNING_HANDLE;
        }
    }

    bool RuntimeTuningService::UpdatePolicyOwned(
        const HF_RuntimeTuningHandle a_handle,
        const HF_RuntimeTuningPolicyV1& a_policy,
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
        RecomputeLocked();
        return true;
    }

    bool RuntimeTuningService::ReleasePolicyOwned(
        const HF_RuntimeTuningHandle a_handle,
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
        RecomputeLocked();
        return true;
    }

    std::uint32_t RuntimeTuningService::ReleaseOwnedBy(const std::string_view a_moduleName) noexcept
    {
        if (a_moduleName.empty()) return 0;
        std::scoped_lock lock{ _lock };
        const auto before = _policies.size();
        std::erase_if(_policies, [&](const PolicyRecord& r) { return NamesEqualInsensitive(r.moduleName, a_moduleName); });
        const auto removed = static_cast<std::uint32_t>(before - _policies.size());
        if (removed != 0) RecomputeLocked();
        return removed;
    }

    void RuntimeTuningService::MaintainOnPresent() noexcept
    {
        std::scoped_lock lock{ _lock };
        if (_papyrusOwner.empty() || _papyrusMaxFPS == 0 || _papyrusBaseBudgetMS <= 0.0F) return;
        float interval = 0.0F;
        if (!FrameTimingService::GetSingleton().GetDeltaSeconds(interval) || !std::isfinite(interval) || interval <= 0.0F) {
            _stateFlags.fetch_and(~HF_RUNTIME_TUNING_STATE_FRAME_SAMPLE_VALID, std::memory_order_acq_rel);
            return;
        }
        _stateFlags.fetch_or(HF_RUNTIME_TUNING_STATE_FRAME_SAMPLE_VALID, std::memory_order_acq_rel);
        const auto minimumInterval = 1.0F / static_cast<float>(_papyrusMaxFPS);
        interval = std::clamp(interval, minimumInterval, 1.0F / 60.0F);
        if (interval <= _lastIntervalSeconds) {
            _lastIntervalSeconds = interval;
        } else {
            _lastIntervalSeconds = std::min(_lastIntervalSeconds + interval * 0.0075F, interval);
        }
        const auto budget = _lastIntervalSeconds * (_papyrusBaseBudgetMS * 60.0F);
        if (!std::isfinite(budget) || budget <= 0.0F) return;
        ModuleContext::Scope scope{ _papyrusOwner.c_str(), _papyrusLogger };
        if (GameSettingsManager::GetSingleton().SetFloat(HF_GAME_SETTING_SOURCE_FALLOUT_INI, kPapyrusBudgetName, budget)) {
            _currentPapyrusBudgetMS = budget;
        }
    }

    bool RuntimeTuningService::GetState(HF_RuntimeTuningStateV1& a_outState) noexcept
    {
        a_outState = {};
        a_outState.structSize = sizeof(a_outState);
        a_outState.flags = _stateFlags.load(std::memory_order_acquire);
        a_outState.requestCount = _requestCount.load(std::memory_order_acquire);
        std::scoped_lock lock{ _lock };
        a_outState.effectivePapyrusPriority = _papyrusPriority;
        a_outState.papyrusBaseBudgetMS = _papyrusBaseBudgetMS;
        a_outState.currentPapyrusBudgetMS = _currentPapyrusBudgetMS;
        a_outState.lastIntervalSeconds = _lastIntervalSeconds;
        a_outState.papyrusMaxFPS = _papyrusMaxFPS;
        a_outState.originalFPSClamp = _originalFPSClampCaptured ? _originalFPSClamp : 0;
        a_outState.generation = _generation.load(std::memory_order_acquire);
        return true;
    }
}
