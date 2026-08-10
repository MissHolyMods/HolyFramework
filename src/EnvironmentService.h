#pragma once

namespace HolyFramework
{
    class EnvironmentService final
    {
    public:
        static EnvironmentService& GetSingleton() noexcept;

        [[nodiscard]] bool IsAvailable() const noexcept;
        bool GetState(HF_EnvironmentStateV1& a_outState) const noexcept;
        bool GetWeatherInfo(HF_FormHandle a_weather, HF_WeatherInfoV1& a_outInfo) const noexcept;

    private:
        EnvironmentService() = default;
        [[nodiscard]] static std::uint32_t GetWeatherFlags(const RE::TESWeather* a_weather) noexcept;
    };
}
