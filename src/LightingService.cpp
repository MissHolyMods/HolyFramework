#include "pch.h"
#include "LightingService.h"

#include "FormService.h"

namespace HolyFramework
{
    namespace
    {
        [[nodiscard]] float FiniteOrZero(const float a_value) noexcept
        {
            return std::isfinite(a_value) ? a_value : 0.0F;
        }

        template <class T, std::size_t N>
        void CopyCString(char (&a_dest)[N], const T& a_source) noexcept
        {
            const char* const value = a_source.c_str();
            std::snprintf(a_dest, N, "%s", value ? value : "");
        }
    }

    LightingService& LightingService::GetSingleton() noexcept
    {
        static LightingService* instance = new LightingService();
        return *instance;
    }

    bool LightingService::IsAvailable() const noexcept
    {
        try {
            const auto* const player = RE::PlayerCharacter::GetSingleton();
            return (player && player->GetParentCell()) || RE::Sky::GetSingleton() != nullptr;
        } catch (...) {
            return false;
        }
    }

    bool LightingService::GetState(HF_LightingStateV1& a_outState) const noexcept
    {
        a_outState = {};
        a_outState.structSize = sizeof(HF_LightingStateV1);

        try {
            auto* const player = RE::PlayerCharacter::GetSingleton();
            auto* const cell = player ? player->GetParentCell() : nullptr;
            auto* const sky = RE::Sky::GetSingleton();
            if (!cell && !sky) {
                return false;
            }

            a_outState.flags |= HF_LIGHTING_STATE_AVAILABLE;

            if (cell) {
                a_outState.playerCell = FormService::MakeHandle(cell);
                if (a_outState.playerCell != HF_INVALID_FORM_HANDLE) {
                    a_outState.flags |= HF_LIGHTING_STATE_PLAYER_CELL_AVAILABLE;
                }

                if (cell->IsInterior()) {
                    a_outState.flags |= HF_LIGHTING_STATE_INTERIOR;
                    a_outState.cellLightingTemplate = FormService::MakeHandle(cell->lightingTemplate);
                    if (a_outState.cellLightingTemplate != HF_INVALID_FORM_HANDLE) {
                        a_outState.flags |= HF_LIGHTING_STATE_CELL_TEMPLATE_AVAILABLE;
                    }
                } else {
                    a_outState.flags |= HF_LIGHTING_STATE_EXTERIOR;
                    auto* const worldspace = cell->worldSpace;
                    a_outState.worldspace = FormService::MakeHandle(worldspace);
                    if (a_outState.worldspace != HF_INVALID_FORM_HANDLE) {
                        a_outState.flags |= HF_LIGHTING_STATE_WORLDSPACE_AVAILABLE;
                    }
                    a_outState.worldspaceLightingTemplate =
                        FormService::MakeHandle(worldspace ? worldspace->lightingTemplate : nullptr);
                    if (a_outState.worldspaceLightingTemplate != HF_INVALID_FORM_HANDLE) {
                        a_outState.flags |= HF_LIGHTING_STATE_WORLDSPACE_TEMPLATE_AVAILABLE;
                    }
                }
            }

            if (sky) {
                a_outState.lightingTransition = FiniteOrZero(sky->lightingTransition);
                a_outState.exteriorLightingOverride = FormService::MakeHandle(sky->extLightingOverride);
                a_outState.previousImageSpace = FormService::MakeHandle(sky->prevImageSpace);
                if (a_outState.exteriorLightingOverride != HF_INVALID_FORM_HANDLE) {
                    a_outState.flags |= HF_LIGHTING_STATE_EXTERIOR_OVERRIDE_AVAILABLE;
                }
                if (a_outState.previousImageSpace != HF_INVALID_FORM_HANDLE) {
                    a_outState.flags |= HF_LIGHTING_STATE_PREVIOUS_IMAGE_SPACE_AVAILABLE;
                }
            }

            return true;
        } catch (...) {
            a_outState = {};
            a_outState.structSize = sizeof(HF_LightingStateV1);
            return false;
        }
    }

    bool LightingService::GetLightingTemplateInfo(
        const HF_FormHandle a_template,
        HF_LightingTemplateInfoV1& a_outInfo) const noexcept
    {
        a_outInfo = {};
        a_outInfo.structSize = sizeof(HF_LightingTemplateInfoV1);

        try {
            auto* const form = FormService::ResolveHandle(a_template);
            auto* const lighting = form ? form->As<RE::BGSLightingTemplate>() : nullptr;
            if (!lighting) {
                return false;
            }

            const auto& data = lighting->data;
            a_outInfo.ambientColor = data.ambient;
            a_outInfo.directionalColor = data.directional;
            a_outInfo.fogColorNear = data.fogColorNear;
            a_outInfo.fogColorFar = data.fogColorFar;
            a_outInfo.fogColorHighNear = data.fogColorHighNear;
            a_outInfo.fogColorHighFar = data.fogColorHighFar;
            a_outInfo.directionalXY = data.directionalXY;
            a_outInfo.directionalZ = data.directionalZ;
            a_outInfo.inheritanceFlags = data.lightingTemplateInheritanceFlags;

            for (std::size_t i = 0; i < std::size(a_outInfo.directionalAmbientColors); ++i) {
                a_outInfo.directionalAmbientColors[i] = data.directionalAmbientLightingColors.colorValues[i];
            }
            a_outInfo.fresnelPower = FiniteOrZero(data.directionalAmbientLightingColors.fresnelPower);

            a_outInfo.fogNear = FiniteOrZero(data.fogNear);
            a_outInfo.fogFar = FiniteOrZero(data.fogFar);
            a_outInfo.directionalFade = FiniteOrZero(data.directionalFade);
            a_outInfo.clipDistance = FiniteOrZero(data.clipDist);
            a_outInfo.fogPower = FiniteOrZero(data.fogPower);
            a_outInfo.fogClamp = FiniteOrZero(data.fogClamp);
            a_outInfo.lightFadeStart = FiniteOrZero(data.lightFadeStart);
            a_outInfo.lightFadeEnd = FiniteOrZero(data.lightFadeEnd);
            a_outInfo.fogHeightMid = FiniteOrZero(data.fogHeightMid);
            a_outInfo.fogHeightRange = FiniteOrZero(data.fogHeightRange);
            a_outInfo.fogHighDensityScale = FiniteOrZero(data.fogHighDensityScale);
            a_outInfo.fogNearColorScale = FiniteOrZero(data.fogNearColorScale);
            a_outInfo.fogFarColorScale = FiniteOrZero(data.fogFarColorScale);
            a_outInfo.fogHighNearColorScale = FiniteOrZero(data.fogHighNearColorScale);
            a_outInfo.fogHighFarColorScale = FiniteOrZero(data.fogHighFarColorScale);
            a_outInfo.fogFarHeightMid = FiniteOrZero(data.fogFarHeightMid);
            a_outInfo.fogFarHeightRange = FiniteOrZero(data.fogFarHeightRange);
            return true;
        } catch (...) {
            a_outInfo = {};
            a_outInfo.structSize = sizeof(HF_LightingTemplateInfoV1);
            return false;
        }
    }

