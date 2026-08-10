#pragma once

namespace HolyFramework
{
    class GameStateService final
    {
    public:
        static GameStateService& GetSingleton() noexcept;

        [[nodiscard]] std::uint32_t GetStateFlags() const noexcept;
        [[nodiscard]] bool HasState(std::uint32_t a_requiredFlags) const noexcept;
        [[nodiscard]] bool IsPaused() const noexcept;
        [[nodiscard]] bool IsLoading() const noexcept;
        [[nodiscard]] bool IsInGame() const noexcept;

    private:
        GameStateService() = default;
    };
}
