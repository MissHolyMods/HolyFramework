#pragma once

namespace HolyFramework
{
    class GameTimeService final
    {
    public:
        static GameTimeService& GetSingleton() noexcept;

        [[nodiscard]] bool IsAvailable() const noexcept;
        [[nodiscard]] bool GetGameHour(float& a_outHour) const noexcept;
        [[nodiscard]] bool GetTimeScale(float& a_outTimeScale) const noexcept;
        [[nodiscard]] bool GetState(HF_GameTimeStateV1& a_outState) const noexcept;

    private:
        GameTimeService() = default;
    };
}
