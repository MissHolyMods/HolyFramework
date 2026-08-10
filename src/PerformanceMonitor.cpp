#include "pch.h"
#include "PerformanceMonitor.h"

#include "Diagnostics.h"
#include "ModuleContext.h"
#include "ModuleLoader.h"

namespace HolyFramework
{
    PerformanceMonitor::Scope::Scope(
        const std::string_view a_owner,
        const std::string_view a_label,
        const std::uint32_t a_thresholdMicroseconds) noexcept :
        _owner(a_owner),
        _label(PerformanceMonitor::SafeLabel(a_label)),
        _threshold(a_thresholdMicroseconds),
        _start(std::chrono::steady_clock::now())
    {}

    PerformanceMonitor::Scope::~Scope() noexcept
    {
        if (_owner.empty() || _label.empty()) {
            return;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - _start).count();
        if (elapsed >= 0) {
            PerformanceMonitor::GetSingleton().Record(
                _owner,
                _label,
                static_cast<std::uint64_t>(elapsed),
                _threshold);
        }
    }

    PerformanceMonitor& PerformanceMonitor::GetSingleton() noexcept
    {
        static PerformanceMonitor* instance = new PerformanceMonitor();
        return *instance;
    }

    bool PerformanceMonitor::NamesEqual(const std::string_view a_left, const std::string_view a_right) noexcept
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

    std::string PerformanceMonitor::SafeLabel(const std::string_view a_label) noexcept
    {
        if (a_label.empty()) {
            return {};
        }
        return std::string{ a_label.substr(0, 95) };
    }

    bool PerformanceMonitor::ResolveOwner(const void* const a_callerAddress, std::string& a_owner) const noexcept
    {
        const auto context = ModuleContext::Current();
        if (context.name && *context.name) {
            a_owner = context.name;
            return true;
        }

        HF_LogHandle logger{};
        if (a_callerAddress && ModuleLoader::GetSingleton().FindExecutionIdentityByCodeAddress(a_callerAddress, a_owner, logger)) {
            return true;
        }
        return false;
    }

    HF_PerfSampleHandle PerformanceMonitor::BeginSample(
        const std::string_view a_label,
        const std::uint32_t a_warningThresholdMicroseconds,
        const void* const a_callerAddress) noexcept
    {
        const auto label = SafeLabel(a_label);
        if (label.empty()) {
            return HF_INVALID_PERF_SAMPLE_HANDLE;
        }

        std::string owner;
        if (!ResolveOwner(a_callerAddress, owner)) {
            Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_PERFORMANCE_OWNER_UNKNOWN);
            return HF_INVALID_PERF_SAMPLE_HANDLE;
        }

        auto handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
        if (handle == HF_INVALID_PERF_SAMPLE_HANDLE) {
            handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
        }

