#include "pch.h"
#include "ModuleLogManager.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

namespace HolyFramework
{
    ModuleLogManager& ModuleLogManager::GetSingleton() noexcept
    {
        static ModuleLogManager* instance = new ModuleLogManager();
        return *instance;
    }

    std::string ModuleLogManager::SanitizeFileName(const std::string_view a_name)
    {
        // ModuleLoader requires the runtime name to match the DLL stem, so most
        // characters are already guaranteed to be valid in a Windows filename.
        // Replace only characters that Windows forbids (plus control chars).
        std::string result;
        result.reserve(a_name.size());
        constexpr std::string_view invalid = R"(<>:"/\|?*)";
        for (const unsigned char ch : a_name) {
            if (ch < 0x20 || invalid.find(static_cast<char>(ch)) != std::string_view::npos) {
                result.push_back('_');
            } else {
                result.push_back(static_cast<char>(ch));
            }
        }
        while (!result.empty() && (result.back() == ' ' || result.back() == '.')) {
            result.pop_back();
        }
        if (result.empty()) {
            result = "HolyFrameworkModule";
        }
        return result;
    }

    std::filesystem::path ModuleLogManager::GetF4SELogDirectory()
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

    HF_LogHandle ModuleLogManager::Open(const std::string_view a_moduleName)
    {
        const auto safeName = SanitizeFileName(a_moduleName);

        std::scoped_lock lock{ _lock };
        if (const auto it = _byName.find(safeName); it != _byName.end()) {
            return it->second;
        }

        const auto directory = GetF4SELogDirectory();
        if (directory.empty()) {
            REX::ERROR("Could not resolve F4SE log directory for module '{}'", safeName);
            return HF_INVALID_LOG_HANDLE;
        }

        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        if (ec) {
            REX::ERROR("Could not create F4SE log directory for module '{}': {}", safeName, ec.message());
            return HF_INVALID_LOG_HANDLE;
        }

        const auto path = directory / (safeName + ".log");
        try {
            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
            const auto handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
            auto logger = std::make_shared<spdlog::logger>(
                std::format("HFModule-{}-{}", safeName, handle),
                std::move(sink));
            logger->set_level(spdlog::level::trace);
            logger->flush_on(spdlog::level::info);
            logger->set_pattern("[%T.%e] [%L] %v");

            _entries.emplace(handle, Entry{ safeName, std::move(logger) });
            _byName.emplace(safeName, handle);
            return handle;
        } catch (const std::exception& e) {
            REX::ERROR("Could not create module log '{}': {}", path.string(), e.what());
        } catch (...) {
            REX::ERROR("Could not create module log '{}'", path.string());
        }

        return HF_INVALID_LOG_HANDLE;
    }

    void ModuleLogManager::Write(
        const HF_LogHandle a_handle,
        const HF_LogLevel a_level,
        const std::string_view a_message) noexcept
    {
        std::shared_ptr<spdlog::logger> logger;
        {
            std::scoped_lock lock{ _lock };
            const auto it = _entries.find(a_handle);
            if (it == _entries.end()) {
                return;
            }
            logger = it->second.logger;
        }

        try {
            spdlog::level::level_enum level = spdlog::level::info;
            switch (a_level) {
            case HF_LOG_TRACE:
                level = spdlog::level::trace;
                break;
            case HF_LOG_DEBUG:
                level = spdlog::level::debug;
                break;
            case HF_LOG_WARNING:
                level = spdlog::level::warn;
                break;
            case HF_LOG_ERROR:
                level = spdlog::level::err;
                break;
            case HF_LOG_CRITICAL:
                level = spdlog::level::critical;
                break;
            case HF_LOG_INFO:
            default:
                level = spdlog::level::info;
                break;
            }
            logger->log(level, "{}", a_message);
        } catch (...) {
            // Logging must never destabilize the game.
        }
    }

    void ModuleLogManager::Close(const HF_LogHandle a_handle) noexcept
    {
        if (a_handle == HF_INVALID_LOG_HANDLE) {
            return;
        }

        std::shared_ptr<spdlog::logger> logger;
        {
            std::scoped_lock lock{ _lock };
            const auto it = _entries.find(a_handle);
            if (it == _entries.end()) {
                return;
            }

            logger = it->second.logger;
            const auto name = it->second.name;
            _entries.erase(it);

            const auto byName = _byName.find(name);
            if (byName != _byName.end() && byName->second == a_handle) {
                _byName.erase(byName);
            }
        }

        try {
            if (logger) {
                logger->flush();
            }
        } catch (...) {
        }
    }

    bool ModuleLogManager::IsOwnedBy(
        const HF_LogHandle a_handle,
        const std::string_view a_moduleName) const noexcept
    {
        if (a_handle == HF_INVALID_LOG_HANDLE || a_moduleName.empty()) {
            return false;
        }
        std::scoped_lock lock{ _lock };
        const auto it = _entries.find(a_handle);
        if (it == _entries.end()) {
            return false;
        }
        const auto expected = SanitizeFileName(a_moduleName);
        return it->second.name == expected;
    }

    void ModuleLogManager::Flush(const HF_LogHandle a_handle) noexcept
    {
        std::shared_ptr<spdlog::logger> logger;
        {
            std::scoped_lock lock{ _lock };
            const auto it = _entries.find(a_handle);
            if (it == _entries.end()) {
                return;
            }
            logger = it->second.logger;
        }

        try {
            logger->flush();
        } catch (...) {
        }
    }
}
