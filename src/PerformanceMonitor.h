#pragma once

namespace HolyFramework
{
    class PerformanceMonitor final
    {
    public:
        class Scope final
        {
        public:
            Scope(std::string_view a_owner, std::string_view a_label, std::uint32_t a_thresholdMicroseconds = 0) noexcept;
            ~Scope() noexcept;

            Scope(const Scope&) = delete;
            Scope& operator=(const Scope&) = delete;

        private:
            std::string _owner;
            std::string _label;
            std::uint32_t _threshold{ 0 };
            std::chrono::steady_clock::time_point _start{};
        };

        static PerformanceMonitor& GetSingleton() noexcept;

        HF_PerfSampleHandle BeginSample(
            std::string_view a_label,
            std::uint32_t a_warningThresholdMicroseconds,
            const void* a_callerAddress) noexcept;
        bool EndSample(HF_PerfSampleHandle a_handle, const void* a_callerAddress) noexcept;

        void Record(
            std::string_view a_owner,
            std::string_view a_label,
            std::uint64_t a_elapsedMicroseconds,
            std::uint32_t a_warningThresholdMicroseconds) noexcept;

        std::uint32_t GetCount() const noexcept;
        bool GetByIndex(std::uint32_t a_index, HF_PerformanceRecordV1& a_out) const noexcept;
        std::uint32_t RemoveOwnedBy(std::string_view a_moduleName) noexcept;

    private:
        struct StatsRecord
        {
            std::string owner;
            std::string label;
            std::uint64_t calls{ 0 };
            std::uint64_t totalMicroseconds{ 0 };
            std::uint64_t maxMicroseconds{ 0 };
            std::uint64_t lastMicroseconds{ 0 };
            std::uint64_t slowCalls{ 0 };
            std::uint32_t warningThresholdMicroseconds{ 0 };
            std::chrono::steady_clock::time_point lastWarning{};
        };

        struct ActiveSample
        {
            HF_PerfSampleHandle handle{ HF_INVALID_PERF_SAMPLE_HANDLE };
            std::string owner;
            std::string label;
            std::uint32_t threshold{ 0 };
            std::chrono::steady_clock::time_point start{};
        };

        PerformanceMonitor() = default;

        static constexpr std::uint32_t kDefaultThresholdMicroseconds = 16'000;
        static constexpr auto kWarningCooldown = std::chrono::seconds{ 30 };

        bool ResolveOwner(const void* a_callerAddress, std::string& a_owner) const noexcept;
        static bool NamesEqual(std::string_view a_left, std::string_view a_right) noexcept;
        static std::string SafeLabel(std::string_view a_label) noexcept;
        static void FillRecord(const StatsRecord& a_record, HF_PerformanceRecordV1& a_out) noexcept;

        mutable std::mutex _lock;
        std::vector<StatsRecord> _stats;
        std::unordered_map<HF_PerfSampleHandle, ActiveSample> _activeSamples;
        std::atomic<HF_PerfSampleHandle> _nextHandle{ 1 };
    };
}
