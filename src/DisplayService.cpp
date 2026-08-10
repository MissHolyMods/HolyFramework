#include "pch.h"

#include "DisplayService.h"

#include "GraphicsService.h"

namespace HolyFramework
{
    DisplayService& DisplayService::GetSingleton() noexcept
    {
        static DisplayService* instance = new DisplayService();
        return *instance;
    }

    bool DisplayService::IsValidRefreshRational(
        const std::uint32_t a_numerator,
        const std::uint32_t a_denominator) noexcept
    {
        if (a_numerator == 0 || a_denominator == 0) {
            return false;
        }
        const auto hz = static_cast<double>(a_numerator) /
            static_cast<double>(a_denominator);
        return std::isfinite(hz) && hz > 0.0;
    }

    void DisplayService::PreferHigherRefresh(
        const std::uint32_t a_numerator,
        const std::uint32_t a_denominator,
        std::uint32_t& a_inOutNumerator,
        std::uint32_t& a_inOutDenominator) noexcept
    {
        if (!IsValidRefreshRational(a_numerator, a_denominator)) {
            return;
        }
        if (!IsValidRefreshRational(a_inOutNumerator, a_inOutDenominator)) {
            a_inOutNumerator = a_numerator;
            a_inOutDenominator = a_denominator;
            return;
        }

        const auto left = static_cast<std::uint64_t>(a_numerator) *
            static_cast<std::uint64_t>(a_inOutDenominator);
        const auto right = static_cast<std::uint64_t>(a_inOutNumerator) *
            static_cast<std::uint64_t>(a_denominator);
        if (left > right) {
            a_inOutNumerator = a_numerator;
            a_inOutDenominator = a_denominator;
        }
    }

    bool DisplayService::IsAvailable() const noexcept
    {
        HF_DisplayStateV1 state{};
        return GetState(state) &&
            (state.flags & HF_DISPLAY_STATE_OUTPUT_AVAILABLE) != 0;
    }

