#pragma once

#include <memory>
#include <unordered_map>

namespace spdlog
{
    class logger;
}

namespace HolyFramework
{
    class ModuleLogManager final
    {
    public:
        static ModuleLogManager& GetSingleton() noexcept;

        HF_LogHandle Open(std::string_view a_moduleName);
        void Write(HF_LogHandle a_handle, HF_LogLevel a_level, std::string_view a_message) noexcept;
        void Flush(HF_LogHandle a_handle) noexcept;
        void Close(HF_LogHandle a_handle) noexcept;
        [[nodiscard]] bool IsOwnedBy(HF_LogHandle a_handle, std::string_view a_moduleName) const noexcept;

    private:
        struct Entry
        {
            std::string name;
            std::shared_ptr<spdlog::logger> logger;
        };

        ModuleLogManager() = default;

        [[nodiscard]] static std::string SanitizeFileName(std::string_view a_name);
        [[nodiscard]] static std::filesystem::path GetF4SELogDirectory();

        mutable std::mutex _lock;
        std::unordered_map<HF_LogHandle, Entry> _entries;
        std::unordered_map<std::string, HF_LogHandle> _byName;
        std::atomic<HF_LogHandle> _nextHandle{ 1 };
    };
}
