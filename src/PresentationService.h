#pragma once

namespace HolyFramework
{
    enum class FrameworkPresentMaintenanceReason : std::uint32_t
    {
        LoadingMenu = 1u << 0,
        FramePacing = 1u << 1,
        WindowCursor = 1u << 2,
        PresentationPolicy = 1u << 3,
        StateFPS = 1u << 4,
        CPUScheduling = 1u << 5,
        RuntimeTuning = 1u << 6
    };

    class PresentationService final
    {
    public:
        static PresentationService& GetSingleton() noexcept;

        [[nodiscard]] bool IsAvailable() noexcept;
        [[nodiscard]] bool GetState(HF_PresentationStateV1& a_outState) const noexcept;
        [[nodiscard]] bool GetCapabilities(HF_PresentationCapabilitiesV1& a_outCapabilities) noexcept;

        HF_PresentationSubscriptionHandle SubscribePresentOwned(
            std::int32_t a_priority,
            HF_PresentCallback a_callback,
            void* a_userData,
            std::string_view a_moduleName,
            HF_LogHandle a_logger) noexcept;
        HF_PresentationSubscriptionHandle SubscribeResizeBuffersOwned(
            std::int32_t a_priority,
            HF_ResizeBuffersCallback a_callback,
            void* a_userData,
            std::string_view a_moduleName,
            HF_LogHandle a_logger) noexcept;
        HF_PresentationSubscriptionHandle SubscribeSwapChainCreateOwned(
            std::int32_t a_priority,
            HF_SwapChainCreateCallback a_callback,
            void* a_userData,
            std::string_view a_moduleName,
            HF_LogHandle a_logger) noexcept;
        bool UnsubscribeOwned(
            HF_PresentationSubscriptionHandle a_handle,
            std::string_view a_moduleName,
            std::string* a_outActualOwner = nullptr) noexcept;
        std::uint32_t UnsubscribeOwnedBy(std::string_view a_moduleName) noexcept;

        // Installs lazily once the renderer swap chain exists and passively
        // audits the DXGI vtable slots and optional early creation call site.
        // Never overwrites a later external hook if an observed site changes.
        [[nodiscard]] bool AuditHooks() noexcept;

        // Internal framework demand for the shared Present path. Multiple core
        // services may require Present simultaneously; reasons are bit-arbitrated.
        void SetFrameworkPresentMaintenance(
            FrameworkPresentMaintenanceReason a_reason,
            bool a_required) noexcept;
        void SetFrameworkSwapChainCreatePolicy(bool a_required) noexcept;

    private:
        enum class SubscriptionKind : std::uint32_t
        {
            Present = 1,
            ResizeBuffers = 2,
            SwapChainCreate = 3
        };

        using Present_t = REX::W32::HRESULT(__stdcall*)(
            REX::W32::IDXGISwapChain*,
            std::uint32_t,
            std::uint32_t);
        using ResizeBuffers_t = REX::W32::HRESULT(__stdcall*)(
            REX::W32::IDXGISwapChain*,
            std::uint32_t,
            std::uint32_t,
            std::uint32_t,
            REX::W32::DXGI_FORMAT,
            std::uint32_t);
        using CreateSwapChain_t = REX::W32::HRESULT(__stdcall*)(
            REX::W32::IDXGIAdapter*,
            REX::W32::D3D_DRIVER_TYPE,
            REX::W32::HMODULE,
            std::uint32_t,
            const REX::W32::D3D_FEATURE_LEVEL*,
            std::uint32_t,
            std::uint32_t,
            const REX::W32::DXGI_SWAP_CHAIN_DESC*,
            REX::W32::IDXGISwapChain**,
            REX::W32::ID3D11Device**,
            REX::W32::D3D_FEATURE_LEVEL*,
            REX::W32::ID3D11DeviceContext**);

        struct SubscriptionRuntime final
        {
            std::atomic_bool enabled{ true };
            std::atomic_uint32_t inFlight{ 0 };
            std::atomic_uint64_t calls{ 0 };
            std::atomic_int64_t lastSlowWarningMilliseconds{ 0 };
            std::mutex waitLock;
            std::condition_variable waitCv;
        };

        struct Subscription final
        {
            HF_PresentationSubscriptionHandle handle{ HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE };
            SubscriptionKind kind{ SubscriptionKind::Present };
            std::int32_t priority{ HF_PRESENTATION_PRIORITY_NORMAL };
            HF_PresentCallback presentCallback{ nullptr };
            HF_ResizeBuffersCallback resizeCallback{ nullptr };
            HF_SwapChainCreateCallback createCallback{ nullptr };
            void* userData{ nullptr };
            std::string moduleName;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
            std::shared_ptr<SubscriptionRuntime> runtime;
        };

        using SubscriptionList = std::vector<Subscription>;

        PresentationService() = default;

