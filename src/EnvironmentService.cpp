#include "pch.h"
#include "EnvironmentService.h"

#include "FormService.h"

namespace HolyFramework
{
    namespace
    {
        constexpr auto kWeatherFlagsIndex =
            static_cast<std::size_t>(std::to_underlying(RE::TESWeather::WeatherData::kFlags));

        [[nodiscard]] float FiniteOrZero(const float a_value) noexcept
        {
            return std::isfinite(a_value) ? a_value : 0.0F;
        }
    }

    EnvironmentService& EnvironmentService::GetSingleton() noexcept
    {
        static EnvironmentService* instance = new EnvironmentService();
        return *instance;
    }

    std::uint32_t EnvironmentService::GetWeatherFlags(const RE::TESWeather* const a_weather) noexcept
    {
        if (!a_weather) {
            return HF_WEATHER_NONE;
        }

        const auto raw = static_cast<std::uint8_t>(a_weather->weatherData[kWeatherFlagsIndex]);
        std::uint32_t flags = HF_WEATHER_NONE;
        if ((raw & 0x01u) != 0) flags |= HF_WEATHER_PLEASANT;
        if ((raw & 0x02u) != 0) flags |= HF_WEATHER_CLOUDY;
        if ((raw & 0x04u) != 0) flags |= HF_WEATHER_RAINY;
        if ((raw & 0x08u) != 0) flags |= HF_WEATHER_SNOW;
        if ((raw & 0x10u) != 0) flags |= HF_WEATHER_PERMANENT_AURORA;
        if ((raw & 0x20u) != 0) flags |= HF_WEATHER_AURORA_FOLLOWS_SUN;
        if ((raw & 0x40u) != 0) flags |= HF_WEATHER_RAIN_OCCLUSION;
        if ((raw & 0x80u) != 0) flags |= HF_WEATHER_HUD_RAIN;
        if (a_weather->precipitationData != nullptr) flags |= HF_WEATHER_HAS_PRECIPITATION_DATA;
        return flags;
    }

    bool EnvironmentService::IsAvailable() const noexcept
    {
        try {
            return RE::Sky::GetSingleton() != nullptr;
        } catch (...) {
            return false;
        }
    }

    bool EnvironmentService::GetState(HF_EnvironmentStateV1& a_outState) const noexcept
    {
        a_outState = {};
        a_outState.structSize = sizeof(HF_EnvironmentStateV1);

        try {
            const auto* const sky = RE::Sky::GetSingleton();
            if (!sky) {
                return false;
            }

            a_outState.flags |= HF_ENVIRONMENT_STATE_AVAILABLE;
            a_outState.skyMode = static_cast<std::uint32_t>(sky->mode.underlying());
            a_outState.currentWeatherPercent = FiniteOrZero(sky->currentWeatherPct);
            a_outState.lightingTransition = FiniteOrZero(sky->lightingTransition);
            a_outState.windSpeed = FiniteOrZero(sky->windSpeed);
            a_outState.windAngle = FiniteOrZero(sky->windAngle);
            a_outState.windTurbulence = FiniteOrZero(sky->windTurbulence);
            a_outState.fogNear = FiniteOrZero(sky->fogDistances[0]);
            a_outState.fogFar = FiniteOrZero(sky->fogDistances[1]);
            a_outState.fogHeight = FiniteOrZero(sky->fogHeight);
            a_outState.fogPower = FiniteOrZero(sky->fogPower);
            a_outState.fogClamp = FiniteOrZero(sky->fogClamp);
            a_outState.fogHighDensityScale = FiniteOrZero(sky->fogHighDensityScale);

            a_outState.currentWeather = FormService::MakeHandle(sky->currentWeather);
            a_outState.lastWeather = FormService::MakeHandle(sky->lastWeather);
            a_outState.defaultWeather = FormService::MakeHandle(sky->defaultWeather);
            a_outState.overrideWeather = FormService::MakeHandle(sky->overrideWeather);
            a_outState.currentClimate = FormService::MakeHandle(sky->currentClimate);
            a_outState.currentRegion = FormService::MakeHandle(sky->currentRegion);

            if (a_outState.currentWeather != HF_INVALID_FORM_HANDLE) {
                a_outState.flags |= HF_ENVIRONMENT_STATE_CURRENT_WEATHER_AVAILABLE;
                a_outState.currentWeatherFlags = GetWeatherFlags(sky->currentWeather);
            }
            if (a_outState.lastWeather != HF_INVALID_FORM_HANDLE) {
                a_outState.flags |= HF_ENVIRONMENT_STATE_LAST_WEATHER_AVAILABLE;
            }
            if (a_outState.defaultWeather != HF_INVALID_FORM_HANDLE) {
                a_outState.flags |= HF_ENVIRONMENT_STATE_DEFAULT_WEATHER_AVAILABLE;
            }
            if (a_outState.overrideWeather != HF_INVALID_FORM_HANDLE) {
                a_outState.flags |= HF_ENVIRONMENT_STATE_OVERRIDE_WEATHER_AVAILABLE;
            }
            if (a_outState.currentClimate != HF_INVALID_FORM_HANDLE) {
                a_outState.flags |= HF_ENVIRONMENT_STATE_CLIMATE_AVAILABLE;
            }
            if (a_outState.currentRegion != HF_INVALID_FORM_HANDLE) {
                a_outState.flags |= HF_ENVIRONMENT_STATE_REGION_AVAILABLE;
            }
            if (sky->precip != nullptr) {
                a_outState.flags |= HF_ENVIRONMENT_STATE_PRECIPITATION_ACTIVE;
            }
            if (a_outState.currentWeatherPercent > 0.001F && a_outState.currentWeatherPercent < 0.999F) {
                a_outState.flags |= HF_ENVIRONMENT_STATE_TRANSITIONING;
            }

            switch (sky->mode.get()) {
            case RE::Sky::Mode::kInterior:
                a_outState.flags |= HF_ENVIRONMENT_STATE_INTERIOR;
                break;
            case RE::Sky::Mode::kSkyDomeOnly:
                a_outState.flags |= HF_ENVIRONMENT_STATE_SKY_DOME_ONLY;
                break;
            case RE::Sky::Mode::kFull:
                a_outState.flags |= HF_ENVIRONMENT_STATE_FULL_SKY;
                break;
            default:
                break;
            }

            return true;
        } catch (...) {
            a_outState = {};
            a_outState.structSize = sizeof(HF_EnvironmentStateV1);
            return false;
        }
    }

    bool EnvironmentService::GetWeatherInfo(
        const HF_FormHandle a_weather,
        HF_WeatherInfoV1& a_outInfo) const noexcept
    {
        a_outInfo = {};
        a_outInfo.structSize = sizeof(HF_WeatherInfoV1);

        try {
            auto* const form = FormService::ResolveHandle(a_weather);
            auto* const weather = form ? form->As<RE::TESWeather>() : nullptr;
            if (!weather) {
                return false;
            }
            a_outInfo.flags = GetWeatherFlags(weather);
            a_outInfo.visibilityMultiplier = FiniteOrZero(weather->visibilityMult);
            a_outInfo.volatilityMultiplier = FiniteOrZero(weather->volatilityMult);
            return true;
        } catch (...) {
            a_outInfo = {};
            a_outInfo.structSize = sizeof(HF_WeatherInfoV1);
            return false;
        }
    }
}
