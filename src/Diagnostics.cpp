#include "pch.h"
#include "Diagnostics.h"
#include "ErrorCatalog.h"
#include "ModuleContext.h"
#include "ModuleLoader.h"
#include "ModuleLogManager.h"

#include <Windows.h>

// <Windows.h> defines ERROR as a legacy macro (0), which collides with REX::ERROR.
#ifdef ERROR
#  undef ERROR
#endif

namespace
{
    std::filesystem::path g_errorHistoryPath;
    std::mutex g_errorHistoryLock;
    PVOID g_vectoredHandler{ nullptr };

    inline constexpr std::size_t kPersistentErrorLimit = 100;

    HF_ErrorCode NativeExceptionToHFCode(const DWORD a_code) noexcept
    {
        switch (a_code) {
        case EXCEPTION_ACCESS_VIOLATION:
            return HF_ERROR_NATIVE_ACCESS_VIOLATION;
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            return HF_ERROR_NATIVE_ILLEGAL_INSTRUCTION;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            return HF_ERROR_NATIVE_DIVIDE_BY_ZERO;
        case EXCEPTION_PRIV_INSTRUCTION:
            return HF_ERROR_NATIVE_PRIVILEGED_INSTRUCTION;
        case EXCEPTION_STACK_OVERFLOW:
            return HF_ERROR_NATIVE_STACK_OVERFLOW;
        default:
            return HF_ERROR_NATIVE_FATAL_UNKNOWN;
        }
    }