    bool DisplayService::GetState(HF_DisplayStateV1& a_outState) const noexcept
    {
        a_outState = {};
        a_outState.structSize = sizeof(HF_DisplayStateV1);

        try {
            HF_GraphicsNativeHandlesV1 handles{};
            if (!GraphicsService::GetSingleton().GetNativeHandles(handles) ||
                handles.swapChain == 0) {
                return false;
            }

            auto* const swapChain = reinterpret_cast<REX::W32::IDXGISwapChain*>(
                static_cast<std::uintptr_t>(handles.swapChain));
            if (!swapChain) {
                return false;
            }

            a_outState.flags |= HF_DISPLAY_STATE_AVAILABLE;

            REX::W32::DXGI_SWAP_CHAIN_DESC swapDesc{};
            const auto haveSwapDesc =
                swapChain->GetDesc(std::addressof(swapDesc)) >= 0;
            if (haveSwapDesc) {
                a_outState.format = static_cast<std::uint32_t>(swapDesc.bufferDesc.format);
            }

            REX::W32::IDXGIOutput* output = nullptr;
            if (swapChain->GetContainingOutput(std::addressof(output)) < 0 || !output) {
                HF_GraphicsStateV1 graphics{};
                if (GraphicsService::GetSingleton().GetState(graphics) &&
                    IsValidRefreshRational(
                        graphics.actualRefreshNumerator,
                        graphics.actualRefreshDenominator)) {
                    a_outState.currentRefreshNumerator = graphics.actualRefreshNumerator;
                    a_outState.currentRefreshDenominator = graphics.actualRefreshDenominator;
                    a_outState.flags |= HF_DISPLAY_STATE_CURRENT_REFRESH_VALID;
                } else if (haveSwapDesc &&
                    IsValidRefreshRational(
                        swapDesc.bufferDesc.refreshRate.numerator,
                        swapDesc.bufferDesc.refreshRate.denominator)) {
                    a_outState.currentRefreshNumerator = swapDesc.bufferDesc.refreshRate.numerator;
                    a_outState.currentRefreshDenominator = swapDesc.bufferDesc.refreshRate.denominator;
                    a_outState.flags |= HF_DISPLAY_STATE_CURRENT_REFRESH_VALID;
                }
                return true;
            }

            a_outState.flags |= HF_DISPLAY_STATE_OUTPUT_AVAILABLE;

            REX::W32::DXGI_OUTPUT_DESC outputDesc{};
            const auto haveOutputDesc =
                output->GetDesc(std::addressof(outputDesc)) >= 0;
            if (haveOutputDesc) {
                a_outState.rotation = static_cast<std::uint32_t>(outputDesc.rotation);
                if (outputDesc.attachedToDesktop != 0) {
                    a_outState.flags |= HF_DISPLAY_STATE_ATTACHED_TO_DESKTOP;
                }
                if (outputDesc.monitor) {
                    a_outState.monitorHandle = static_cast<HF_NativeHandle>(
                        reinterpret_cast<std::uintptr_t>(outputDesc.monitor));
                    a_outState.flags |= HF_DISPLAY_STATE_MONITOR_HANDLE_AVAILABLE;
                }

                const auto left = outputDesc.desktopCoordinates.x1;
                const auto top = outputDesc.desktopCoordinates.y1;
                const auto right = outputDesc.desktopCoordinates.x2;
                const auto bottom = outputDesc.desktopCoordinates.y2;
                if (right > left && bottom > top) {
                    a_outState.desktopLeft = left;
                    a_outState.desktopTop = top;
                    a_outState.desktopRight = right;
                    a_outState.desktopBottom = bottom;
                    a_outState.desktopWidth = static_cast<std::uint32_t>(right - left);
                    a_outState.desktopHeight = static_cast<std::uint32_t>(bottom - top);
                    a_outState.flags |= HF_DISPLAY_STATE_DESKTOP_BOUNDS_VALID;
                }
            }

            if ((a_outState.flags & HF_DISPLAY_STATE_CURRENT_REFRESH_VALID) == 0) {
                HF_GraphicsStateV1 graphics{};
                if (GraphicsService::GetSingleton().GetState(graphics) &&
                    IsValidRefreshRational(
                        graphics.actualRefreshNumerator,
                        graphics.actualRefreshDenominator)) {
                    a_outState.currentRefreshNumerator = graphics.actualRefreshNumerator;
                    a_outState.currentRefreshDenominator = graphics.actualRefreshDenominator;
                    a_outState.flags |= HF_DISPLAY_STATE_CURRENT_REFRESH_VALID;
                } else if (haveSwapDesc &&
                    IsValidRefreshRational(
                        swapDesc.bufferDesc.refreshRate.numerator,
                        swapDesc.bufferDesc.refreshRate.denominator)) {
                    a_outState.currentRefreshNumerator = swapDesc.bufferDesc.refreshRate.numerator;
                    a_outState.currentRefreshDenominator = swapDesc.bufferDesc.refreshRate.denominator;
                    a_outState.flags |= HF_DISPLAY_STATE_CURRENT_REFRESH_VALID;
                }
            }

            if (haveOutputDesc &&
                (a_outState.flags & HF_DISPLAY_STATE_DESKTOP_BOUNDS_VALID) != 0) {
                const auto scanModes = [&](const REX::W32::DXGI_FORMAT a_format) {
                    std::uint32_t modeCount = 0;
                    if (output->GetDisplayModeList(
                            a_format,
                            0,
                            std::addressof(modeCount),
                            nullptr) < 0 ||
                        modeCount == 0) {
                        return;
                    }

                    std::unique_ptr<REX::W32::DXGI_MODE_DESC[]> modes{
                        new (std::nothrow) REX::W32::DXGI_MODE_DESC[modeCount]
                    };
                    if (!modes ||
                        output->GetDisplayModeList(
                            a_format,
                            0,
                            std::addressof(modeCount),
                            modes.get()) < 0) {
                        return;
                    }

                    const auto quarterTurn =
                        a_outState.rotation == 2u || a_outState.rotation == 4u;
                    for (std::uint32_t index = 0; index < modeCount; ++index) {
                        const auto& mode = modes[index];
                        const auto directMatch =
                            mode.width == a_outState.desktopWidth &&
                            mode.height == a_outState.desktopHeight;
                        const auto rotatedMatch =
                            quarterTurn &&
                            mode.width == a_outState.desktopHeight &&
                            mode.height == a_outState.desktopWidth;
                        if (!directMatch && !rotatedMatch) {
                            continue;
                        }
                        PreferHigherRefresh(
                            mode.refreshRate.numerator,
                            mode.refreshRate.denominator,
                            a_outState.maxRefreshNumerator,
                            a_outState.maxRefreshDenominator);
                    }
                };

                const auto primaryFormat = haveSwapDesc ?
                    swapDesc.bufferDesc.format :
                    REX::W32::DXGI_FORMAT_R8G8B8A8_UNORM;
                scanModes(primaryFormat);

                // Some flip-model chains use a format for which the output mode
                // list is empty. Fall back to the desktop-standard UNORM list
                // before declaring the maximum refresh unavailable.
                if (!IsValidRefreshRational(
                        a_outState.maxRefreshNumerator,
                        a_outState.maxRefreshDenominator) &&
                    primaryFormat != REX::W32::DXGI_FORMAT_R8G8B8A8_UNORM) {
                    scanModes(REX::W32::DXGI_FORMAT_R8G8B8A8_UNORM);
                }
            }

            if (IsValidRefreshRational(
                    a_outState.maxRefreshNumerator,
                    a_outState.maxRefreshDenominator)) {
                a_outState.flags |= HF_DISPLAY_STATE_MAX_REFRESH_VALID;
            }

            output->Release();
            return true;
        } catch (...) {
            a_outState = {};
            a_outState.structSize = sizeof(HF_DisplayStateV1);
            return false;
        }
    }
}
