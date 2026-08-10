#pragma once

namespace HolyFramework
{
    class GraphicsService
    {
    public:
        static GraphicsService& GetSingleton() noexcept;

        [[nodiscard]] bool IsAvailable() const noexcept;
        [[nodiscard]] bool GetState(HF_GraphicsStateV1& a_outState) const noexcept;
        [[nodiscard]] bool GetNativeHandles(HF_GraphicsNativeHandlesV1& a_outHandles) const noexcept;

    private:
        GraphicsService() = default;

        [[nodiscard]] static RE::BSGraphics::RendererWindow* ResolveWindow(
            RE::BSGraphics::RendererData* a_data,
            std::uint32_t& a_outIndex) noexcept;
    };
}