        ActiveSample sample{
            .handle = handle,
            .owner = std::move(owner),
            .label = label,
            .threshold = a_warningThresholdMicroseconds,
            .start = std::chrono::steady_clock::now()
        };
        std::scoped_lock lock{ _lock };
        _activeSamples.emplace(handle, std::move(sample));
        return handle;
    }

    bool PerformanceMonitor::EndSample(const HF_PerfSampleHandle a_handle, const void* const a_callerAddress) noexcept
    {
        if (a_handle == HF_INVALID_PERF_SAMPLE_HANDLE) {
            return false;
        }

        std::string callerOwner;
        if (!ResolveOwner(a_callerAddress, callerOwner)) {
            Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_PERFORMANCE_OWNER_UNKNOWN);
            return false;
        }

        ActiveSample sample;
        {
            std::scoped_lock lock{ _lock };
            const auto it = _activeSamples.find(a_handle);
            if (it == _activeSamples.end()) {
                return false;
            }
            if (!NamesEqual(it->second.owner, callerOwner)) {
                Diagnostics::ReportFrameworkFailureForModule(
                    callerOwner,
                    ModuleContext::Current().logger,
                    HF_ERROR_PERFORMANCE_OWNER_MISMATCH);
                return false;
            }
            sample = std::move(it->second);
            _activeSamples.erase(it);
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - sample.start).count();
        if (elapsed < 0) {
            return false;
        }
        Record(sample.owner, sample.label, static_cast<std::uint64_t>(elapsed), sample.threshold);
        return true;
    }

    void PerformanceMonitor::Record(
        const std::string_view a_owner,
        const std::string_view a_label,
        const std::uint64_t a_elapsedMicroseconds,
        const std::uint32_t a_warningThresholdMicroseconds) noexcept
    {
        if (a_owner.empty() || a_label.empty()) {
            return;
        }

        const auto threshold = a_warningThresholdMicroseconds != 0 ?
            a_warningThresholdMicroseconds : kDefaultThresholdMicroseconds;
        const auto now = std::chrono::steady_clock::now();

        bool emitWarning = false;
        std::uint64_t callCount = 0;
        {
            std::scoped_lock lock{ _lock };
            auto it = std::ranges::find_if(_stats, [&](const StatsRecord& record) {
                return NamesEqual(record.owner, a_owner) && record.label == a_label;
            });
            if (it == _stats.end()) {
                _stats.push_back(StatsRecord{
                    .owner = std::string{ a_owner },
                    .label = SafeLabel(a_label),
                    .warningThresholdMicroseconds = threshold
                });
                it = std::prev(_stats.end());
            }

            auto& record = *it;
            record.warningThresholdMicroseconds = threshold;
            ++record.calls;
            record.totalMicroseconds += a_elapsedMicroseconds;
            record.lastMicroseconds = a_elapsedMicroseconds;
            record.maxMicroseconds = std::max(record.maxMicroseconds, a_elapsedMicroseconds);
            callCount = record.calls;

            if (a_elapsedMicroseconds >= threshold) {
                ++record.slowCalls;
                if (record.lastWarning.time_since_epoch().count() == 0 || now - record.lastWarning >= kWarningCooldown) {
                    record.lastWarning = now;
                    emitWarning = true;
                }
            }
        }

        if (emitWarning) {
            Diagnostics::ReportPerformanceWarning(
                a_owner,
                a_label,
                a_elapsedMicroseconds,
                threshold,
                callCount);
        }
    }

    void PerformanceMonitor::FillRecord(const StatsRecord& a_record, HF_PerformanceRecordV1& a_out) noexcept
    {
        a_out = {};
        a_out.structSize = sizeof(HF_PerformanceRecordV1);
        a_out.calls = a_record.calls;
        a_out.totalMicroseconds = a_record.totalMicroseconds;
        a_out.maxMicroseconds = a_record.maxMicroseconds;
        a_out.lastMicroseconds = a_record.lastMicroseconds;
        a_out.slowCalls = a_record.slowCalls;
        a_out.warningThresholdMicroseconds = a_record.warningThresholdMicroseconds;
        std::snprintf(a_out.owner, sizeof(a_out.owner), "%s", a_record.owner.c_str());
        std::snprintf(a_out.label, sizeof(a_out.label), "%s", a_record.label.c_str());
    }

    std::uint32_t PerformanceMonitor::GetCount() const noexcept
    {
        std::scoped_lock lock{ _lock };
        return static_cast<std::uint32_t>(_stats.size());
    }

    bool PerformanceMonitor::GetByIndex(const std::uint32_t a_index, HF_PerformanceRecordV1& a_out) const noexcept
    {
        std::scoped_lock lock{ _lock };
        if (a_index >= _stats.size()) {
            return false;
        }
        FillRecord(_stats[a_index], a_out);
        return true;
    }

    std::uint32_t PerformanceMonitor::RemoveOwnedBy(const std::string_view a_moduleName) noexcept
    {
        if (a_moduleName.empty()) {
            return 0;
        }
        std::scoped_lock lock{ _lock };
        const auto oldStats = _stats.size();
        std::erase_if(_stats, [&](const StatsRecord& record) {
            return NamesEqual(record.owner, a_moduleName);
        });
        std::erase_if(_activeSamples, [&](const auto& pair) {
            return NamesEqual(pair.second.owner, a_moduleName);
        });
        return static_cast<std::uint32_t>(oldStats - _stats.size());
    }
}