    bool LightingService::GetImageSpaceInfo(
        const HF_FormHandle a_imageSpace,
        HF_ImageSpaceInfoV1& a_outInfo) const noexcept
    {
        a_outInfo = {};
        a_outInfo.structSize = sizeof(HF_ImageSpaceInfoV1);

        try {
            auto* const form = FormService::ResolveHandle(a_imageSpace);
            auto* const imageSpace = form ? form->As<RE::TESImageSpace>() : nullptr;
            if (!imageSpace) {
                return false;
            }

            const auto& data = imageSpace->data;
            a_outInfo.eyeAdaptSpeed = FiniteOrZero(data.hdrData.eyeAdaptSpeed);
            a_outInfo.bloomBlurRadius = FiniteOrZero(data.hdrData.bloomBlurRadius);
            a_outInfo.bloomThreshold = FiniteOrZero(data.hdrData.bloomThreshold);
            a_outInfo.bloomScale = FiniteOrZero(data.hdrData.bloomScale);
            a_outInfo.receiveBloomThreshold = FiniteOrZero(data.hdrData.receiveBloomThreshold);
            a_outInfo.whitePoint = FiniteOrZero(data.hdrData.white);
            a_outInfo.sunlightScale = FiniteOrZero(data.hdrData.sunlightScale);
            a_outInfo.skyScale = FiniteOrZero(data.hdrData.skyScale);
            a_outInfo.eyeAdaptStrength = FiniteOrZero(data.hdrData.eyeAdaptStrength);

            a_outInfo.saturation = FiniteOrZero(data.cinematicData.saturation);
            a_outInfo.brightness = FiniteOrZero(data.cinematicData.brightness);
            a_outInfo.contrast = FiniteOrZero(data.cinematicData.contrast);

            a_outInfo.tintAmount = FiniteOrZero(data.tintData.amount);
            a_outInfo.tintRed = FiniteOrZero(data.tintData.color.red);
            a_outInfo.tintGreen = FiniteOrZero(data.tintData.color.green);
            a_outInfo.tintBlue = FiniteOrZero(data.tintData.color.blue);

            a_outInfo.dofStrength = FiniteOrZero(data.dofData.strength);
            a_outInfo.dofDistance = FiniteOrZero(data.dofData.distance);
            a_outInfo.dofRange = FiniteOrZero(data.dofData.range);
            a_outInfo.vignetteRadius = FiniteOrZero(data.dofData.vignetteRadius);
            a_outInfo.vignetteStrength = FiniteOrZero(data.dofData.vignetteStrength);
            a_outInfo.dofMode = FiniteOrZero(data.dofData.mode);

            CopyCString(a_outInfo.lutTexture, imageSpace->lutTexture.textureName);
            if (a_outInfo.lutTexture[0] != '\0') {
                a_outInfo.flags |= HF_IMAGE_SPACE_HAS_LUT_TEXTURE;
            }
            return true;
        } catch (...) {
            a_outInfo = {};
            a_outInfo.structSize = sizeof(HF_ImageSpaceInfoV1);
            return false;
        }
    }

    bool LightingService::GetWeatherImageSpace(
        const HF_FormHandle a_weather,
        const HF_WeatherTimeSlot a_timeSlot,
        HF_FormHandle& a_outImageSpace) const noexcept
    {
        a_outImageSpace = HF_INVALID_FORM_HANDLE;
        const auto slot = static_cast<std::uint32_t>(a_timeSlot);
        if (slot >= static_cast<std::uint32_t>(HF_WEATHER_TIME_COUNT)) {
            return false;
        }

        try {
            auto* const form = FormService::ResolveHandle(a_weather);
            auto* const weather = form ? form->As<RE::TESWeather>() : nullptr;
            if (!weather) {
                return false;
            }
            a_outImageSpace = FormService::MakeHandle(weather->imageSpace[slot]);
            return a_outImageSpace != HF_INVALID_FORM_HANDLE;
        } catch (...) {
            a_outImageSpace = HF_INVALID_FORM_HANDLE;
            return false;
        }
    }
}
