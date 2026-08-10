#include "pch.h"
#include "CPUSchedulingService.h"

#include "Diagnostics.h"
#include "PresentationService.h"

namespace HolyFramework
{
    namespace
    {
        inline constexpr std::uint32_t kCpuSetInformationType = 0;
        inline constexpr std::uint8_t kCpuSetParkedFlag = 0x1;
        inline constexpr std::uint8_t kCpuSetAllocatedFlag = 0x2;
        inline constexpr std::uint8_t kCpuSetAllocatedToTargetProcessFlag = 0x4;

        struct SystemCpuSetInformation
        {
            std::uint32_t size{};
            std::uint32_t type{};
            std::uint32_t id{};
            std::uint16_t group{};
            std::uint8_t logicalProcessorIndex{};
            std::uint8_t coreIndex{};
            std::uint8_t lastLevelCacheIndex{};
            std::uint8_t numaNodeIndex{};
            std::uint8_t efficiencyClass{};
            std::uint8_t allFlags{};
            std::uint32_t reserved{};
            std::uint64_t allocationTag{};
        };
        static_assert(sizeof(SystemCpuSetInformation) == 32);
    }

    CPUSchedulingService& CPUSchedulingService::GetSingleton() noexcept
    {
        static CPUSchedulingService* instance = new CPUSchedulingService();
        return *instance;
    }

