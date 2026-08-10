#pragma once

namespace HolyFramework
{
    struct ModuleExecutionContext
    {
        const char* name{ nullptr };
        HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
        std::uint32_t checkpoint{ 0 };
    };

    class ModuleContext final
    {
    public:
        class Scope final
        {
        public:
            Scope(const char* a_name, HF_LogHandle a_logger, std::uint32_t a_checkpoint = 0) noexcept;
            ~Scope() noexcept;

            Scope(const Scope&) = delete;
            Scope& operator=(const Scope&) = delete;

        private:
            ModuleExecutionContext _previous{};
        };

        [[nodiscard]] static ModuleExecutionContext Current() noexcept;
        static void SetCheckpoint(std::uint32_t a_checkpoint) noexcept;
        static void ClearCheckpoint() noexcept;

    private:
        static void Set(ModuleExecutionContext a_context) noexcept;
    };
}
