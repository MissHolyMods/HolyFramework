#include "pch.h"
#include "GraphicsService.h"

namespace HolyFramework
{
    GraphicsService& GraphicsService::GetSingleton() noexcept
    {
        static GraphicsService* instance = new GraphicsService();
        return *instance;
    }

    RE::BSGraphics::RendererWindow* GraphicsService::ResolveWindow(
        RE::BSGraphics::RendererData* const a_data,
        std::uint32_t& a_outIndex) noexcept
    {
        a_outIndex = HF_INVALID_GRAPHICS_WINDOW_INDEX;
        if (!a_data) {
            return nullptr;
        }

        RE::BSGraphics::RendererWindow* current = nullptr;
        try {
            current = RE::BSGraphics::GetCurrentRendererWindow();
        } catch (...) {
            current = nullptr;
        }

        if (current) {
            for (std::uint32_t i = 0; i < 32; ++i) {
                if (current == std::addressof(a_data->renderWindow[i])) {
                    a_outIndex = i;
                    break;
                }
            }
            return current;
        }

        a_outIndex = 0;
        return std::addressof(a_data->renderWindow[0]);
    }

    bool GraphicsService::IsAvailable() const noexcept
    {
        try {
            return RE::BSGraphics::GetRendererData() != nullptr;
        } catch (...) {
            return false;
        }
    }

    bool GraphicsService::GetState(HF_GraphicsStateV1& a_outState) const noexcept
    {
        a_outState = {};
        a_outState.structSize = sizeof(HF_GraphicsStateV1);
        a_outState.currentWindowIndex = HF_INVALID_GRAPHICS_WINDOW_INDEX;

        try {
            auto* const data = RE::BSGraphics::GetRendererData();
            if (!data) {
                return false;
            }

            a_outState.flags |= HF_GRAPHICS_STATE_AVAILABLE;
            if (data->initialized) a_outState.flags |= HF_GRAPHICS_STATE_INITIALIZED;
            if (data->fullScreen != 0) a_outState.flags |= HF_GRAPHICS_STATE_FULLSCREEN;
            if (data->appFullScreen) a_outState.flags |= HF_GRAPHICS_STATE_APP_FULLSCREEN;
            if (data->borderlessWindow) a_outState.flags |= HF_GRAPHICS_STATE_BORDERLESS;
            if (data->vsync) a_outState.flags |= HF_GRAPHICS_STATE_VSYNC_ENABLED;
            if (data->requestWindowSizeChange) a_outState.flags |= HF_GRAPHICS_STATE_WINDOW_SIZE_CHANGE_PENDING;
            if (data->device) a_outState.flags |= HF_GRAPHICS_STATE_DEVICE_AVAILABLE;
            if (data->context) a_outState.flags |= HF_GRAPHICS_STATE_CONTEXT_AVAILABLE;

            a_outState.adapterIndex = data->adapter;
            a_outState.presentInterval = data->presentInterval;
            a_outState.desiredRefreshNumerator = data->desiredRefreshRate.numerator;
            a_outState.desiredRefreshDenominator = data->desiredRefreshRate.denominator;
            a_outState.actualRefreshNumerator = data->actualRefreshRate.numerator;
            a_outState.actualRefreshDenominator = data->actualRefreshRate.denominator;

            auto* const window = ResolveWindow(data, a_outState.currentWindowIndex);
            if (window) {
                a_outState.windowX = window->windowX;
                a_outState.windowY = window->windowY;
                a_outState.windowWidth = window->windowWidth > 0 ?
                    static_cast<std::uint32_t>(window->windowWidth) : 0;
                a_outState.windowHeight = window->windowHeight > 0 ?
                    static_cast<std::uint32_t>(window->windowHeight) : 0;
                if (window->hwnd) a_outState.flags |= HF_GRAPHICS_STATE_WINDOW_AVAILABLE;
                if (window->swapChain) a_outState.flags |= HF_GRAPHICS_STATE_SWAP_CHAIN_AVAILABLE;
            }

            return true;
        } catch (...) {
            a_outState = {};
            a_outState.structSize = sizeof(HF_GraphicsStateV1);
            a_outState.currentWindowIndex = HF_INVALID_GRAPHICS_WINDOW_INDEX;
            return false;
        }
    }

    bool GraphicsService::GetNativeHandles(HF_GraphicsNativeHandlesV1& a_outHandles) const noexcept
    {
        a_outHandles = {};
        a_outHandles.structSize = sizeof(HF_GraphicsNativeHandlesV1);

        try {
            auto* const data = RE::BSGraphics::GetRendererData();
            if (!data) {
                return false;
            }

            std::uint32_t windowIndex = HF_INVALID_GRAPHICS_WINDOW_INDEX;
            auto* const window = ResolveWindow(data, windowIndex);
            if (window) {
                a_outHandles.windowHandle =
                    static_cast<HF_NativeHandle>(reinterpret_cast<std::uintptr_t>(window->hwnd));
                a_outHandles.swapChain =
                    static_cast<HF_NativeHandle>(reinterpret_cast<std::uintptr_t>(window->swapChain));
            }
            a_outHandles.device =
                static_cast<HF_NativeHandle>(reinterpret_cast<std::uintptr_t>(data->device));
            a_outHandles.immediateContext =
                static_cast<HF_NativeHandle>(reinterpret_cast<std::uintptr_t>(data->context));
            return true;
        } catch (...) {
            a_outHandles = {};
            a_outHandles.structSize = sizeof(HF_GraphicsNativeHandlesV1);
            return false;
        }
    }
}
