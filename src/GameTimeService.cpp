#include "pch.h"
#include "GameTimeService.h"

namespace HolyFramework
{
    namespace
    {
        [[nodiscard]] bool ReadUnsignedGlobal(
            RE::TESGlobal* const a_global,
            std::uint32_t& a_outValue) noexcept
        {
            a_outValue = 0;
            if (!a_global) {
                return false;
            }
            const auto value = a_global->GetValue();
            if (!std::isfinite(value) || value < 0.0F) {
                return false;
            }
            const auto bounded = std::min(
                static_cast<double>(value),
                static_cast<double>(std::numeric_limits<std::uint32_t>::max()));
            a_outValue = static_cast<std::uint32_t>(bounded);
            return true;
        }
    }

    GameTimeService& GameTimeService::GetSingleton() noexcept
    {
        static GameTimeService* instance = new GameTimeService();
        return *instance;
    }

    bool GameTimeService::IsAvailable() const noexcept
    {
        try {
            const auto* const calendar = RE::Calendar::GetSingleton();
            return calendar != nullptr && calendar->gameHour != nullptr;
        } catch (...) {
            return false;
        }
    }

    bool GameTimeService::GetGameHour(float& a_outHour) const noexcept
    {
        a_outHour = 0.0F;
        try {
            const auto* const calendar = RE::Calendar::GetSingleton();
            if (!calendar || !calendar->gameHour) {
                return false;
            }
            const auto value = calendar->gameHour->GetValue();
            if (!std::isfinite(value)) {
                return false;
            }
            a_outHour = value;
            return true;
        } catch (...) {
            return false;
        }
    }

    bool GameTimeService::GetTimeScale(float& a_outTimeScale) const noexcept
    {
        a_outTimeScale = 0.0F;
        try {
            const auto* const calendar = RE::Calendar::GetSingleton();
            if (!calendar || !calendar->timeScale) {
                return false;
            }
            const auto value = calendar->timeScale->GetValue();
            if (!std::isfinite(value)) {
                return false;
            }
            a_outTimeScale = value;
            return true;
        } catch (...) {
            return false;
        }
    }

    bool GameTimeService::GetState(HF_GameTimeStateV1& a_outState) const noexcept
    {
        a_outState = {};
        a_outState.structSize = sizeof(HF_GameTimeStateV1);

        try {
            const auto* const calendar = RE::Calendar::GetSingleton();
            if (!calendar) {
                return false;
            }

            a_outState.flags |= HF_GAME_TIME_STATE_AVAILABLE;
            a_outState.midnightsPassed = calendar->midnightsPassed;
            a_outState.rawDaysPassed = std::isfinite(calendar->rawDaysPassed) ? calendar->rawDaysPassed : 0.0F;

            if (calendar->gameHour) {
                const auto value = calendar->gameHour->GetValue();
                if (std::isfinite(value)) {
                    a_outState.gameHour = value;
                    a_outState.flags |= HF_GAME_TIME_STATE_HOUR_AVAILABLE;
                }
            }

            if (ReadUnsignedGlobal(calendar->gameYear, a_outState.year) &&
                ReadUnsignedGlobal(calendar->gameMonth, a_outState.month) &&
                ReadUnsignedGlobal(calendar->gameDay, a_outState.day)) {
                a_outState.flags |= HF_GAME_TIME_STATE_DATE_AVAILABLE;
            }

            if (calendar->gameDaysPassed) {
                const auto value = calendar->gameDaysPassed->GetValue();
                if (std::isfinite(value)) {
                    a_outState.daysPassed = value;
                    a_outState.hoursPassed = value * 24.0F;
                    a_outState.flags |= HF_GAME_TIME_STATE_DAYS_PASSED_AVAILABLE;
                }
            }

            if (calendar->timeScale) {
                const auto value = calendar->timeScale->GetValue();
                if (std::isfinite(value)) {
                    a_outState.timeScale = value;
                    a_outState.flags |= HF_GAME_TIME_STATE_TIME_SCALE_AVAILABLE;
                }
            }

            return true;
        } catch (...) {
            a_outState = {};
            a_outState.structSize = sizeof(HF_GameTimeStateV1);
            return false;
        }
    }
}