        static REX::W32::HRESULT __stdcall PresentThunk(
            REX::W32::IDXGISwapChain* a_swapChain,
            std::uint32_t a_syncInterval,
            std::uint32_t a_flags) noexcept;
        static REX::W32::HRESULT __stdcall ResizeBuffersThunk(
            REX::W32::IDXGISwapChain* a_swapChain,
            std::uint32_t a_bufferCount,
            std::uint32_t a_width,
            std::uint32_t a_height,
            REX::W32::DXGI_FORMAT a_newFormat,
            std::uint32_t a_swapChainFlags) noexcept;
        static REX::W32::HRESULT __stdcall CreateSwapChainThunk(
            REX::W32::IDXGIAdapter* a_adapter,
            REX::W32::D3D_DRIVER_TYPE a_driverType,
            REX::W32::HMODULE a_software,
            std::uint32_t a_flags,
            const REX::W32::D3D_FEATURE_LEVEL* a_featureLevels,
            std::uint32_t a_featureLevelCount,
            std::uint32_t a_sdkVersion,
            const REX::W32::DXGI_SWAP_CHAIN_DESC* a_swapChainDesc,
            REX::W32::IDXGISwapChain** a_swapChain,
            REX::W32::ID3D11Device** a_device,
            REX::W32::D3D_FEATURE_LEVEL* a_featureLevel,
            REX::W32::ID3D11DeviceContext** a_immediateContext) noexcept;

        [[nodiscard]] bool TryInstallHooks() noexcept;
        [[nodiscard]] bool TryInstallCreateHook() noexcept;
        [[nodiscard]] bool AuditVTableHooks() noexcept;
        [[nodiscard]] bool AuditCreateHook() noexcept;
        [[nodiscard]] bool HasSubscribers() const noexcept;
        [[nodiscard]] bool HasSubscribers(SubscriptionKind a_kind) const noexcept;
        static bool WriteVTableSlot(std::uintptr_t* a_slot, std::uintptr_t a_value) noexcept;
        static bool ReadPointer(std::uintptr_t a_address, std::uintptr_t& a_outValue) noexcept;
        static bool ReadBytes(std::uintptr_t a_address, void* a_outBytes, std::size_t a_size) noexcept;
        static bool NamesEqual(std::string_view a_left, std::string_view a_right) noexcept;
        static std::int64_t SteadyMilliseconds() noexcept;
        static void WaitForQuiescence(
            const std::shared_ptr<SubscriptionRuntime>& a_runtime,
            HF_PresentationSubscriptionHandle a_handle) noexcept;
        static void LeaveCallback(const std::shared_ptr<SubscriptionRuntime>& a_runtime) noexcept;
        void PublishSnapshot(std::shared_ptr<const SubscriptionList> a_snapshot) noexcept;
        [[nodiscard]] std::shared_ptr<const SubscriptionList> LoadSnapshot() const noexcept;

        void DispatchPresent(HF_PresentContextV1& a_context) noexcept;
        void DispatchResizeBuffers(HF_ResizeBuffersContextV1& a_context) noexcept;
        void DispatchSwapChainCreate(HF_SwapChainCreateContextV1& a_context) noexcept;
        void MarkCompromised() noexcept;
        void MarkCreateCompromised() noexcept;
        void QueryCapabilitiesOnce() noexcept;

        mutable std::mutex _subscriptionLock;
        std::atomic<std::shared_ptr<const SubscriptionList>> _subscriptions{ std::make_shared<const SubscriptionList>() };
        std::atomic<HF_PresentationSubscriptionHandle> _nextHandle{ 1 };

        std::mutex _installLock;
        std::atomic_bool _installed{ false };
        std::atomic_bool _compromised{ false };
        std::atomic_bool _installFailureReported{ false };
        std::atomic<std::uintptr_t> _presentSlotAddress{ 0 };
        std::atomic<std::uintptr_t> _resizeSlotAddress{ 0 };
        std::atomic<Present_t> _originalPresent{ nullptr };
        std::atomic<ResizeBuffers_t> _originalResizeBuffers{ nullptr };
        std::atomic_uint64_t _presentSequence{ 0 };
        std::atomic_uint64_t _resizeSequence{ 0 };
        std::atomic_uint32_t _frameworkPresentMaintenanceFlags{ 0 };

        std::mutex _createInstallLock;
        std::atomic_bool _createInstalled{ false };
        std::atomic_bool _createCompromised{ false };
        std::atomic_bool _createInstallFailureReported{ false };
        std::atomic<std::uintptr_t> _createCallSiteAddress{ 0 };
        std::array<std::uint8_t, 5> _createInstalledBytes{};
        std::atomic<CreateSwapChain_t> _originalCreateSwapChain{ nullptr };
        std::atomic_uint64_t _createSequence{ 0 };
        std::atomic_bool _frameworkCreatePolicyRequired{ false };

        std::once_flag _capabilitiesOnce;
        HF_PresentationCapabilitiesV1 _capabilities{};
    };
}
