#pragma once

namespace HolyFramework
{
    class FrameTimingService final
    {
    public:
        static FrameTimingService& GetSingleton() noexcept;

        [[nodiscard]] bool IsAvailable() noexcept;
        [[nodiscard]] bool GetState(HF_FrameTimingStateV1& a_outState) noexcept;
        [[nodiscard]] bool GetDeltaSeconds(float& a_outDeltaSeconds) noexcept;
        [[nodiscard]] bool GetFramesPerSecond(float& a_outFramesPerSecond) noexcept;

    private:
        FrameTimingService() = default;

        void Resolve() noexcept;
        [[nodiscard]] bool ReadSample(float& a_outDeltaSeconds) noexcept;

        std::once_flag _resolveOnce;
        const float* _frameTimer{ nullptr };
    };
}
