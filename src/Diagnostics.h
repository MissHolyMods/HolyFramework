#pragma once

namespace HolyFramework
{
    class Diagnostics final
    {
    public:
        static void Install() noexcept;

        // Public/module-local reporting. The 3-letter namespace is resolved from
        // the active module's <Module>.toml catalog. Coded failures are also
        // recorded in the shared persistent HolyFramework_error.log history.
        static void ReportFailure(HF_ErrorCode a_code) noexcept;

        // Internal HolyFramework failures always use the HFW namespace even when
        // the failure occurred while supervising a module.
        static void ReportFrameworkFailureForModule(
            std::string_view a_moduleName,
            HF_LogHandle a_logger,
            HF_ErrorCode a_code) noexcept;

        // Non-fatal framework observations (for example an expected logical
        // resource conflict) remain session-only and do not enter the persistent
        // coded-error history.
        static void ReportFrameworkWarningForModule(
            std::string_view a_moduleName,
            HF_LogHandle a_logger,
            HF_ErrorCode a_code) noexcept;

        // Performance anomalies are session warnings, not module failures. They
        // remain in the normal HolyFramework log and never enter the persistent
        // coded-error history or degrade module health.
        static void ReportPerformanceWarning(
            std::string_view a_moduleName,
            std::string_view a_label,
            std::uint64_t a_elapsedMicroseconds,
            std::uint32_t a_thresholdMicroseconds,
            std::uint64_t a_callCount) noexcept;

    private:
        static void ReportFailureWithPrefix(
            std::string_view a_moduleName,
            HF_LogHandle a_logger,
            std::string_view a_prefix,
            HF_ErrorCode a_code) noexcept;
    };
}