    bool CPUSchedulingService::NamesEqualInsensitive(const std::string_view a_left, const std::string_view a_right) noexcept
    {
        if (a_left.size() != a_right.size()) return false;
        for (std::size_t i = 0; i < a_left.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a_left[i])) != std::tolower(static_cast<unsigned char>(a_right[i]))) return false;
        }
        return true;
    }

    bool CPUSchedulingService::ValidRequest(const std::uint32_t a_maxLogicalProcessors, const std::uint32_t a_timeoutMs) noexcept
    {
        return a_timeoutMs <= HF_CPU_SCHEDULING_MAX_TIMEOUT_MS && a_maxLogicalProcessors <= 1024;
    }

    void CPUSchedulingService::Prepare() noexcept
    {
        std::call_once(_prepareOnce, [this]() noexcept {
            _processHandle = REX::W32::GetCurrentProcess();
            if (!_processHandle) return;
            const auto kernel32 = REX::W32::GetModuleHandleW(L"kernel32.dll");
            if (!kernel32) return;

            const auto getDefaults = reinterpret_cast<GetProcessDefaultCpuSets_t>(REX::W32::GetProcAddress(kernel32, "GetProcessDefaultCpuSets"));
            _setProcessDefaultCpuSets = reinterpret_cast<SetProcessDefaultCpuSets_t>(REX::W32::GetProcAddress(kernel32, "SetProcessDefaultCpuSets"));
            const auto getCpuSets = reinterpret_cast<GetSystemCpuSetInformation_t>(REX::W32::GetProcAddress(kernel32, "GetSystemCpuSetInformation"));
            const auto getAffinity = reinterpret_cast<GetProcessAffinityMask_t>(REX::W32::GetProcAddress(kernel32, "GetProcessAffinityMask"));
            _setProcessAffinityMask = reinterpret_cast<SetProcessAffinityMask_t>(REX::W32::GetProcAddress(kernel32, "SetProcessAffinityMask"));

            std::uintptr_t systemMask = 0;
            if (getAffinity && _setProcessAffinityMask &&
                getAffinity(_processHandle, std::addressof(_originalAffinityMask), std::addressof(systemMask)) != 0 &&
                _originalAffinityMask != 0) {
                _legacyAffinityAvailable = true;
            }

            if (getDefaults && _setProcessDefaultCpuSets && getCpuSets) {
                std::uint32_t originalCount = 0;
                const auto queriedDefaults = getDefaults(_processHandle, nullptr, 0, std::addressof(originalCount));
                if (queriedDefaults != 0 || originalCount != 0) {
                    _originalDefaultCpuSetsCaptured = true;
                    if (originalCount != 0) {
                        _originalDefaultCpuSets.resize(originalCount);
                        std::uint32_t filled = originalCount;
                        if (getDefaults(_processHandle, _originalDefaultCpuSets.data(), originalCount, std::addressof(filled)) == 0 || filled > originalCount) {
                            _originalDefaultCpuSets.clear();
                            _originalDefaultCpuSetsCaptured = false;
                        } else {
                            _originalDefaultCpuSets.resize(filled);
                        }
                    }
                }

                if (_originalDefaultCpuSetsCaptured) {
                    std::uint32_t requiredBytes = 0;
                    (void)getCpuSets(nullptr, 0, std::addressof(requiredBytes), _processHandle, 0);
                    if (requiredBytes >= sizeof(SystemCpuSetInformation)) {
                        std::vector<std::uint8_t> buffer(requiredBytes);
                        std::uint32_t returned = requiredBytes;
                        if (getCpuSets(buffer.data(), requiredBytes, std::addressof(returned), _processHandle, 0) != 0) {
                            std::size_t offset = 0;
                            while (offset + 8 <= returned) {
                                const auto* info = reinterpret_cast<const SystemCpuSetInformation*>(buffer.data() + offset);
                                if (info->size < 8 || offset + info->size > returned) break;
                                if (info->type == kCpuSetInformationType && info->size >= sizeof(SystemCpuSetInformation)) {
                                    const bool parked = (info->allFlags & kCpuSetParkedFlag) != 0;
                                    const bool allocated = (info->allFlags & kCpuSetAllocatedFlag) != 0;
                                    const bool allocatedToUs = (info->allFlags & kCpuSetAllocatedToTargetProcessFlag) != 0;
                                    const bool allowedDefaults = _originalDefaultCpuSets.empty() ||
                                        std::find(_originalDefaultCpuSets.begin(), _originalDefaultCpuSets.end(), info->id) != _originalDefaultCpuSets.end();
                                    const bool allowedLegacy = !_legacyAffinityAvailable || info->group != 0 ||
                                        info->logicalProcessorIndex >= sizeof(std::uintptr_t) * 8 ||
                                        (_originalAffinityMask & (static_cast<std::uintptr_t>(1) << info->logicalProcessorIndex)) != 0;
                                    if (!parked && (!allocated || allocatedToUs) && allowedDefaults && allowedLegacy) {
                                        _eligibleCpuSetIds.push_back(info->id);
                                    }
                                }
                                offset += info->size;
                            }
                        }
                    }
                }
            }

            std::uint32_t legacyCount = 0;
            auto mask = _originalAffinityMask;
            while (mask != 0) { legacyCount += static_cast<std::uint32_t>(mask & 1); mask >>= 1; }
            const auto available = !_eligibleCpuSetIds.empty() ? static_cast<std::uint32_t>(_eligibleCpuSetIds.size()) : legacyCount;
            _availableCount.store(available, std::memory_order_release);
            std::uint32_t flags = 0;
            if (available != 0) flags |= HF_CPU_SCHEDULING_STATE_AVAILABLE;
            _stateFlags.store(flags, std::memory_order_release);
        });
    }

    bool CPUSchedulingService::IsAvailable() noexcept
    {
        Prepare();
        return (_stateFlags.load(std::memory_order_acquire) & HF_CPU_SCHEDULING_STATE_AVAILABLE) != 0;
    }

    std::uintptr_t CPUSchedulingService::BuildLegacyMask(const std::uint32_t a_count) const noexcept
    {
        std::uintptr_t result = 0;
        std::uint32_t selected = 0;
        for (std::uint32_t bit = 0; bit < sizeof(std::uintptr_t) * 8 && selected < a_count; ++bit) {
            const auto candidate = static_cast<std::uintptr_t>(1) << bit;
            if ((_originalAffinityMask & candidate) != 0) {
                result |= candidate;
                ++selected;
            }
        }
        return result;
    }

    bool CPUSchedulingService::RestoreLocked() noexcept
    {
        if (_appliedMode == ApplyMode::None) return true;
        bool ok = false;
        if (_appliedMode == ApplyMode::CpuSets && _setProcessDefaultCpuSets && _originalDefaultCpuSetsCaptured) {
            ok = _setProcessDefaultCpuSets(
                _processHandle,
                _originalDefaultCpuSets.empty() ? nullptr : _originalDefaultCpuSets.data(),
                static_cast<std::uint32_t>(_originalDefaultCpuSets.size())) != 0;
        } else if (_appliedMode == ApplyMode::LegacyAffinity && _setProcessAffinityMask && _originalAffinityMask != 0) {
            ok = _setProcessAffinityMask(_processHandle, _originalAffinityMask) != 0;
        }
        if (ok) {
            _appliedMode = ApplyMode::None;
            _appliedCount.store(0, std::memory_order_release);
            auto flags = _stateFlags.load(std::memory_order_acquire) & HF_CPU_SCHEDULING_STATE_AVAILABLE;
            _stateFlags.store(flags, std::memory_order_release);
        }
        return ok;
    }

    bool CPUSchedulingService::ApplyTargetLocked(const std::uint32_t a_target) noexcept
    {
        if (a_target == 0) return RestoreLocked();
        const auto available = _availableCount.load(std::memory_order_acquire);
        if (available == 0) return false;
        const auto desired = std::min(a_target, available);

        if (!_eligibleCpuSetIds.empty() && _setProcessDefaultCpuSets && _originalDefaultCpuSetsCaptured) {
            if (_appliedMode == ApplyMode::LegacyAffinity && !RestoreLocked()) return false;
            if (_setProcessDefaultCpuSets(_processHandle, _eligibleCpuSetIds.data(), desired) != 0) {
                _appliedMode = ApplyMode::CpuSets;
                _appliedCount.store(desired, std::memory_order_release);
                _stateFlags.store(HF_CPU_SCHEDULING_STATE_AVAILABLE | HF_CPU_SCHEDULING_STATE_ACTIVE | HF_CPU_SCHEDULING_STATE_CPU_SETS, std::memory_order_release);
                return true;
            }
        }

        if (_legacyAffinityAvailable && _setProcessAffinityMask) {
            if (_appliedMode == ApplyMode::CpuSets && !RestoreLocked()) return false;
            const auto mask = BuildLegacyMask(desired);
            if (mask != 0 && _setProcessAffinityMask(_processHandle, mask) != 0) {
                _appliedMode = ApplyMode::LegacyAffinity;
                _appliedCount.store(desired, std::memory_order_release);
                _stateFlags.store(HF_CPU_SCHEDULING_STATE_AVAILABLE | HF_CPU_SCHEDULING_STATE_ACTIVE | HF_CPU_SCHEDULING_STATE_LEGACY_AFFINITY, std::memory_order_release);
                return true;
            }
        }
        return false;
    }

    void CPUSchedulingService::RecomputeLocked() noexcept
    {
        std::uint32_t target = 0;
        bool timed = false;
        for (const auto& record : _limits) {
            if (record.timed) timed = true;
            if (record.maxLogicalProcessors == 0) continue;
            if (target == 0 || record.maxLogicalProcessors < target) target = record.maxLogicalProcessors;
        }
        _requestCount.store(static_cast<std::uint32_t>(_limits.size()), std::memory_order_release);
        const auto previous = _activeMax.exchange(target, std::memory_order_acq_rel);
        if (previous != target) _generation.fetch_add(1, std::memory_order_acq_rel);
        if (!ApplyTargetLocked(target)) {
            if (target != 0 || _appliedMode != ApplyMode::None) {
                Diagnostics::ReportFrameworkFailureForModule(
                    "HolyFramework", HF_INVALID_LOG_HANDLE, HF_ERROR_CPU_SCHEDULING_APPLY_FAILED);
            }
        }
        PresentationService::GetSingleton().SetFrameworkPresentMaintenance(
            FrameworkPresentMaintenanceReason::CPUScheduling, timed);
    }

    bool CPUSchedulingService::GetState(HF_CPUSchedulingStateV1& a_outState) noexcept
    {
        Prepare();
        a_outState = {};
        a_outState.structSize = sizeof(a_outState);
        a_outState.flags = _stateFlags.load(std::memory_order_acquire);
        a_outState.activeMaxLogicalProcessors = _activeMax.load(std::memory_order_acquire);
        a_outState.requestCount = _requestCount.load(std::memory_order_acquire);
        a_outState.appliedLogicalProcessors = _appliedCount.load(std::memory_order_acquire);
        a_outState.availableLogicalProcessors = _availableCount.load(std::memory_order_acquire);
        a_outState.generation = _generation.load(std::memory_order_acquire);
        return (a_outState.flags & HF_CPU_SCHEDULING_STATE_AVAILABLE) != 0;
    }

    HF_CPUSchedulingHandle CPUSchedulingService::AcquireLimitOwned(
        const std::uint32_t a_maxLogicalProcessors,
        const std::uint32_t a_timeoutMs,
        const std::string_view a_moduleName,
        const HF_LogHandle a_logger) noexcept
    {
        Prepare();
        if (a_moduleName.empty() || !IsAvailable() || !ValidRequest(a_maxLogicalProcessors, a_timeoutMs)) {
            if (!a_moduleName.empty()) Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName, a_logger, HF_ERROR_CPU_SCHEDULING_INVALID_REQUEST);
            return HF_INVALID_CPU_SCHEDULING_HANDLE;
        }
        try {
            auto handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
            if (handle == 0) handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
            LimitRecord record{};
            record.handle = handle;
            record.maxLogicalProcessors = a_maxLogicalProcessors;
            record.timed = a_timeoutMs != 0;
            if (record.timed) record.expiresAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(a_timeoutMs);
            record.moduleName = a_moduleName;
            record.logger = a_logger;
            std::scoped_lock lock{ _lock };
            _limits.push_back(std::move(record));
            RecomputeLocked();
            return handle;
        } catch (...) {
            return HF_INVALID_CPU_SCHEDULING_HANDLE;
        }
    }

    bool CPUSchedulingService::UpdateLimitOwned(
        const HF_CPUSchedulingHandle a_handle,
        const std::uint32_t a_maxLogicalProcessors,
        const std::uint32_t a_timeoutMs,
        const std::string_view a_moduleName,
        std::string* const a_outActualOwner) noexcept
    {
        if (a_outActualOwner) a_outActualOwner->clear();
        if (a_handle == 0 || !ValidRequest(a_maxLogicalProcessors, a_timeoutMs)) return false;
        std::scoped_lock lock{ _lock };
        const auto it = std::ranges::find_if(_limits, [&](const LimitRecord& r) { return r.handle == a_handle; });
        if (it == _limits.end()) return false;
        if (!NamesEqualInsensitive(it->moduleName, a_moduleName)) {
            if (a_outActualOwner) *a_outActualOwner = it->moduleName;
            return false;
        }
        it->maxLogicalProcessors = a_maxLogicalProcessors;
        it->timed = a_timeoutMs != 0;
        it->expiresAt = it->timed ? std::chrono::steady_clock::now() + std::chrono::milliseconds(a_timeoutMs) : std::chrono::steady_clock::time_point{};
        RecomputeLocked();
        return true;
    }

    bool CPUSchedulingService::ReleaseLimitOwned(
        const HF_CPUSchedulingHandle a_handle,
        const std::string_view a_moduleName,
        std::string* const a_outActualOwner) noexcept
    {
        if (a_outActualOwner) a_outActualOwner->clear();
        if (a_handle == 0) return false;
        std::scoped_lock lock{ _lock };
        const auto it = std::ranges::find_if(_limits, [&](const LimitRecord& r) { return r.handle == a_handle; });
        if (it == _limits.end()) return false;
        if (!NamesEqualInsensitive(it->moduleName, a_moduleName)) {
            if (a_outActualOwner) *a_outActualOwner = it->moduleName;
            return false;
        }
        _limits.erase(it);
        RecomputeLocked();
        return true;
    }

    std::uint32_t CPUSchedulingService::ReleaseOwnedBy(const std::string_view a_moduleName) noexcept
    {
        if (a_moduleName.empty()) return 0;
        std::scoped_lock lock{ _lock };
        const auto before = _limits.size();
        std::erase_if(_limits, [&](const LimitRecord& r) { return NamesEqualInsensitive(r.moduleName, a_moduleName); });
        const auto removed = static_cast<std::uint32_t>(before - _limits.size());
        if (removed != 0) RecomputeLocked();
        return removed;
    }

    void CPUSchedulingService::MaintainOnPresent() noexcept
    {
        const auto now = std::chrono::steady_clock::now();
        std::scoped_lock lock{ _lock };
        const auto before = _limits.size();
        std::erase_if(_limits, [&](const LimitRecord& r) { return r.timed && now >= r.expiresAt; });
        if (_limits.size() != before) RecomputeLocked();
    }
}
