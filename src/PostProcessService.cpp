#include "pch.h"
#include "PostProcessService.h"

#include "Diagnostics.h"

namespace HolyFramework
{
    namespace
    {
        using namespace REX::W32;

        constexpr char kVertexShaderSource[] = R"(
struct HF_PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

HF_PSInput VSMain(uint vertexID : SV_VertexID)
{
    HF_PSInput output;
    const float2 texcoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(
        texcoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f),
        0.0f,
        1.0f);
    output.texcoord = texcoord;
    return output;
}
)";

        constexpr char kPixelShaderPrelude[] = R"(
struct HF_PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D<float4> SceneColor : register(t0);
SamplerState LinearClamp : register(s0);
)";
    }

    PostProcessService& PostProcessService::GetSingleton() noexcept
    {
        static PostProcessService* instance = new PostProcessService();
        return *instance;
    }

    bool PostProcessService::NamesEqual(
        const std::string_view a_left,
        const std::string_view a_right) noexcept
    {
        if (a_left.size() != a_right.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a_left.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a_left[i])) !=
                std::tolower(static_cast<unsigned char>(a_right[i]))) {
                return false;
            }
        }
        return true;
    }

    bool PostProcessService::ValidStage(const HF_RenderStage a_stage) noexcept
    {
        return a_stage == HF_RENDER_STAGE_POST_HDR_WORLD;
    }

    bool PostProcessService::CompileShader(
        const std::string& a_source,
        const char* const a_name,
        const char* const a_entryPoint,
        const char* const a_target,
        ID3DBlob** const a_outBytecode) noexcept
    {
        if (!a_outBytecode || !a_entryPoint || !*a_entryPoint ||
            !a_target || !*a_target || a_source.empty()) {
            return false;
        }

        *a_outBytecode = nullptr;
        ID3DBlob* errors = nullptr;
        const auto result = D3DCompile(
            a_source.data(),
            a_source.size(),
            a_name && *a_name ? a_name : "HolyFrameworkPostProcess",
            nullptr,
            nullptr,
            a_entryPoint,
            a_target,
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0,
            a_outBytecode,
            &errors);

        if (result < 0) {
            Release(errors);
            return false;
        }

        Release(errors);
        return true;
    }

    void PostProcessService::ReleaseEffectResources(Effect& a_effect) noexcept
    {
        Release(a_effect.constantBuffer);
        Release(a_effect.pixelShader);
        a_effect.device = nullptr;
    }

    void PostProcessService::ReleaseSceneCopy() noexcept
    {
        Release(_sceneCopyView);
        Release(_sceneCopy);
        _sceneCopyDescription = {};
    }

    void PostProcessService::ReleaseSharedResources() noexcept
    {
        ReleaseSceneCopy();
        Release(_rasterizerState);
        Release(_depthStencilState);
        Release(_blendState);
        Release(_samplerState);
        Release(_vertexShader);
        _sharedDevice = nullptr;

        for (auto& effect : _effects) {
            ReleaseEffectResources(effect);
        }
    }

    bool PostProcessService::EnsureSharedResources(ID3D11Device* const a_device) noexcept
    {
        if (!a_device) {
            return false;
        }
        if (_sharedDevice == a_device &&
            _vertexShader &&
            _samplerState &&
            _blendState &&
            _depthStencilState &&
            _rasterizerState) {
            return true;
        }

        ReleaseSharedResources();

        ID3DBlob* vertexBytecode = nullptr;
        if (!CompileShader(
                kVertexShaderSource,
                "HolyFrameworkFullscreenTriangle",
                "VSMain",
                "vs_5_0",
                &vertexBytecode)) {
            Diagnostics::ReportFrameworkFailureForModule(
                "HolyFramework",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_POST_PROCESS_SHADER_COMPILE_FAILED);
            return false;
        }

        if (a_device->CreateVertexShader(
                vertexBytecode->GetBufferPointer(),
                vertexBytecode->GetBufferSize(),
                nullptr,
                &_vertexShader) < 0) {
            Release(vertexBytecode);
            Diagnostics::ReportFrameworkFailureForModule(
                "HolyFramework",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_POST_PROCESS_RESOURCE_FAILED);
            ReleaseSharedResources();
            return false;
        }
        Release(vertexBytecode);

        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDescription.addressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.addressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.addressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.maxLOD = D3D11_FLOAT32_MAX;
        if (a_device->CreateSamplerState(
                &samplerDescription,
                &_samplerState) < 0) {
            ReleaseSharedResources();
            return false;
        }

        D3D11_BLEND_DESC blendDescription{};
        blendDescription.renderTarget[0].blendEnable = 0;
        blendDescription.renderTarget[0].renderTargetWriteMask =
            D3D11_COLOR_WRITE_ENABLE_ALL;
        if (a_device->CreateBlendState(
                &blendDescription,
                &_blendState) < 0) {
            ReleaseSharedResources();
            return false;
        }

        D3D11_DEPTH_STENCIL_DESC depthDescription{};
        depthDescription.depthEnable = 0;
        depthDescription.depthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depthDescription.depthFunc = D3D11_COMPARISON_ALWAYS;
        depthDescription.stencilEnable = 0;
        if (a_device->CreateDepthStencilState(
                &depthDescription,
                &_depthStencilState) < 0) {
            ReleaseSharedResources();
            return false;
        }

        D3D11_RASTERIZER_DESC rasterDescription{};
        rasterDescription.fillMode = D3D11_FILL_SOLID;
        rasterDescription.cullMode = D3D11_CULL_NONE;
        rasterDescription.depthClipEnable = 1;
        if (a_device->CreateRasterizerState(
                &rasterDescription,
                &_rasterizerState) < 0) {
            ReleaseSharedResources();
            return false;
        }

        _sharedDevice = a_device;
        return true;
    }

    bool PostProcessService::EnsureEffectResources(
        Effect& a_effect,
        ID3D11Device* const a_device) noexcept
    {
        if (a_effect.device == a_device &&
            a_effect.pixelShader &&
            (a_effect.constantBufferSize == 0 || a_effect.constantBuffer)) {
            return true;
        }

        ReleaseEffectResources(a_effect);

        std::string source;
        try {
            source.reserve(
                std::char_traits<char>::length(kPixelShaderPrelude) +
                a_effect.shaderSource.size() + 2);
            source.append(kPixelShaderPrelude);
            source.push_back('\n');
            source.append(a_effect.shaderSource);
        } catch (...) {
            return false;
        }

        ID3DBlob* pixelBytecode = nullptr;
        if (!CompileShader(
                source,
                a_effect.label.c_str(),
                a_effect.entryPoint.c_str(),
                "ps_5_0",
                &pixelBytecode)) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_effect.owner,
                a_effect.logger,
                HF_ERROR_POST_PROCESS_SHADER_COMPILE_FAILED);
            return false;
        }

        if (a_device->CreatePixelShader(
                pixelBytecode->GetBufferPointer(),
                pixelBytecode->GetBufferSize(),
                nullptr,
                &a_effect.pixelShader) < 0) {
            Release(pixelBytecode);
            Diagnostics::ReportFrameworkFailureForModule(
                a_effect.owner,
                a_effect.logger,
                HF_ERROR_POST_PROCESS_RESOURCE_FAILED);
            return false;
        }
        Release(pixelBytecode);

        if (a_effect.constantBufferSize > 0) {
            D3D11_BUFFER_DESC bufferDescription{};
            bufferDescription.byteWidth = a_effect.constantBufferSize;
            bufferDescription.usage = D3D11_USAGE_DEFAULT;
            bufferDescription.bindFlags = D3D11_BIND_CONSTANT_BUFFER;
            if (a_device->CreateBuffer(
                    &bufferDescription,
                    nullptr,
                    &a_effect.constantBuffer) < 0) {
                ReleaseEffectResources(a_effect);
                Diagnostics::ReportFrameworkFailureForModule(
                    a_effect.owner,
                    a_effect.logger,
                    HF_ERROR_POST_PROCESS_RESOURCE_FAILED);
                return false;
            }
        }

        a_effect.device = a_device;
        return true;
    }

    HF_PostProcessEffectHandle PostProcessService::CreateEffect(
        const HF_PostProcessEffectDescV1& a_desc,
        const std::string_view a_moduleName,
        const HF_LogHandle a_logger) noexcept
    {
        const auto sourceLength = a_desc.pixelShaderSource ?
            std::strlen(a_desc.pixelShaderSource) : 0;
        const auto entry = a_desc.pixelShaderEntryPoint &&
            *a_desc.pixelShaderEntryPoint ?
            a_desc.pixelShaderEntryPoint :
            "PSMain";
        const auto label = a_desc.label && *a_desc.label ?
            a_desc.label :
            "post-process";

        if (a_moduleName.empty() ||
            a_desc.structSize < sizeof(HF_PostProcessEffectDescV1) ||
            !ValidStage(a_desc.stage) ||
            sourceLength == 0 ||
            sourceLength > HF_POST_PROCESS_MAX_SHADER_SOURCE_BYTES ||
            a_desc.constantBufferSize > HF_POST_PROCESS_MAX_CONSTANT_BYTES ||
            (a_desc.constantBufferSize % 16u) != 0u) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName.empty() ? "<unknown>" : a_moduleName,
                a_logger,
                HF_ERROR_POST_PROCESS_INVALID_REQUEST);
            return HF_INVALID_POST_PROCESS_EFFECT_HANDLE;
        }

        try {
            auto handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
            if (handle == HF_INVALID_POST_PROCESS_EFFECT_HANDLE) {
                handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
            }

            Effect effect{};
            effect.handle = handle;
            effect.stage = a_desc.stage;
            effect.constantBufferSize = a_desc.constantBufferSize;
            effect.shaderSource.assign(
                a_desc.pixelShaderSource,
                sourceLength);
            effect.entryPoint = entry;
            effect.label = label;
            effect.owner = std::string{ a_moduleName };
            effect.logger = a_logger;

            std::scoped_lock lock{ _lock };
            _effects.push_back(std::move(effect));
            return handle;
        } catch (...) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName,
                a_logger,
                HF_ERROR_POST_PROCESS_CREATE_FAILED);
            return HF_INVALID_POST_PROCESS_EFFECT_HANDLE;
        }
    }

    bool PostProcessService::DestroyEffect(
        const HF_PostProcessEffectHandle a_handle,
        const std::string_view a_moduleName,
        std::string* const a_outActualOwner) noexcept
    {
        if (a_handle == HF_INVALID_POST_PROCESS_EFFECT_HANDLE ||
            a_moduleName.empty()) {
            return false;
        }

        std::scoped_lock lock{ _lock };
        const auto it = std::ranges::find_if(
            _effects,
            [a_handle](const Effect& a_effect) {
                return a_effect.handle == a_handle;
            });
        if (it == _effects.end()) {
            return false;
        }
        if (!NamesEqual(it->owner, a_moduleName)) {
            if (a_outActualOwner) {
                *a_outActualOwner = it->owner;
            }
            return false;
        }

        ReleaseEffectResources(*it);
        _effects.erase(it);
        return true;
    }

    std::uint32_t PostProcessService::DestroyOwnedBy(
        const std::string_view a_moduleName) noexcept
    {
        if (a_moduleName.empty()) {
            return 0;
        }

        std::uint32_t count = 0;
        std::scoped_lock lock{ _lock };
        for (std::size_t i = _effects.size(); i > 0; --i) {
            auto& effect = _effects[i - 1];
            if (!NamesEqual(effect.owner, a_moduleName)) {
                continue;
            }
            ReleaseEffectResources(effect);
            _effects.erase(
                _effects.begin() + static_cast<std::ptrdiff_t>(i - 1));
            ++count;
        }
        return count;
    }

    void PostProcessService::CaptureState(
        ID3D11DeviceContext* const a_context,
        PipelineState& a_state) noexcept
    {
        a_context->RSGetViewports(
            &a_state.viewportCount,
            a_state.viewports);
        a_context->OMGetRenderTargets(
            1,
            &a_state.renderTarget,
            &a_state.depthStencilView);
        a_context->OMGetBlendState(
            &a_state.blendState,
            a_state.blendFactor,
            &a_state.sampleMask);
        a_context->OMGetDepthStencilState(
            &a_state.depthStencilState,
            &a_state.stencilRef);
        a_context->RSGetState(&a_state.rasterizerState);
        a_context->IAGetInputLayout(&a_state.inputLayout);
        a_context->IAGetVertexBuffers(
            0,
            1,
            &a_state.vertexBuffer,
            &a_state.vertexStride,
            &a_state.vertexOffset);
        a_context->IAGetIndexBuffer(
            &a_state.indexBuffer,
            &a_state.indexFormat,
            &a_state.indexOffset);
        a_context->IAGetPrimitiveTopology(&a_state.topology);
        a_context->VSGetShader(&a_state.vertexShader, nullptr, nullptr);
        a_context->PSGetShader(&a_state.pixelShader, nullptr, nullptr);
        a_context->PSGetShaderResources(
            0,
            1,
            &a_state.pixelShaderResource0);
        a_context->PSGetSamplers(
            0,
            1,
            &a_state.pixelShaderSampler0);
        a_context->PSGetConstantBuffers(
            0,
            1,
            &a_state.pixelShaderConstant0);
        a_context->GSGetShader(&a_state.geometryShader, nullptr, nullptr);
        a_context->HSGetShader(&a_state.hullShader, nullptr, nullptr);
        a_context->DSGetShader(&a_state.domainShader, nullptr, nullptr);
    }

    void PostProcessService::RestoreState(
        ID3D11DeviceContext* const a_context,
        PipelineState& a_state) noexcept
    {
        ID3D11ShaderResourceView* nullResource = nullptr;
        a_context->PSSetShaderResources(0, 1, &nullResource);
        a_context->OMSetRenderTargets(
            1,
            &a_state.renderTarget,
            a_state.depthStencilView);
        a_context->VSSetShader(a_state.vertexShader, nullptr, 0);
        a_context->PSSetShader(a_state.pixelShader, nullptr, 0);
        a_context->PSSetShaderResources(
            0,
            1,
            &a_state.pixelShaderResource0);
        a_context->PSSetSamplers(
            0,
            1,
            &a_state.pixelShaderSampler0);
        a_context->PSSetConstantBuffers(
            0,
            1,
            &a_state.pixelShaderConstant0);
        a_context->GSSetShader(a_state.geometryShader, nullptr, 0);
        a_context->HSSetShader(a_state.hullShader, nullptr, 0);
        a_context->DSSetShader(a_state.domainShader, nullptr, 0);
        a_context->IASetInputLayout(a_state.inputLayout);
        a_context->IASetVertexBuffers(
            0,
            1,
            &a_state.vertexBuffer,
            &a_state.vertexStride,
            &a_state.vertexOffset);
        a_context->IASetIndexBuffer(
            a_state.indexBuffer,
            a_state.indexFormat,
            a_state.indexOffset);
        a_context->IASetPrimitiveTopology(a_state.topology);
        a_context->RSSetState(a_state.rasterizerState);
        a_context->RSSetViewports(
            a_state.viewportCount,
            a_state.viewportCount > 0 ? a_state.viewports : nullptr);
        a_context->OMSetDepthStencilState(
            a_state.depthStencilState,
            a_state.stencilRef);
        a_context->OMSetBlendState(
            a_state.blendState,
            a_state.blendFactor,
            a_state.sampleMask);

        Release(a_state.domainShader);
        Release(a_state.hullShader);
        Release(a_state.geometryShader);
        Release(a_state.pixelShaderConstant0);
        Release(a_state.pixelShaderSampler0);
        Release(a_state.pixelShaderResource0);
        Release(a_state.pixelShader);
        Release(a_state.vertexShader);
        Release(a_state.indexBuffer);
        Release(a_state.vertexBuffer);
        Release(a_state.inputLayout);
        Release(a_state.rasterizerState);
        Release(a_state.depthStencilState);
        Release(a_state.blendState);
        Release(a_state.depthStencilView);
        Release(a_state.renderTarget);
    }

    bool PostProcessService::GetRenderTargetTexture(
        ID3D11RenderTargetView* const a_renderTarget,
        ID3D11Texture2D** const a_texture,
        D3D11_TEXTURE2D_DESC& a_description) noexcept
    {
        if (!a_renderTarget || !a_texture) {
            return false;
        }

        *a_texture = nullptr;
        ID3D11Resource* resource = nullptr;
        a_renderTarget->GetResource(&resource);
        if (!resource) {
            return false;
        }

        ID3D11Texture2D* texture = nullptr;
        const auto queryResult = resource->QueryInterface(
            IID_ID3D11Texture2D,
            reinterpret_cast<void**>(&texture));
        Release(resource);
        if (queryResult < 0 || !texture) {
            return false;
        }

        texture->GetDesc(&a_description);
        *a_texture = texture;
        return true;
    }

    bool PostProcessService::EnsureSceneCopy(
        ID3D11Device* const a_device,
        const D3D11_TEXTURE2D_DESC& a_sourceDescription) noexcept
    {
        D3D11_TEXTURE2D_DESC desired = a_sourceDescription;
        desired.mipLevels = 1;
        desired.arraySize = 1;
        desired.usage = D3D11_USAGE_DEFAULT;
        desired.bindFlags = D3D11_BIND_SHADER_RESOURCE;
        desired.cpuAccessFlags = 0;
        desired.miscFlags = 0;
        if (desired.sampleDesc.count > 1) {
            desired.sampleDesc.count = 1;
            desired.sampleDesc.quality = 0;
        }

        if (_sceneCopy &&
            _sceneCopyDescription.width == desired.width &&
            _sceneCopyDescription.height == desired.height &&
            _sceneCopyDescription.format == desired.format &&
            _sceneCopyDescription.sampleDesc.count == desired.sampleDesc.count &&
            _sceneCopyDescription.sampleDesc.quality == desired.sampleDesc.quality) {
            return true;
        }

        ReleaseSceneCopy();
        if (a_device->CreateTexture2D(
                &desired,
                nullptr,
                &_sceneCopy) < 0 ||
            a_device->CreateShaderResourceView(
                _sceneCopy,
                nullptr,
                &_sceneCopyView) < 0) {
            ReleaseSceneCopy();
            return false;
        }

        _sceneCopyDescription = desired;
        return true;
    }

    bool PostProcessService::Draw(
        const HF_PostProcessEffectHandle a_handle,
        const HF_RenderStageContextV1& a_renderContext,
        const void* const a_constants,
        const std::uint32_t a_constantBytes,
        const std::string_view a_moduleName) noexcept
    {
        if (a_handle == HF_INVALID_POST_PROCESS_EFFECT_HANDLE ||
            a_moduleName.empty() ||
            a_renderContext.structSize < sizeof(HF_RenderStageContextV1) ||
            (a_renderContext.flags & HF_RENDER_CONTEXT_DEVICE_AVAILABLE) == 0 ||
            (a_renderContext.flags & HF_RENDER_CONTEXT_CONTEXT_AVAILABLE) == 0 ||
            a_renderContext.device == 0 ||
            a_renderContext.immediateContext == 0) {
            return false;
        }

        std::scoped_lock lock{ _lock };
        const auto it = std::ranges::find_if(
            _effects,
            [a_handle](const Effect& a_effect) {
                return a_effect.handle == a_handle;
            });
        if (it == _effects.end() ||
            !NamesEqual(it->owner, a_moduleName) ||
            it->stage != a_renderContext.stage ||
            a_constantBytes != it->constantBufferSize ||
            (a_constantBytes > 0 && !a_constants)) {
            return false;
        }

        auto* const device = reinterpret_cast<ID3D11Device*>(
            static_cast<std::uintptr_t>(a_renderContext.device));
        auto* const context = reinterpret_cast<ID3D11DeviceContext*>(
            static_cast<std::uintptr_t>(a_renderContext.immediateContext));
        if (!device || !context ||
            !EnsureSharedResources(device) ||
            !EnsureEffectResources(*it, device)) {
            return false;
        }

        PipelineState state{};
        CaptureState(context, state);
        if (!state.renderTarget) {
            RestoreState(context, state);
            return false;
        }

        ID3D11Texture2D* sourceTexture = nullptr;
        D3D11_TEXTURE2D_DESC sourceDescription{};
        if (!GetRenderTargetTexture(
                state.renderTarget,
                &sourceTexture,
                sourceDescription)) {
            RestoreState(context, state);
            return false;
        }

        const auto graphicsState = RE::BSGraphics::State::GetSingleton();
        const bool fullSizeTarget =
            (sourceDescription.width == graphicsState.screenWidth &&
             sourceDescription.height == graphicsState.screenHeight) ||
            (sourceDescription.width == graphicsState.backBufferWidth &&
             sourceDescription.height == graphicsState.backBufferHeight);

        if (!fullSizeTarget ||
            !EnsureSceneCopy(device, sourceDescription)) {
            Release(sourceTexture);
            RestoreState(context, state);
            return false;
        }

        context->OMSetRenderTargets(0, nullptr, nullptr);
        if (sourceDescription.sampleDesc.count > 1) {
            context->ResolveSubresource(
                _sceneCopy,
                0,
                sourceTexture,
                0,
                sourceDescription.format);
        } else {
            context->CopySubresourceRegion(
                _sceneCopy,
                0,
                0,
                0,
                0,
                sourceTexture,
                0,
                nullptr);
        }
        Release(sourceTexture);

        context->OMSetRenderTargets(
            1,
            &state.renderTarget,
            state.depthStencilView);

        if (it->constantBufferSize > 0) {
            context->UpdateSubresource(
                it->constantBuffer,
                0,
                nullptr,
                a_constants,
                0,
                0);
        }

        ID3D11Buffer* nullVertexBuffer = nullptr;
        std::uint32_t zero = 0;
        context->IASetInputLayout(nullptr);
        context->IASetVertexBuffers(
            0,
            1,
            &nullVertexBuffer,
            &zero,
            &zero);
        context->IASetIndexBuffer(
            nullptr,
            DXGI_FORMAT_UNKNOWN,
            0);
        context->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(_vertexShader, nullptr, 0);
        context->PSSetShader(it->pixelShader, nullptr, 0);
        context->PSSetShaderResources(0, 1, &_sceneCopyView);
        context->PSSetSamplers(0, 1, &_samplerState);
        if (it->constantBufferSize > 0) {
            context->PSSetConstantBuffers(
                0,
                1,
                &it->constantBuffer);
        } else {
            ID3D11Buffer* nullBuffer = nullptr;
            context->PSSetConstantBuffers(0, 1, &nullBuffer);
        }
        context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        context->RSSetState(_rasterizerState);

        const D3D11_VIEWPORT viewport{
            0.0f,
            0.0f,
            static_cast<float>(sourceDescription.width),
            static_cast<float>(sourceDescription.height),
            0.0f,
            1.0f
        };
        context->RSSetViewports(1, &viewport);
        context->OMSetBlendState(
            _blendState,
            nullptr,
            0xFFFFFFFF);
        context->OMSetDepthStencilState(
            _depthStencilState,
            0);
        context->Draw(3, 0);

        RestoreState(context, state);
        return true;
    }
}