    bool IsObservedNativeException(const DWORD a_code) noexcept
    {
        switch (a_code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_STACK_OVERFLOW:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] std::string PersistentTimestamp() noexcept
    {
        SYSTEMTIME localTime{};
        ::GetLocalTime(std::addressof(localTime));

        char buffer[32]{};
        const auto length = std::snprintf(
            buffer,
            sizeof(buffer),
            "%04u-%02u-%02u %02u:%02u:%02u",
            static_cast<unsigned>(localTime.wYear),
            static_cast<unsigned>(localTime.wMonth),
            static_cast<unsigned>(localTime.wDay),
            static_cast<unsigned>(localTime.wHour),
            static_cast<unsigned>(localTime.wMinute),
            static_cast<unsigned>(localTime.wSecond));
        if (length <= 0) {
            return {};
        }
        return std::string{ buffer, static_cast<std::size_t>(length) };
    }

    void AppendPersistentError(
        const std::string_view a_moduleName,
        const std::string_view a_prefix,
        const HF_ErrorCode a_code,
        const bool a_nonBlocking = false) noexcept
    {
        if (g_errorHistoryPath.empty() || a_code == HF_ERROR_NONE) {
            return;
        }

        try {
            std::unique_lock lock{ g_errorHistoryLock, std::defer_lock };
            if (a_nonBlocking) {
                if (!lock.try_lock()) {
                    return;
                }
            } else {
                lock.lock();
            }

            std::vector<std::string> existing;
            if (std::ifstream input{ g_errorHistoryPath, std::ios::binary }; input) {
                std::string line;
                while (std::getline(input, line)) {
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    if (!line.empty()) {
                        existing.push_back(std::move(line));
                    }
                }
            }

            const auto owner = a_prefix == "HFW" ?
                std::string{ "HolyFramework" } :
                (a_moduleName.empty() ? std::string{ a_prefix } : std::string{ a_moduleName });

            const auto line = std::format(
                "{} | {} | error.{:05}",
                PersistentTimestamp(),
                owner.empty() ? "Unknown" : owner,
                static_cast<std::uint32_t>(a_code));

            const auto keepCount = std::min<std::size_t>(
                existing.size(),
                kPersistentErrorLimit - 1);
            const auto begin = existing.size() - keepCount;

            auto temporaryPath = g_errorHistoryPath;
            temporaryPath += ".tmp";
            {
                std::ofstream output{ temporaryPath, std::ios::binary | std::ios::trunc };
                if (!output) {
                    return;
                }
                for (std::size_t i = begin; i < existing.size(); ++i) {
                    output << existing[i] << "\r\n";
                }
                output << line << "\r\n";
                output.flush();
                if (!output) {
                    return;
                }
            }

            const auto temporaryPathWide = temporaryPath.wstring();
            const auto historyPathWide = g_errorHistoryPath.wstring();
            if (!::MoveFileExW(
                    temporaryPathWide.c_str(),
                    historyPathWide.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                ::DeleteFileW(temporaryPathWide.c_str());
            }
        } catch (...) {
            // Persistent diagnostics must never destabilize the game.
        }
    }

    void RecordNativeException(const DWORD a_nativeCode) noexcept
    {
        const auto hfCode = NativeExceptionToHFCode(a_nativeCode);
        AppendPersistentError({}, "HFW", hfCode, true);
    }

    LONG CALLBACK VectoredHandler(PEXCEPTION_POINTERS a_exception) noexcept
    {
        if (!a_exception || !a_exception->ExceptionRecord) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        const auto nativeCode = a_exception->ExceptionRecord->ExceptionCode;
        if (!IsObservedNativeException(nativeCode)) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        const auto context = HolyFramework::ModuleContext::Current();
        if (!context.name) {
            // Only correlate fatal native exceptions while our module code is
            // executing inside a HolyFramework-supervised scope.
            return EXCEPTION_CONTINUE_SEARCH;
        }

        // Persist only the compact coded error. Never attempt recovery from
        // arbitrary native corruption here; the normal exception chain
        // continues so crash loggers/Windows can act.
        RecordNativeException(nativeCode);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    std::filesystem::path ResolveF4SELogDirectory()
    {
        wchar_t* knownBuffer{ nullptr };
        const auto result = REX::W32::SHGetKnownFolderPath(
            REX::W32::FOLDERID_Documents,
            REX::W32::KF_FLAG_DEFAULT,
            nullptr,
            std::addressof(knownBuffer));
        std::unique_ptr<wchar_t[], decltype(&REX::W32::CoTaskMemFree)> knownPath(
            knownBuffer,
            REX::W32::CoTaskMemFree);

        if (!knownPath || result != 0) {
            return {};
        }

        std::filesystem::path path = knownPath.get();
        path /= "My Games";
        path /= std::string(F4SE::GetSaveFolderName());
        path /= "F4SE";
        return path;
    }

}

namespace HolyFramework
{
    void Diagnostics::Install() noexcept
    {
        const auto directory = ResolveF4SELogDirectory();
        if (directory.empty()) {
            REX::WARN("HolyFramework diagnostics could not resolve its log path");
            return;
        }

        std::error_code directoryError;
        std::filesystem::create_directories(directory, directoryError);
        if (directoryError) {
            REX::WARN("HolyFramework could not prepare its diagnostic directory");
        }
        g_errorHistoryPath = directory / L"HolyFramework_error.log";

        g_vectoredHandler = ::AddVectoredExceptionHandler(0, VectoredHandler);
        if (g_vectoredHandler) {
            REX::INFO("HolyFramework diagnostic exception observer installed");
        } else {
            REX::WARN("HolyFramework diagnostic exception observer could not be installed");
        }
    }

    void Diagnostics::ReportFailure(const HF_ErrorCode a_code) noexcept
    {
        const auto context = ModuleContext::Current();
        const std::string_view moduleName =
            context.name ? std::string_view{ context.name } : std::string_view{};
        const auto prefix = ResolveModuleErrorPrefix(moduleName);

        ReportFailureWithPrefix(
            moduleName,
            context.logger,
            prefix,
            a_code);
    }

    void Diagnostics::ReportFrameworkFailureForModule(
        const std::string_view a_moduleName,
        const HF_LogHandle a_logger,
        const HF_ErrorCode a_code) noexcept
    {
        ReportFailureWithPrefix(
            a_moduleName,
            a_logger,
            "HFW",
            a_code);
    }

    void Diagnostics::ReportFrameworkWarningForModule(
        const std::string_view a_moduleName,
        const HF_LogHandle a_logger,
        const HF_ErrorCode a_code) noexcept
    {
        try {
            const auto current = ModuleContext::Current();
            const auto checkpoint = current.name && a_moduleName == current.name ? current.checkpoint : 0;
            const auto codeText = FormatErrorCode("HFW", a_code);
            if (a_logger != HF_INVALID_LOG_HANDLE) {
                const auto reference = checkpoint != 0 ?
                    std::format("{} (checkpoint={})", codeText, checkpoint) :
                    codeText;
                ModuleLogManager::GetSingleton().Write(a_logger, HF_LOG_WARNING, reference);
            } else {
                REX::WARN("{} | module={}", codeText, a_moduleName.empty() ? "HolyFramework" : a_moduleName);
            }
        } catch (...) {
        }
    }


    void Diagnostics::ReportPerformanceWarning(
        const std::string_view a_moduleName,
        const std::string_view a_label,
        const std::uint64_t a_elapsedMicroseconds,
        const std::uint32_t a_thresholdMicroseconds,
        const std::uint64_t a_callCount) noexcept
    {
        try {
            REX::WARN(
                "{} | module={} | label={} | elapsed_us={} | threshold_us={} | calls={}",
                HolyFramework::FormatErrorCode("HFW", HF_ERROR_PERFORMANCE_SLOW_CALL),
                a_moduleName.empty() ? "HolyFramework" : a_moduleName,
                a_label.empty() ? "<unnamed>" : a_label,
                a_elapsedMicroseconds,
                a_thresholdMicroseconds,
                a_callCount);
        } catch (...) {
        }
    }

    void Diagnostics::ReportFailureWithPrefix(
        const std::string_view a_moduleName,
        const HF_LogHandle a_logger,
        const std::string_view a_prefix,
        const HF_ErrorCode a_code) noexcept
    {
        if (a_code == HF_ERROR_NONE) {
            return;
        }

        const auto current = ModuleContext::Current();
        const auto checkpoint = current.name && a_moduleName == current.name ? current.checkpoint : 0;
        const auto codeText = FormatErrorCode(a_prefix, a_code);
        try {
            // Session logs and the persistent history keep only compact coded
            // references. Error meaning lives in the TOML catalogs.
            if (a_logger != HF_INVALID_LOG_HANDLE) {
                const auto reference = checkpoint != 0 ?
                    std::format("{} (checkpoint={})", codeText, checkpoint) :
                    codeText;
                ModuleLogManager::GetSingleton().Write(a_logger, HF_LOG_ERROR, reference);
                ModuleLogManager::GetSingleton().Flush(a_logger);
            } else {
                REX::ERROR("{} | module={}", codeText, a_moduleName.empty() ? "HolyFramework" : a_moduleName);
            }

            AppendPersistentError(a_moduleName, a_prefix, a_code);
            if (!a_moduleName.empty()) {
                ModuleLoader::GetSingleton().MarkDegraded(a_moduleName, a_prefix, a_code);
            }

        } catch (...) {
            // Diagnostics must never turn a recoverable failure into another failure.
        }
    }

}
