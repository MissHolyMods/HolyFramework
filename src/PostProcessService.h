#pragma once

namespace HolyFramework
{
    class PostProcessService final
    {
    public:
        static PostProcessService& GetSingleton() noexcept;

        HF_PostProcessEffectHandle CreateEffect(
            const HF_PostProcessEffectDescV1& a_desc,
            std::string_view a_moduleName,
            HF_LogHandle a_logger) noexcept;
        bool DestroyEffect(
            HF_PostProcessEffectHandle a_handle,
            std::string_view a_moduleName,
            std::string* a_outActualOwner = nullptr) noexcept;
        bool Draw(
            HF_PostProcessEffectHandle a_handle,
            const HF_RenderStageContextV1& a_context,
            const void* a_constants,
            std::uint32_t a_constantBytes,
            std::string_view a_moduleName) noexcept;
        std::uint32_t DestroyOwnedBy(std::string_view a_moduleName) noexcept;

    private:
        struct Effect
        {
            HF_PostProcessEffectHandle handle{ HF_INVALID_POST_PROCESS_EFFECT_HANDLE };
            HF_RenderStage stage{ HF_RENDER_STAGE_POST_HDR_WORLD };
            std::uint32_t constantBufferSize{ 0 };
            std::string shaderSource;
            std::string entryPoint;
            std::string label;
            std::string owner;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
            REX::W32::ID3D11PixelShader* pixelShader{ nullptr };
            REX::W32::ID3D11Buffer* constantBuffer{ nullptr };
            REX::W32::ID3D11Device* device{ nullptr };
        };

        struct PipelineState
        {
            std::uint32_t viewportCount{
                REX::W32::D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE
            };
            REX::W32::D3D11_VIEWPORT viewports[
                REX::W32::D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
            REX::W32::ID3D11RenderTargetView* renderTarget{ nullptr };
            REX::W32::ID3D11DepthStencilView* depthStencilView{ nullptr };
            REX::W32::ID3D11BlendState* blendState{ nullptr };
            float blendFactor[4]{};
            std::uint32_t sampleMask{ 0 };
            REX::W32::ID3D11DepthStencilState* depthStencilState{ nullptr };
            std::uint32_t stencilRef{ 0 };
            REX::W32::ID3D11RasterizerState* rasterizerState{ nullptr };
            REX::W32::ID3D11InputLayout* inputLayout{ nullptr };
            REX::W32::ID3D11Buffer* vertexBuffer{ nullptr };
            std::uint32_t vertexStride{ 0 };
            std::uint32_t vertexOffset{ 0 };
            REX::W32::ID3D11Buffer* indexBuffer{ nullptr };
            REX::W32::DXGI_FORMAT indexFormat{ REX::W32::DXGI_FORMAT_UNKNOWN };
            std::uint32_t indexOffset{ 0 };
            REX::W32::D3D11_PRIMITIVE_TOPOLOGY topology{
                REX::W32::D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED
            };
            REX::W32::ID3D11VertexShader* vertexShader{ nullptr };
            REX::W32::ID3D11PixelShader* pixelShader{ nullptr };
            REX::W32::ID3D11ShaderResourceView* pixelShaderResource0{ nullptr };
            REX::W32::ID3D11SamplerState* pixelShaderSampler0{ nullptr };
            REX::W32::ID3D11Buffer* pixelShaderConstant0{ nullptr };
            REX::W32::ID3D11GeometryShader* geometryShader{ nullptr };
            REX::W32::ID3D11HullShader* hullShader{ nullptr };
            REX::W32::ID3D11DomainShader* domainShader{ nullptr };
        };

        PostProcessService() = default;

        template <class T>
        static void Release(T*& a_object) noexcept
        {
            if (a_object) {
                a_object->Release();
                a_object = nullptr;
            }
        }

        [[nodiscard]] static bool NamesEqual(
            std::string_view a_left,
            std::string_view a_right) noexcept;
        [[nodiscard]] static bool ValidStage(HF_RenderStage a_stage) noexcept;
        [[nodiscard]] static bool CompileShader(
            const std::string& a_source,
            const char* a_name,
            const char* a_entryPoint,
            const char* a_target,
            REX::W32::ID3DBlob** a_outBytecode) noexcept;

        bool EnsureSharedResources(REX::W32::ID3D11Device* a_device) noexcept;
        bool EnsureEffectResources(Effect& a_effect, REX::W32::ID3D11Device* a_device) noexcept;
        void ReleaseEffectResources(Effect& a_effect) noexcept;
        void ReleaseSharedResources() noexcept;
        void ReleaseSceneCopy() noexcept;
        void CaptureState(REX::W32::ID3D11DeviceContext* a_context, PipelineState& a_state) noexcept;
        void RestoreState(REX::W32::ID3D11DeviceContext* a_context, PipelineState& a_state) noexcept;
        [[nodiscard]] bool GetRenderTargetTexture(
            REX::W32::ID3D11RenderTargetView* a_renderTarget,
            REX::W32::ID3D11Texture2D** a_texture,
            REX::W32::D3D11_TEXTURE2D_DESC& a_description) noexcept;
        [[nodiscard]] bool EnsureSceneCopy(
            REX::W32::ID3D11Device* a_device,
            const REX::W32::D3D11_TEXTURE2D_DESC& a_sourceDescription) noexcept;

        std::mutex _lock;
        std::vector<Effect> _effects;
        std::atomic<std::uint64_t> _nextHandle{ 1 };

        REX::W32::ID3D11Device* _sharedDevice{ nullptr };
        REX::W32::ID3D11VertexShader* _vertexShader{ nullptr };
        REX::W32::ID3D11SamplerState* _samplerState{ nullptr };
        REX::W32::ID3D11BlendState* _blendState{ nullptr };
        REX::W32::ID3D11DepthStencilState* _depthStencilState{ nullptr };
        REX::W32::ID3D11RasterizerState* _rasterizerState{ nullptr };
        REX::W32::ID3D11Texture2D* _sceneCopy{ nullptr };
        REX::W32::ID3D11ShaderResourceView* _sceneCopyView{ nullptr };
        REX::W32::D3D11_TEXTURE2D_DESC _sceneCopyDescription{};
    };
}
