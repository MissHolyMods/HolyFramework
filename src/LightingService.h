#pragma once

namespace HolyFramework
{
    class LightingService final
    {
    public:
        static LightingService& GetSingleton() noexcept;

        [[nodiscard]] bool IsAvailable() const noexcept;
        bool GetState(HF_LightingStateV1& a_outState) const noexcept;
        bool GetLightingTemplateInfo(HF_FormHandle a_template, HF_LightingTemplateInfoV1& a_outInfo) const noexcept;
        bool GetImageSpaceInfo(HF_FormHandle a_imageSpace, HF_ImageSpaceInfoV1& a_outInfo) const noexcept;
        bool GetWeatherImageSpace(
            HF_FormHandle a_weather,
            HF_WeatherTimeSlot a_timeSlot,
            HF_FormHandle& a_outImageSpace) const noexcept;

    private:
        LightingService() = default;
    };
}
