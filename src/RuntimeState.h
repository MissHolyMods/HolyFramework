#pragma once

namespace HolyFramework
{
    class RuntimeState final
    {
    public:
        static RuntimeState& GetSingleton() noexcept;

        void SetFrameworkReady(bool a_ready) noexcept;
        void OnEvent(HF_Event a_event) noexcept;
        void MarkSessionReady() noexcept;

        [[nodiscard]] std::uint32_t GetFlags() const noexcept;
        [[nodiscard]] std::uint64_t GetSessionGeneration() const noexcept;
        [[nodiscard]] bool HasState(std::uint32_t a_requiredFlags) const noexcept;

    private:
        RuntimeState() = default;

        void SetFlag(std::uint32_t a_flag, bool a_enabled) noexcept;

        std::atomic_uint32_t _flags{ HF_RUNTIME_STATE_NONE };
        std::atomic_uint64_t _sessionGeneration{ 0 };
    };
}
