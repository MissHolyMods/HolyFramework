#pragma once

namespace HolyFramework
{
    class DisplayService final
    {
    public:
        static DisplayService& GetSingleton() noexcept;

        [[nodiscard]] bool IsAvailable() const noexcept;
        [[nodiscard]] bool GetState(HF_DisplayStateV1& a_outState) const noexcept;

    private:
        DisplayService() = default;

        [[nodiscard]] static bool IsValidRefreshRational(
            std::uint32_t a_numerator,
            std::uint32_t a_denominator) noexcept;
        static void PreferHigherRefresh(
            std::uint32_t a_numerator,
            std::uint32_t a_denominator,
            std::uint32_t& a_inOutNumerator,
            std::uint32_t& a_inOutDenominator) noexcept;
    };
}
