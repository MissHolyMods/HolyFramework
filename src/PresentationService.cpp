#include "pch.h"

#include <Windows.h>
#ifdef ERROR
#  undef ERROR
#endif

#include "PresentationService.h"

#include "PresentationPolicyService.h"

#include "Diagnostics.h"
#include "GraphicsService.h"
#include "FramePacingService.h"
#include "StateFPSService.h"
#include "CPUSchedulingService.h"
#include "RuntimeTuningService.h"
#include "WindowService.h"
#include "ModuleContext.h"
#include "RuntimeState.h"
#include "UIStateService.h"

namespace HolyFramework
{
    namespace
    {
        inline constexpr std::size_t kPresentVTableIndex = 8;
        inline constexpr std::size_t kResizeBuffersVTableIndex = 13;
        inline constexpr std::uint64_t kSlowCallbackMicroseconds = 50'000;
        inline constexpr std::int64_t kSlowWarningCooldownMilliseconds = 30'000;
        inline constexpr std::uint32_t kMaximumBufferCount = 16;
        inline constexpr std::uint32_t kMaximumDimension = 16'384;
        inline constexpr std::uint32_t kMaximumSampleCount = 32;
        inline constexpr REL::ID kD3D11CreateCallSiteID{ 4492363 };
        inline constexpr std::ptrdiff_t kD3D11CreateCallOffset{ 0x410 };
        inline constexpr REX::W32::HRESULT kEFail = static_cast<REX::W32::HRESULT>(-2147467259);
        thread_local HF_PresentationSubscriptionHandle g_currentPresentationSubscription =
            HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE;
    }

    PresentationService& PresentationService::GetSingleton() noexcept
    {
        static PresentationService* instance = new PresentationService();
        return *instance;
    }

    bool PresentationService::NamesEqual(
        const std::string_view a_left,
        const std::string_view a_right) noexcept
    {
        if (a_left.size() != a_right.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a_left.size(); ++i) {
            const auto left = static_cast<unsigned char>(a_left[i]);
            const auto right = static_cast<unsigned char>(a_right[i]);
            if (std::tolower(left) != std::tolower(right)) {
                return false;
            }
        }
        return true;
    }

    std::int64_t PresentationService::SteadyMilliseconds() noexcept
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    bool PresentationService::ReadPointer(
        const std::uintptr_t a_address,
        std::uintptr_t& a_outValue) noexcept
    {
        a_outValue = 0;
        if (a_address == 0) {
            return false;
        }
        SIZE_T bytesRead = 0;
        const auto ok = ::ReadProcessMemory(
            ::GetCurrentProcess(),
            reinterpret_cast<const void*>(a_address),
            &a_outValue,
            sizeof(a_outValue),
            &bytesRead);
        return ok != FALSE && bytesRead == sizeof(a_outValue);
    }

    bool PresentationService::ReadBytes(
        const std::uintptr_t a_address,
        void* const a_outBytes,
        const std::size_t a_size) noexcept
    {
        if (a_address == 0 || !a_outBytes || a_size == 0) {
            return false;
        }
        SIZE_T bytesRead = 0;
        const auto ok = ::ReadProcessMemory(
            ::GetCurrentProcess(),
            reinterpret_cast<const void*>(a_address),
            a_outBytes,
            a_size,
            &bytesRead);
        return ok != FALSE && bytesRead == a_size;
    }

    bool PresentationService::WriteVTableSlot(
        std::uintptr_t* const a_slot,
        const std::uintptr_t a_value) noexcept
    {
        if (!a_slot || a_value == 0) {
            return false;
        }

        DWORD oldProtection = 0;
        if (::VirtualProtect(
                a_slot,
                sizeof(std::uintptr_t),
                PAGE_EXECUTE_READWRITE,
                &oldProtection) == FALSE) {
            return false;
        }

        std::atomic_ref<std::uintptr_t>(*a_slot).store(a_value, std::memory_order_release);

        DWORD ignoredProtection = 0;
        (void)::VirtualProtect(
            a_slot,
            sizeof(std::uintptr_t),
            oldProtection,
            &ignoredProtection);

        return std::atomic_ref<std::uintptr_t>(*a_slot).load(std::memory_order_acquire) == a_value;
    }

    void PresentationService::WaitForQuiescence(
        const std::shared_ptr<SubscriptionRuntime>& a_runtime,
        const HF_PresentationSubscriptionHandle a_handle) noexcept
    {
        if (!a_runtime || g_currentPresentationSubscription == a_handle) {
            return;
        }
        try {
            std::unique_lock lock{ a_runtime->waitLock };
            a_runtime->waitCv.wait(lock, [&] {
                return a_runtime->inFlight.load(std::memory_order_acquire) == 0;
            });
        } catch (...) {
        }
    }

    void PresentationService::LeaveCallback(
        const std::shared_ptr<SubscriptionRuntime>& a_runtime) noexcept
    {
        if (!a_runtime) {
            return;
        }
        if (a_runtime->inFlight.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            try {
                std::scoped_lock lock{ a_runtime->waitLock };
                a_runtime->waitCv.notify_all();
            } catch (...) {
            }
        }
    }

    void PresentationService::PublishSnapshot(
        std::shared_ptr<const SubscriptionList> a_snapshot) noexcept
    {
        _subscriptions.store(std::move(a_snapshot), std::memory_order_release);
    }

    std::shared_ptr<const PresentationService::SubscriptionList>
    PresentationService::LoadSnapshot() const noexcept
    {
        return _subscriptions.load(std::memory_order_acquire);
    }

    bool PresentationService::HasSubscribers() const noexcept
    {
        const auto listeners = LoadSnapshot();
        return listeners && !listeners->empty();
    }

    bool PresentationService::HasSubscribers(const SubscriptionKind a_kind) const noexcept
    {
        const auto listeners = LoadSnapshot();
        if (!listeners) {
            return false;
        }
        return std::ranges::any_of(*listeners, [a_kind](const Subscription& sub) {
            return sub.kind == a_kind;
        });
    }

    void PresentationService::MarkCompromised() noexcept
    {
        bool expected = false;
        if (_compromised.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            Diagnostics::ReportFrameworkWarningForModule(
                "HolyFramework",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_PRESENTATION_HOOK_CHANGED_EXTERNALLY);
        }
    }

    void PresentationService::MarkCreateCompromised() noexcept
    {
        bool expected = false;
        if (_createCompromised.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            Diagnostics::ReportFrameworkWarningForModule(
                "HolyFramework",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_PRESENTATION_CREATE_HOOK_CHANGED_EXTERNALLY);
        }
    }

    bool PresentationService::TryInstallHooks() noexcept
    {
        if (_compromised.load(std::memory_order_acquire)) {
            return false;
        }
        if (_installed.load(std::memory_order_acquire)) {
            return true;
        }
        if (_frameworkPresentMaintenanceFlags.load(std::memory_order_acquire) == 0 &&
            !HasSubscribers(SubscriptionKind::Present) &&
            !HasSubscribers(SubscriptionKind::ResizeBuffers)) {
            return true;
        }

        std::scoped_lock lock{ _installLock };
        if (_compromised.load(std::memory_order_relaxed)) {
            return false;
        }
        if (_installed.load(std::memory_order_relaxed)) {
            return true;
        }

        HF_GraphicsNativeHandlesV1 handles{};
        if (!GraphicsService::GetSingleton().GetNativeHandles(handles) || handles.swapChain == 0) {
            // Renderer creation is asynchronous relative to module loading. A
            // pending subscription is valid; the integrity monitor retries.
            return false;
        }

        auto* const swapChain = reinterpret_cast<REX::W32::IDXGISwapChain*>(
            static_cast<std::uintptr_t>(handles.swapChain));
        auto* const vtable = swapChain ? *reinterpret_cast<std::uintptr_t**>(swapChain) : nullptr;
        if (!vtable) {
            return false;
        }

        auto* const presentSlot = std::addressof(vtable[kPresentVTableIndex]);
        auto* const resizeSlot = std::addressof(vtable[kResizeBuffersVTableIndex]);
        const auto presentTarget = std::atomic_ref<std::uintptr_t>(*presentSlot).load(std::memory_order_acquire);
        const auto resizeTarget = std::atomic_ref<std::uintptr_t>(*resizeSlot).load(std::memory_order_acquire);
        if (presentTarget == 0 || resizeTarget == 0) {
            return false;
        }

        const auto presentThunk = reinterpret_cast<std::uintptr_t>(&PresentThunk);
        const auto resizeThunk = reinterpret_cast<std::uintptr_t>(&ResizeBuffersThunk);
        if (presentTarget == presentThunk || resizeTarget == resizeThunk) {
            if (presentTarget == presentThunk && resizeTarget == resizeThunk &&
                _originalPresent.load(std::memory_order_acquire) != nullptr &&
                _originalResizeBuffers.load(std::memory_order_acquire) != nullptr) {
                _presentSlotAddress.store(reinterpret_cast<std::uintptr_t>(presentSlot), std::memory_order_release);
                _resizeSlotAddress.store(reinterpret_cast<std::uintptr_t>(resizeSlot), std::memory_order_release);
                _installed.store(true, std::memory_order_release);
                return true;
            }

            if (!_installFailureReported.exchange(true, std::memory_order_acq_rel)) {
                Diagnostics::ReportFrameworkFailureForModule(
                    "HolyFramework",
                    HF_INVALID_LOG_HANDLE,
                    HF_ERROR_PRESENTATION_HOOK_INSTALL_FAILED);
            }
            return false;
        }

        _originalPresent.store(reinterpret_cast<Present_t>(presentTarget), std::memory_order_release);
        _originalResizeBuffers.store(reinterpret_cast<ResizeBuffers_t>(resizeTarget), std::memory_order_release);

        if (!WriteVTableSlot(presentSlot, presentThunk)) {
            _originalPresent.store(nullptr, std::memory_order_release);
            _originalResizeBuffers.store(nullptr, std::memory_order_release);
            if (!_installFailureReported.exchange(true, std::memory_order_acq_rel)) {
                Diagnostics::ReportFrameworkFailureForModule(
                    "HolyFramework",
                    HF_INVALID_LOG_HANDLE,
                    HF_ERROR_PRESENTATION_HOOK_INSTALL_FAILED);
            }
            return false;
        }

        if (!WriteVTableSlot(resizeSlot, resizeThunk)) {
            // Conservative rollback: restore Present only if our thunk is still
            // there. If another component changed Present during this narrow
            // installation window, keep the chain target alive because that
            // component may already be chaining through our thunk.
            const auto currentPresent =
                std::atomic_ref<std::uintptr_t>(*presentSlot).load(std::memory_order_acquire);
            if (currentPresent == presentThunk) {
                if (WriteVTableSlot(presentSlot, presentTarget)) {
                    _originalPresent.store(nullptr, std::memory_order_release);
                    _originalResizeBuffers.store(nullptr, std::memory_order_release);
                }
            } else {
                MarkCompromised();
            }
            if (!_installFailureReported.exchange(true, std::memory_order_acq_rel)) {
                Diagnostics::ReportFrameworkFailureForModule(
                    "HolyFramework",
                    HF_INVALID_LOG_HANDLE,
                    HF_ERROR_PRESENTATION_HOOK_INSTALL_FAILED);
            }
            return false;
        }

        _presentSlotAddress.store(reinterpret_cast<std::uintptr_t>(presentSlot), std::memory_order_release);
        _resizeSlotAddress.store(reinterpret_cast<std::uintptr_t>(resizeSlot), std::memory_order_release);
        _installed.store(true, std::memory_order_release);
        _installFailureReported.store(false, std::memory_order_release);
        REX::INFO("HolyFramework presentation observer installed: DXGI Present/ResizeBuffers");
        return true;
    }

    bool PresentationService::TryInstallCreateHook() noexcept
    {
        if (_createCompromised.load(std::memory_order_acquire)) {
            return false;
        }
        if (_createInstalled.load(std::memory_order_acquire)) {
            return true;
        }
        if (!_frameworkCreatePolicyRequired.load(std::memory_order_acquire) &&
            !HasSubscribers(SubscriptionKind::SwapChainCreate)) {
            return true;
        }

        std::scoped_lock lock{ _createInstallLock };
        if (_createCompromised.load(std::memory_order_relaxed)) {
            return false;
        }
        if (_createInstalled.load(std::memory_order_relaxed)) {
            return true;
        }

        try {
            const REL::Relocation<std::uintptr_t> callSite{
                kD3D11CreateCallSiteID,
                kD3D11CreateCallOffset
            };
            std::array<std::uint8_t, 5> before{};
            if (!ReadBytes(callSite.address(), before.data(), before.size()) || before[0] != 0xE8) {
                if (!_createInstallFailureReported.exchange(true, std::memory_order_acq_rel)) {
                    Diagnostics::ReportFrameworkFailureForModule(
                        "HolyFramework",
                        HF_INVALID_LOG_HANDLE,
                        HF_ERROR_PRESENTATION_CREATE_HOOK_INSTALL_FAILED);
                }
                return false;
            }

            auto& trampoline = REL::GetTrampoline();
            const auto original = trampoline.write_call<5>(callSite.address(), CreateSwapChainThunk);
            if (!original) {
                if (!_createInstallFailureReported.exchange(true, std::memory_order_acq_rel)) {
                    Diagnostics::ReportFrameworkFailureForModule(
                        "HolyFramework",
                        HF_INVALID_LOG_HANDLE,
                        HF_ERROR_PRESENTATION_CREATE_HOOK_INSTALL_FAILED);
                }
                return false;
            }

            _originalCreateSwapChain.store(reinterpret_cast<CreateSwapChain_t>(original), std::memory_order_release);
            _createCallSiteAddress.store(callSite.address(), std::memory_order_release);

            std::array<std::uint8_t, 5> installed{};
            if (!ReadBytes(callSite.address(), installed.data(), installed.size()) || installed[0] != 0xE8) {
                _createCompromised.store(true, std::memory_order_release);
                if (!_createInstallFailureReported.exchange(true, std::memory_order_acq_rel)) {
                    Diagnostics::ReportFrameworkFailureForModule(
                        "HolyFramework",
                        HF_INVALID_LOG_HANDLE,
                        HF_ERROR_PRESENTATION_CREATE_HOOK_INSTALL_FAILED);
                }
                return false;
            }

            _createInstalledBytes = installed;
            _createInstalled.store(true, std::memory_order_release);
            _createInstallFailureReported.store(false, std::memory_order_release);
            REX::INFO("HolyFramework presentation creation observer installed: D3D11CreateDeviceAndSwapChain");
            return true;
        } catch (const std::exception&) {
            if (!_createInstallFailureReported.exchange(true, std::memory_order_acq_rel)) {
                Diagnostics::ReportFrameworkFailureForModule(
                    "HolyFramework",
                    HF_INVALID_LOG_HANDLE,
                    HF_ERROR_PRESENTATION_CREATE_HOOK_INSTALL_FAILED);
            }
            return false;
        } catch (...) {
            if (!_createInstallFailureReported.exchange(true, std::memory_order_acq_rel)) {
                Diagnostics::ReportFrameworkFailureForModule(
                    "HolyFramework",
                    HF_INVALID_LOG_HANDLE,
                    HF_ERROR_PRESENTATION_CREATE_HOOK_INSTALL_FAILED);
            }
            return false;
        }
    }

    bool PresentationService::AuditVTableHooks() noexcept
    {
        if (_compromised.load(std::memory_order_acquire)) {
            return false;
        }

        if (!_installed.load(std::memory_order_acquire)) {
            if (_frameworkPresentMaintenanceFlags.load(std::memory_order_acquire) != 0 ||
                HasSubscribers(SubscriptionKind::Present) ||
                HasSubscribers(SubscriptionKind::ResizeBuffers)) {
                (void)TryInstallHooks();
            }
            return !_compromised.load(std::memory_order_acquire);
        }

        const auto presentAddress = _presentSlotAddress.load(std::memory_order_acquire);
        const auto resizeAddress = _resizeSlotAddress.load(std::memory_order_acquire);
        std::uintptr_t currentPresent = 0;
        std::uintptr_t currentResize = 0;
        if (!ReadPointer(presentAddress, currentPresent) || !ReadPointer(resizeAddress, currentResize)) {
            return true;
        }

        const auto expectedPresent = reinterpret_cast<std::uintptr_t>(&PresentThunk);
        const auto expectedResize = reinterpret_cast<std::uintptr_t>(&ResizeBuffersThunk);
        if (currentPresent == expectedPresent && currentResize == expectedResize) {
            return true;
        }

        MarkCompromised();
        return false;
    }

    bool PresentationService::AuditCreateHook() noexcept
    {
        if (_createCompromised.load(std::memory_order_acquire)) {
            return false;
        }
        if (!_createInstalled.load(std::memory_order_acquire)) {
            if (_frameworkCreatePolicyRequired.load(std::memory_order_acquire) ||
                HasSubscribers(SubscriptionKind::SwapChainCreate)) {
                (void)TryInstallCreateHook();
            }
            return !_createCompromised.load(std::memory_order_acquire);
        }

        std::scoped_lock lock{ _createInstallLock };
        const auto address = _createCallSiteAddress.load(std::memory_order_acquire);
        std::array<std::uint8_t, 5> current{};
        if (!ReadBytes(address, current.data(), current.size())) {
            return true;
        }
        if (current == _createInstalledBytes) {
            return true;
        }

        MarkCreateCompromised();
        return false;
    }

    void PresentationService::SetFrameworkPresentMaintenance(
        const FrameworkPresentMaintenanceReason a_reason,
        const bool a_required) noexcept
    {
        const auto bit = static_cast<std::uint32_t>(a_reason);
        if (bit == 0) {
            return;
        }
        if (a_required) {
            _frameworkPresentMaintenanceFlags.fetch_or(bit, std::memory_order_acq_rel);
            // A missing swap chain during early module loading is a pending
            // state, not a failure. Lifecycle/integrity audits retry later.
            (void)TryInstallHooks();
        } else {
            _frameworkPresentMaintenanceFlags.fetch_and(~bit, std::memory_order_acq_rel);
        }
    }

    void PresentationService::SetFrameworkSwapChainCreatePolicy(const bool a_required) noexcept
    {
        _frameworkCreatePolicyRequired.store(a_required, std::memory_order_release);
        if (a_required) {
            (void)TryInstallCreateHook();
        }
    }

    bool PresentationService::AuditHooks() noexcept
    {
        const auto vtableOK = AuditVTableHooks();
        const auto createOK = AuditCreateHook();
        return vtableOK && createOK;
    }

    bool PresentationService::IsAvailable() noexcept
    {
        if (_compromised.load(std::memory_order_acquire)) {
            return false;
        }
        if (_installed.load(std::memory_order_acquire)) {
            return true;
        }

        HF_PresentationStateV1 state{};
        if (!GetState(state) || (state.flags & HF_PRESENTATION_STATE_AVAILABLE) == 0) {
            return false;
        }
        if (_frameworkPresentMaintenanceFlags.load(std::memory_order_acquire) != 0 ||
            HasSubscribers(SubscriptionKind::Present) ||
            HasSubscribers(SubscriptionKind::ResizeBuffers)) {
            (void)TryInstallHooks();
        }
        if (_frameworkCreatePolicyRequired.load(std::memory_order_acquire) ||
            HasSubscribers(SubscriptionKind::SwapChainCreate)) {
            (void)TryInstallCreateHook();
        }
        return !_compromised.load(std::memory_order_acquire);
    }

    bool PresentationService::GetState(HF_PresentationStateV1& a_outState) const noexcept
    {
        a_outState = {};
        a_outState.structSize = sizeof(HF_PresentationStateV1);
        if (_installed.load(std::memory_order_acquire)) {
            a_outState.flags |= HF_PRESENTATION_STATE_HOOKS_INSTALLED;
        }
        if (_compromised.load(std::memory_order_acquire)) {
            a_outState.flags |= HF_PRESENTATION_STATE_COMPROMISED;
        }
        if (_createInstalled.load(std::memory_order_acquire)) {
            a_outState.flags |= HF_PRESENTATION_STATE_CREATE_HOOK_INSTALLED;
        }
        if (_createCompromised.load(std::memory_order_acquire)) {
            a_outState.flags |= HF_PRESENTATION_STATE_CREATE_HOOK_COMPROMISED;
        }

        try {
            HF_GraphicsNativeHandlesV1 handles{};
            if (!GraphicsService::GetSingleton().GetNativeHandles(handles) || handles.swapChain == 0) {
                return false;
            }

            auto* const swapChain = reinterpret_cast<REX::W32::IDXGISwapChain*>(
                static_cast<std::uintptr_t>(handles.swapChain));
            if (!swapChain) {
                return false;
            }

            REX::W32::DXGI_SWAP_CHAIN_DESC desc{};
            if (swapChain->GetDesc(std::addressof(desc)) < 0) {
                return false;
            }

            a_outState.flags |= HF_PRESENTATION_STATE_AVAILABLE;
            a_outState.bufferCount = desc.bufferCount;
            a_outState.width = desc.bufferDesc.width;
            a_outState.height = desc.bufferDesc.height;
            a_outState.format = static_cast<std::uint32_t>(desc.bufferDesc.format);
            a_outState.swapEffect = static_cast<std::uint32_t>(desc.swapEffect);
            a_outState.swapChainFlags = desc.flags;
            a_outState.sampleCount = desc.sampleDesc.count;
            a_outState.sampleQuality = desc.sampleDesc.quality;
            a_outState.windowed = desc.windowed != 0 ? HF_TRUE : HF_FALSE;

            if (desc.windowed != 0) {
                a_outState.flags |= HF_PRESENTATION_STATE_WINDOWED;
            }
            if (a_outState.swapEffect == HF_SWAP_EFFECT_FLIP_SEQUENTIAL) {
                a_outState.flags |= HF_PRESENTATION_STATE_FLIP_SEQUENTIAL;
            } else if (a_outState.swapEffect == HF_SWAP_EFFECT_FLIP_DISCARD) {
                a_outState.flags |= HF_PRESENTATION_STATE_FLIP_DISCARD;
            }
            if ((desc.flags & HF_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0) {
                a_outState.flags |= HF_PRESENTATION_STATE_ALLOW_TEARING;
            }
            return true;
        } catch (...) {
            const auto preserved = a_outState.flags &
                (HF_PRESENTATION_STATE_HOOKS_INSTALLED |
                 HF_PRESENTATION_STATE_COMPROMISED |
                 HF_PRESENTATION_STATE_CREATE_HOOK_INSTALLED |
                 HF_PRESENTATION_STATE_CREATE_HOOK_COMPROMISED);
            a_outState = {};
            a_outState.structSize = sizeof(HF_PresentationStateV1);
            a_outState.flags = preserved;
            return false;
        }
    }

    void PresentationService::QueryCapabilitiesOnce() noexcept
    {
        _capabilities = {};
        _capabilities.structSize = sizeof(HF_PresentationCapabilitiesV1);
        try {
            REX::W32::IDXGIFactory2* factory2 = nullptr;
            if (REX::W32::CreateDXGIFactory1(
                    REX::W32::IID_IDXGIFactory2,
                    reinterpret_cast<void**>(std::addressof(factory2))) >= 0 && factory2) {
                _capabilities.flags |= HF_PRESENTATION_CAPABILITY_FLIP_SEQUENTIAL;
                factory2->Release();
            }

            REX::W32::IDXGIFactory4* factory4 = nullptr;
            if (REX::W32::CreateDXGIFactory1(
                    REX::W32::IID_IDXGIFactory4,
                    reinterpret_cast<void**>(std::addressof(factory4))) >= 0 && factory4) {
                _capabilities.flags |= HF_PRESENTATION_CAPABILITY_FLIP_DISCARD;
                factory4->Release();
            }

            REX::W32::IDXGIFactory5* factory5 = nullptr;
            if (REX::W32::CreateDXGIFactory1(
                    REX::W32::IID_IDXGIFactory5,
                    reinterpret_cast<void**>(std::addressof(factory5))) >= 0 && factory5) {
                REX::W32::BOOL tearingSupported = 0;
                if (factory5->CheckFeatureSupport(
                        REX::W32::DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                        std::addressof(tearingSupported),
                        static_cast<std::uint32_t>(sizeof(tearingSupported))) >= 0 && tearingSupported != 0) {
                    _capabilities.flags |= HF_PRESENTATION_CAPABILITY_ALLOW_TEARING;
                }
                factory5->Release();
            }
        } catch (...) {
            Diagnostics::ReportFrameworkWarningForModule(
                "HolyFramework",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_PRESENTATION_CAPABILITY_QUERY_FAILED);
        }
    }

    bool PresentationService::GetCapabilities(HF_PresentationCapabilitiesV1& a_outCapabilities) noexcept
    {
        std::call_once(_capabilitiesOnce, [this] { QueryCapabilitiesOnce(); });
        a_outCapabilities = _capabilities;
        return true;
    }

    HF_PresentationSubscriptionHandle PresentationService::SubscribePresentOwned(
        const std::int32_t a_priority,
        const HF_PresentCallback a_callback,
        void* const a_userData,
        const std::string_view a_moduleName,
        const HF_LogHandle a_logger) noexcept
    {
        if (!a_callback || a_moduleName.empty()) {
            return HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE;
        }
        if (_compromised.load(std::memory_order_acquire)) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName,
                a_logger,
                HF_ERROR_PRESENTATION_HOOK_UNAVAILABLE);
            return HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE;
        }

        auto handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
        if (handle == HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE) {
            handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
        }

        try {
            auto runtime = std::make_shared<SubscriptionRuntime>();
            {
                std::scoped_lock lock{ _subscriptionLock };
                const auto current = LoadSnapshot();
                auto next = std::make_shared<SubscriptionList>(current ? *current : SubscriptionList{});
                next->push_back(Subscription{
                    .handle = handle,
                    .kind = SubscriptionKind::Present,
                    .priority = a_priority,
                    .presentCallback = a_callback,
                    .resizeCallback = nullptr,
                    .userData = a_userData,
                    .moduleName = std::string{ a_moduleName },
                    .logger = a_logger,
                    .runtime = std::move(runtime)
                });
                std::stable_sort(next->begin(), next->end(), [](const Subscription& left, const Subscription& right) {
                    if (left.kind != right.kind) {
                        return static_cast<std::uint32_t>(left.kind) < static_cast<std::uint32_t>(right.kind);
                    }
                    return left.priority < right.priority;
                });
                PublishSnapshot(next);
            }
            (void)TryInstallHooks();
            return handle;
        } catch (...) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName,
                a_logger,
                HF_ERROR_PRESENTATION_SUBSCRIBE_FAILED);
            return HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE;
        }
    }

    HF_PresentationSubscriptionHandle PresentationService::SubscribeResizeBuffersOwned(
        const std::int32_t a_priority,
        const HF_ResizeBuffersCallback a_callback,
        void* const a_userData,
        const std::string_view a_moduleName,
        const HF_LogHandle a_logger) noexcept
    {
        if (!a_callback || a_moduleName.empty()) {
            return HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE;
        }
        if (_compromised.load(std::memory_order_acquire)) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName,
                a_logger,
                HF_ERROR_PRESENTATION_HOOK_UNAVAILABLE);
            return HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE;
        }

        auto handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
        if (handle == HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE) {
            handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
        }

        try {
            auto runtime = std::make_shared<SubscriptionRuntime>();
            {
                std::scoped_lock lock{ _subscriptionLock };
                const auto current = LoadSnapshot();
                auto next = std::make_shared<SubscriptionList>(current ? *current : SubscriptionList{});
                next->push_back(Subscription{
                    .handle = handle,
                    .kind = SubscriptionKind::ResizeBuffers,
                    .priority = a_priority,
                    .presentCallback = nullptr,
                    .resizeCallback = a_callback,
                    .userData = a_userData,
                    .moduleName = std::string{ a_moduleName },
                    .logger = a_logger,
                    .runtime = std::move(runtime)
                });
                std::stable_sort(next->begin(), next->end(), [](const Subscription& left, const Subscription& right) {
                    if (left.kind != right.kind) {
                        return static_cast<std::uint32_t>(left.kind) < static_cast<std::uint32_t>(right.kind);
                    }
                    return left.priority < right.priority;
                });
                PublishSnapshot(next);
            }
            (void)TryInstallHooks();
            return handle;
        } catch (...) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName,
                a_logger,
                HF_ERROR_PRESENTATION_SUBSCRIBE_FAILED);
            return HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE;
        }
    }

    HF_PresentationSubscriptionHandle PresentationService::SubscribeSwapChainCreateOwned(
        const std::int32_t a_priority,
        const HF_SwapChainCreateCallback a_callback,
        void* const a_userData,
        const std::string_view a_moduleName,
        const HF_LogHandle a_logger) noexcept
    {
        if (!a_callback || a_moduleName.empty()) {
            return HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE;
        }
        if (_createCompromised.load(std::memory_order_acquire)) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName,
                a_logger,
                HF_ERROR_PRESENTATION_HOOK_UNAVAILABLE);
            return HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE;
        }

        auto handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
        if (handle == HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE) {
            handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
        }

        try {
            auto runtime = std::make_shared<SubscriptionRuntime>();
            {
                std::scoped_lock lock{ _subscriptionLock };
                const auto current = LoadSnapshot();
                auto next = std::make_shared<SubscriptionList>(current ? *current : SubscriptionList{});
                next->push_back(Subscription{
                    .handle = handle,
                    .kind = SubscriptionKind::SwapChainCreate,
                    .priority = a_priority,
                    .presentCallback = nullptr,
                    .resizeCallback = nullptr,
                    .createCallback = a_callback,
                    .userData = a_userData,
                    .moduleName = std::string{ a_moduleName },
                    .logger = a_logger,
                    .runtime = std::move(runtime)
                });
                std::stable_sort(next->begin(), next->end(), [](const Subscription& left, const Subscription& right) {
                    if (left.kind != right.kind) {
                        return static_cast<std::uint32_t>(left.kind) < static_cast<std::uint32_t>(right.kind);
                    }
                    return left.priority < right.priority;
                });
                PublishSnapshot(next);
            }

            if (!TryInstallCreateHook()) {
                std::string ignoredOwner;
                (void)UnsubscribeOwned(handle, a_moduleName, &ignoredOwner);
                return HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE;
            }
            return handle;
        } catch (...) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName,
                a_logger,
                HF_ERROR_PRESENTATION_SUBSCRIBE_FAILED);
            return HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE;
        }
    }

    bool PresentationService::UnsubscribeOwned(
        const HF_PresentationSubscriptionHandle a_handle,
        const std::string_view a_moduleName,
        std::string* const a_outActualOwner) noexcept
    {
        if (a_handle == HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE || a_moduleName.empty()) {
            return false;
        }

        std::shared_ptr<SubscriptionRuntime> runtime;
        try {
            {
                std::scoped_lock lock{ _subscriptionLock };
                const auto current = LoadSnapshot();
                if (!current) {
                    return false;
                }
                const auto it = std::ranges::find_if(*current, [a_handle](const Subscription& sub) {
                    return sub.handle == a_handle;
                });
                if (it == current->end()) {
                    return false;
                }
                if (!NamesEqual(it->moduleName, a_moduleName)) {
                    if (a_outActualOwner) {
                        *a_outActualOwner = it->moduleName;
                    }
                    return false;
                }

                runtime = it->runtime;
                if (runtime) {
                    runtime->enabled.store(false, std::memory_order_release);
                }
                auto next = std::make_shared<SubscriptionList>(*current);
                std::erase_if(*next, [a_handle](const Subscription& sub) {
                    return sub.handle == a_handle;
                });
                PublishSnapshot(next);
            }

            WaitForQuiescence(runtime, a_handle);
            return true;
        } catch (...) {
            return false;
        }
    }

    std::uint32_t PresentationService::UnsubscribeOwnedBy(
        const std::string_view a_moduleName) noexcept
    {
        if (a_moduleName.empty()) {
            return 0;
        }

        std::vector<std::pair<HF_PresentationSubscriptionHandle, std::shared_ptr<SubscriptionRuntime>>> retired;
        try {
            {
                std::scoped_lock lock{ _subscriptionLock };
                const auto current = LoadSnapshot();
                if (!current || current->empty()) {
                    return 0;
                }

                auto next = std::make_shared<SubscriptionList>();
                next->reserve(current->size());
                for (const auto& sub : *current) {
                    if (NamesEqual(sub.moduleName, a_moduleName)) {
                        if (sub.runtime) {
                            sub.runtime->enabled.store(false, std::memory_order_release);
                        }
                        retired.emplace_back(sub.handle, sub.runtime);
                    } else {
                        next->push_back(sub);
                    }
                }
                if (retired.empty()) {
                    return 0;
                }
                PublishSnapshot(next);
            }

            for (const auto& [handle, runtime] : retired) {
                WaitForQuiescence(runtime, handle);
            }
            return static_cast<std::uint32_t>(retired.size());
        } catch (...) {
            return 0;
        }
    }

    void PresentationService::DispatchPresent(HF_PresentContextV1& a_context) noexcept
    {
        const auto listeners = LoadSnapshot();
        if (!listeners || listeners->empty()) {
            return;
        }

        for (const auto& listener : *listeners) {
            if (listener.kind != SubscriptionKind::Present || !listener.presentCallback || !listener.runtime) {
                continue;
            }

            auto& runtime = listener.runtime;
            if (!runtime->enabled.load(std::memory_order_acquire)) {
                continue;
            }
            runtime->inFlight.fetch_add(1, std::memory_order_acq_rel);
            if (!runtime->enabled.load(std::memory_order_acquire)) {
                LeaveCallback(runtime);
                continue;
            }

            auto candidate = a_context;
            const auto previousHandle = g_currentPresentationSubscription;
            g_currentPresentationSubscription = listener.handle;
            ModuleContext::Scope scope{ listener.moduleName.c_str(), listener.logger };
            const auto start = std::chrono::steady_clock::now();
            bool callbackFailed = false;
            try {
                listener.presentCallback(&candidate, listener.userData);
            } catch (const std::exception&) {
                callbackFailed = true;
                Diagnostics::ReportFrameworkFailureForModule(
                    listener.moduleName,
                    listener.logger,
                    HF_ERROR_PRESENTATION_CALLBACK_EXCEPTION);
            } catch (...) {
                callbackFailed = true;
                Diagnostics::ReportFrameworkFailureForModule(
                    listener.moduleName,
                    listener.logger,
                    HF_ERROR_PRESENTATION_CALLBACK_EXCEPTION);
            }

            if (!callbackFailed) {
                const auto testWasSet = (a_context.presentFlags & HF_PRESENT_FLAG_TEST) != 0;
                const auto testIsSet = (candidate.presentFlags & HF_PRESENT_FLAG_TEST) != 0;
                if (candidate.syncInterval > 4 || testWasSet != testIsSet) {
                    Diagnostics::ReportFrameworkFailureForModule(
                        listener.moduleName,
                        listener.logger,
                        HF_ERROR_PRESENTATION_INVALID_MUTATION);
                } else {
                    a_context.syncInterval = candidate.syncInterval;
                    a_context.presentFlags = candidate.presentFlags;
                }
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count();
            const auto calls = runtime->calls.fetch_add(1, std::memory_order_relaxed) + 1;
            if (elapsed >= static_cast<std::int64_t>(kSlowCallbackMicroseconds)) {
                const auto nowMs = SteadyMilliseconds();
                auto lastMs = runtime->lastSlowWarningMilliseconds.load(std::memory_order_relaxed);
                if (lastMs == 0 || nowMs - lastMs >= kSlowWarningCooldownMilliseconds) {
                    if (runtime->lastSlowWarningMilliseconds.compare_exchange_strong(
                            lastMs, nowMs, std::memory_order_relaxed)) {
                        Diagnostics::ReportPerformanceWarning(
                            listener.moduleName,
                            "presentation.pre_present",
                            static_cast<std::uint64_t>(elapsed),
                            static_cast<std::uint32_t>(kSlowCallbackMicroseconds),
                            calls);
                    }
                }
            }

            if (callbackFailed) {
                (void)UnsubscribeOwned(listener.handle, listener.moduleName);
            }
            g_currentPresentationSubscription = previousHandle;
            LeaveCallback(runtime);
        }
    }

    void PresentationService::DispatchResizeBuffers(HF_ResizeBuffersContextV1& a_context) noexcept
    {
        const auto listeners = LoadSnapshot();
        if (!listeners || listeners->empty()) {
            return;
        }

        for (const auto& listener : *listeners) {
            if (listener.kind != SubscriptionKind::ResizeBuffers || !listener.resizeCallback || !listener.runtime) {
                continue;
            }

            auto& runtime = listener.runtime;
            if (!runtime->enabled.load(std::memory_order_acquire)) {
                continue;
            }
            runtime->inFlight.fetch_add(1, std::memory_order_acq_rel);
            if (!runtime->enabled.load(std::memory_order_acquire)) {
                LeaveCallback(runtime);
                continue;
            }

            auto candidate = a_context;
            const auto previousHandle = g_currentPresentationSubscription;
            g_currentPresentationSubscription = listener.handle;
            ModuleContext::Scope scope{ listener.moduleName.c_str(), listener.logger };
            const auto start = std::chrono::steady_clock::now();
            bool callbackFailed = false;
            try {
                listener.resizeCallback(&candidate, listener.userData);
            } catch (const std::exception&) {
                callbackFailed = true;
                Diagnostics::ReportFrameworkFailureForModule(
                    listener.moduleName,
                    listener.logger,
                    HF_ERROR_PRESENTATION_CALLBACK_EXCEPTION);
            } catch (...) {
                callbackFailed = true;
                Diagnostics::ReportFrameworkFailureForModule(
                    listener.moduleName,
                    listener.logger,
                    HF_ERROR_PRESENTATION_CALLBACK_EXCEPTION);
            }

            if (!callbackFailed) {
                // ResizeBuffers explicitly allows zero buffer count/size to mean
                // preserve/current values. Reject only values outside D3D11's
                // practical swap-chain safety bounds.
                if (candidate.bufferCount > kMaximumBufferCount ||
                    candidate.width > kMaximumDimension ||
                    candidate.height > kMaximumDimension) {
                    Diagnostics::ReportFrameworkFailureForModule(
                        listener.moduleName,
                        listener.logger,
                        HF_ERROR_PRESENTATION_INVALID_MUTATION);
                } else {
                    a_context.bufferCount = candidate.bufferCount;
                    a_context.width = candidate.width;
                    a_context.height = candidate.height;
                    a_context.format = candidate.format;
                    a_context.swapChainFlags = candidate.swapChainFlags;
                }
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count();
            const auto calls = runtime->calls.fetch_add(1, std::memory_order_relaxed) + 1;
            if (elapsed >= static_cast<std::int64_t>(kSlowCallbackMicroseconds)) {
                const auto nowMs = SteadyMilliseconds();
                auto lastMs = runtime->lastSlowWarningMilliseconds.load(std::memory_order_relaxed);
                if (lastMs == 0 || nowMs - lastMs >= kSlowWarningCooldownMilliseconds) {
                    if (runtime->lastSlowWarningMilliseconds.compare_exchange_strong(
                            lastMs, nowMs, std::memory_order_relaxed)) {
                        Diagnostics::ReportPerformanceWarning(
                            listener.moduleName,
                            "presentation.pre_resize_buffers",
                            static_cast<std::uint64_t>(elapsed),
                            static_cast<std::uint32_t>(kSlowCallbackMicroseconds),
                            calls);
                    }
                }
            }

            if (callbackFailed) {
                (void)UnsubscribeOwned(listener.handle, listener.moduleName);
            }
            g_currentPresentationSubscription = previousHandle;
            LeaveCallback(runtime);
        }
    }

    void PresentationService::DispatchSwapChainCreate(HF_SwapChainCreateContextV1& a_context) noexcept
    {
        const auto listeners = LoadSnapshot();
        if (!listeners || listeners->empty()) {
            return;
        }

        for (const auto& listener : *listeners) {
            if (listener.kind != SubscriptionKind::SwapChainCreate || !listener.createCallback || !listener.runtime) {
                continue;
            }

            auto& runtime = listener.runtime;
            if (!runtime->enabled.load(std::memory_order_acquire)) {
                continue;
            }
            runtime->inFlight.fetch_add(1, std::memory_order_acq_rel);
            if (!runtime->enabled.load(std::memory_order_acquire)) {
                LeaveCallback(runtime);
                continue;
            }

            auto candidate = a_context;
            const auto previousHandle = g_currentPresentationSubscription;
            g_currentPresentationSubscription = listener.handle;
            ModuleContext::Scope scope{ listener.moduleName.c_str(), listener.logger };
            const auto start = std::chrono::steady_clock::now();
            bool callbackFailed = false;
            try {
                listener.createCallback(&candidate, listener.userData);
            } catch (const std::exception&) {
                callbackFailed = true;
                Diagnostics::ReportFrameworkFailureForModule(
                    listener.moduleName,
                    listener.logger,
                    HF_ERROR_PRESENTATION_CALLBACK_EXCEPTION);
            } catch (...) {
                callbackFailed = true;
                Diagnostics::ReportFrameworkFailureForModule(
                    listener.moduleName,
                    listener.logger,
                    HF_ERROR_PRESENTATION_CALLBACK_EXCEPTION);
            }

            if (!callbackFailed) {
                const auto immutableChanged =
                    candidate.structSize != a_context.structSize ||
                    candidate.flags != a_context.flags ||
                    candidate.sequence != a_context.sequence ||
                    candidate.width != a_context.width ||
                    candidate.height != a_context.height ||
                    candidate.refreshNumerator != a_context.refreshNumerator ||
                    candidate.refreshDenominator != a_context.refreshDenominator ||
                    candidate.format != a_context.format ||
                    candidate.scanlineOrdering != a_context.scanlineOrdering ||
                    candidate.scaling != a_context.scaling ||
                    candidate.bufferUsage != a_context.bufferUsage ||
                    candidate.outputWindow != a_context.outputWindow ||
                    candidate.windowed != a_context.windowed;
                const auto validSwapEffect =
                    candidate.swapEffect == HF_SWAP_EFFECT_DISCARD ||
                    candidate.swapEffect == HF_SWAP_EFFECT_SEQUENTIAL ||
                    candidate.swapEffect == HF_SWAP_EFFECT_FLIP_SEQUENTIAL ||
                    candidate.swapEffect == HF_SWAP_EFFECT_FLIP_DISCARD;
                const auto validMutable =
                    candidate.bufferCount >= 1 && candidate.bufferCount <= kMaximumBufferCount &&
                    candidate.sampleCount >= 1 && candidate.sampleCount <= kMaximumSampleCount &&
                    candidate.sampleQuality <= 0xFFFFu && validSwapEffect;

                if (immutableChanged || !validMutable) {
                    Diagnostics::ReportFrameworkFailureForModule(
                        listener.moduleName,
                        listener.logger,
                        HF_ERROR_PRESENTATION_INVALID_MUTATION);
                } else {
                    a_context.sampleCount = candidate.sampleCount;
                    a_context.sampleQuality = candidate.sampleQuality;
                    a_context.bufferCount = candidate.bufferCount;
                    a_context.swapEffect = candidate.swapEffect;
                    a_context.swapChainFlags = candidate.swapChainFlags;
                }
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count();
            const auto calls = runtime->calls.fetch_add(1, std::memory_order_relaxed) + 1;
            if (elapsed >= static_cast<std::int64_t>(kSlowCallbackMicroseconds)) {
                const auto nowMs = SteadyMilliseconds();
                auto lastMs = runtime->lastSlowWarningMilliseconds.load(std::memory_order_relaxed);
                if (lastMs == 0 || nowMs - lastMs >= kSlowWarningCooldownMilliseconds) {
                    if (runtime->lastSlowWarningMilliseconds.compare_exchange_strong(
                            lastMs, nowMs, std::memory_order_relaxed)) {
                        Diagnostics::ReportPerformanceWarning(
                            listener.moduleName,
                            "presentation.pre_swap_chain_create",
                            static_cast<std::uint64_t>(elapsed),
                            static_cast<std::uint32_t>(kSlowCallbackMicroseconds),
                            calls);
                    }
                }
            }

            if (callbackFailed) {
                (void)UnsubscribeOwned(listener.handle, listener.moduleName);
            }
            g_currentPresentationSubscription = previousHandle;
            LeaveCallback(runtime);
        }
    }

    REX::W32::HRESULT __stdcall PresentationService::CreateSwapChainThunk(
        REX::W32::IDXGIAdapter* const a_adapter,
        const REX::W32::D3D_DRIVER_TYPE a_driverType,
        const REX::W32::HMODULE a_software,
        const std::uint32_t a_flags,
        const REX::W32::D3D_FEATURE_LEVEL* const a_featureLevels,
        const std::uint32_t a_featureLevelCount,
        const std::uint32_t a_sdkVersion,
        const REX::W32::DXGI_SWAP_CHAIN_DESC* const a_swapChainDesc,
        REX::W32::IDXGISwapChain** const a_swapChain,
        REX::W32::ID3D11Device** const a_device,
        REX::W32::D3D_FEATURE_LEVEL* const a_featureLevel,
        REX::W32::ID3D11DeviceContext** const a_immediateContext) noexcept
    {
        auto& service = GetSingleton();
        const auto original = service._originalCreateSwapChain.load(std::memory_order_acquire);
        if (!original) {
            return kEFail;
        }
        if (!a_swapChainDesc) {
            return original(
                a_adapter, a_driverType, a_software, a_flags, a_featureLevels, a_featureLevelCount,
                a_sdkVersion, a_swapChainDesc, a_swapChain, a_device, a_featureLevel, a_immediateContext);
        }

        HF_SwapChainCreateContextV1 context{};
        context.structSize = sizeof(HF_SwapChainCreateContextV1);
        context.sequence = service._createSequence.fetch_add(1, std::memory_order_relaxed) + 1;
        context.width = a_swapChainDesc->bufferDesc.width;
        context.height = a_swapChainDesc->bufferDesc.height;
        context.refreshNumerator = a_swapChainDesc->bufferDesc.refreshRate.numerator;
        context.refreshDenominator = a_swapChainDesc->bufferDesc.refreshRate.denominator;
        context.format = static_cast<std::uint32_t>(a_swapChainDesc->bufferDesc.format);
        context.scanlineOrdering = static_cast<std::uint32_t>(a_swapChainDesc->bufferDesc.scanlineOrdering);
        context.scaling = static_cast<std::uint32_t>(a_swapChainDesc->bufferDesc.scaling);
        context.sampleCount = a_swapChainDesc->sampleDesc.count;
        context.sampleQuality = a_swapChainDesc->sampleDesc.quality;
        context.bufferUsage = static_cast<std::uint32_t>(a_swapChainDesc->bufferUsage);
        context.bufferCount = a_swapChainDesc->bufferCount;
        context.outputWindow = static_cast<HF_NativeHandle>(reinterpret_cast<std::uintptr_t>(a_swapChainDesc->outputWindow));
        context.windowed = a_swapChainDesc->windowed != 0 ? HF_TRUE : HF_FALSE;
        context.swapEffect = static_cast<std::uint32_t>(a_swapChainDesc->swapEffect);
        context.swapChainFlags = a_swapChainDesc->flags;
        if (context.windowed != HF_FALSE) {
            context.flags |= HF_SWAP_CHAIN_CREATE_CONTEXT_WINDOWED;
        }

        PresentationPolicyService::GetSingleton().ApplySwapChainCreatePolicy(context);
        service.DispatchSwapChainCreate(context);
        PresentationPolicyService::GetSingleton().ApplySwapChainCreatePolicy(context);

        const auto changed =
            context.sampleCount != a_swapChainDesc->sampleDesc.count ||
            context.sampleQuality != a_swapChainDesc->sampleDesc.quality ||
            context.bufferCount != a_swapChainDesc->bufferCount ||
            context.swapEffect != static_cast<std::uint32_t>(a_swapChainDesc->swapEffect) ||
            context.swapChainFlags != a_swapChainDesc->flags;
        if (!changed) {
            return original(
                a_adapter, a_driverType, a_software, a_flags, a_featureLevels, a_featureLevelCount,
                a_sdkVersion, a_swapChainDesc, a_swapChain, a_device, a_featureLevel, a_immediateContext);
        }

        auto modified = *a_swapChainDesc;
        modified.sampleDesc.count = context.sampleCount;
        modified.sampleDesc.quality = context.sampleQuality;
        modified.bufferCount = context.bufferCount;
        modified.swapEffect = static_cast<REX::W32::DXGI_SWAP_EFFECT>(context.swapEffect);
        modified.flags = context.swapChainFlags;

        REX::W32::IDXGISwapChain* tempSwapChain = nullptr;
        REX::W32::ID3D11Device* tempDevice = nullptr;
        REX::W32::D3D_FEATURE_LEVEL tempFeatureLevel{};
        REX::W32::ID3D11DeviceContext* tempContext = nullptr;
        const auto result = original(
            a_adapter,
            a_driverType,
            a_software,
            a_flags,
            a_featureLevels,
            a_featureLevelCount,
            a_sdkVersion,
            std::addressof(modified),
            a_swapChain ? std::addressof(tempSwapChain) : nullptr,
            a_device ? std::addressof(tempDevice) : nullptr,
            a_featureLevel ? std::addressof(tempFeatureLevel) : nullptr,
            a_immediateContext ? std::addressof(tempContext) : nullptr);

        if (result >= 0) {
            if (a_swapChain) *a_swapChain = tempSwapChain;
            if (a_device) *a_device = tempDevice;
            if (a_featureLevel) *a_featureLevel = tempFeatureLevel;
            if (a_immediateContext) *a_immediateContext = tempContext;
            PresentationPolicyService::GetSingleton().ObserveSwapChain(
                tempSwapChain ? static_cast<HF_NativeHandle>(reinterpret_cast<std::uintptr_t>(tempSwapChain)) : 0);
            return result;
        }

        if (tempContext) tempContext->Release();
        if (tempDevice) tempDevice->Release();
        if (tempSwapChain) tempSwapChain->Release();
        Diagnostics::ReportFrameworkWarningForModule(
            "HolyFramework",
            HF_INVALID_LOG_HANDLE,
            HF_ERROR_PRESENTATION_POLICY_FALLBACK);

        const auto fallback = original(
            a_adapter, a_driverType, a_software, a_flags, a_featureLevels, a_featureLevelCount,
            a_sdkVersion, a_swapChainDesc, a_swapChain, a_device, a_featureLevel, a_immediateContext);
        if (fallback >= 0 && a_swapChain && *a_swapChain) {
            PresentationPolicyService::GetSingleton().ObserveSwapChain(
                static_cast<HF_NativeHandle>(reinterpret_cast<std::uintptr_t>(*a_swapChain)));
        }
        return fallback;
    }

    REX::W32::HRESULT __stdcall PresentationService::PresentThunk(
        REX::W32::IDXGISwapChain* const a_swapChain,
        const std::uint32_t a_syncInterval,
        const std::uint32_t a_flags) noexcept
    {
        auto& service = GetSingleton();
        const auto original = service._originalPresent.load(std::memory_order_acquire);
        if (!original) {
            return kEFail;
        }

        HF_PresentContextV1 context{};
        context.structSize = sizeof(HF_PresentContextV1);
        context.sequence = service._presentSequence.fetch_add(1, std::memory_order_relaxed) + 1;
        context.sessionGeneration = RuntimeState::GetSingleton().GetSessionGeneration();
        context.swapChain = static_cast<HF_NativeHandle>(reinterpret_cast<std::uintptr_t>(a_swapChain));
        context.syncInterval = a_syncInterval;
        context.presentFlags = a_flags;

        const auto runtimeFlags = RuntimeState::GetSingleton().GetFlags();
        if ((runtimeFlags & HF_RUNTIME_STATE_SESSION_ACTIVE) != 0) {
            context.flags |= HF_PRESENTATION_CONTEXT_SESSION_ACTIVE;
        }
        if ((runtimeFlags & HF_RUNTIME_STATE_SESSION_READY) != 0) {
            context.flags |= HF_PRESENTATION_CONTEXT_SESSION_READY;
        }
        if ((a_flags & HF_PRESENT_FLAG_TEST) != 0) {
            context.flags |= HF_PRESENTATION_CONTEXT_TEST_PRESENT;
        }

        if ((a_flags & HF_PRESENT_FLAG_TEST) == 0) {
            const auto maintenance = service._frameworkPresentMaintenanceFlags.load(std::memory_order_acquire);
            if ((maintenance & static_cast<std::uint32_t>(FrameworkPresentMaintenanceReason::LoadingMenu)) != 0) {
                UIStateService::GetSingleton().MaintainLoadingMenuPolicyOnPresent();
            }
            if ((maintenance & static_cast<std::uint32_t>(FrameworkPresentMaintenanceReason::StateFPS)) != 0) {
                StateFPSService::GetSingleton().MaintainOnPresent();
            }
            if ((maintenance & static_cast<std::uint32_t>(FrameworkPresentMaintenanceReason::RuntimeTuning)) != 0) {
                RuntimeTuningService::GetSingleton().MaintainOnPresent();
            }
            if ((maintenance & static_cast<std::uint32_t>(FrameworkPresentMaintenanceReason::CPUScheduling)) != 0) {
                CPUSchedulingService::GetSingleton().MaintainOnPresent();
            }
            if ((maintenance & static_cast<std::uint32_t>(FrameworkPresentMaintenanceReason::FramePacing)) != 0) {
                FramePacingService::GetSingleton().MaintainOnPresent();
            }
            if ((maintenance & static_cast<std::uint32_t>(FrameworkPresentMaintenanceReason::WindowCursor)) != 0) {
                WindowService::GetSingleton().MaintainOnPresent();
            }
        }
        PresentationPolicyService::GetSingleton().ApplyPresentPolicy(context);
        service.DispatchPresent(context);
        PresentationPolicyService::GetSingleton().ApplyPresentPolicy(context);
        return original(a_swapChain, context.syncInterval, context.presentFlags);
    }

    REX::W32::HRESULT __stdcall PresentationService::ResizeBuffersThunk(
        REX::W32::IDXGISwapChain* const a_swapChain,
        const std::uint32_t a_bufferCount,
        const std::uint32_t a_width,
        const std::uint32_t a_height,
        const REX::W32::DXGI_FORMAT a_newFormat,
        const std::uint32_t a_swapChainFlags) noexcept
    {
        auto& service = GetSingleton();
        const auto original = service._originalResizeBuffers.load(std::memory_order_acquire);
        if (!original) {
            return kEFail;
        }

        HF_ResizeBuffersContextV1 context{};
        context.structSize = sizeof(HF_ResizeBuffersContextV1);
        context.sequence = service._resizeSequence.fetch_add(1, std::memory_order_relaxed) + 1;
        context.sessionGeneration = RuntimeState::GetSingleton().GetSessionGeneration();
        context.swapChain = static_cast<HF_NativeHandle>(reinterpret_cast<std::uintptr_t>(a_swapChain));
        context.bufferCount = a_bufferCount;
        context.width = a_width;
        context.height = a_height;
        context.format = static_cast<std::uint32_t>(a_newFormat);
        context.swapChainFlags = a_swapChainFlags;

        const auto runtimeFlags = RuntimeState::GetSingleton().GetFlags();
        if ((runtimeFlags & HF_RUNTIME_STATE_SESSION_ACTIVE) != 0) {
            context.flags |= HF_PRESENTATION_CONTEXT_SESSION_ACTIVE;
        }
        if ((runtimeFlags & HF_RUNTIME_STATE_SESSION_READY) != 0) {
            context.flags |= HF_PRESENTATION_CONTEXT_SESSION_READY;
        }

        PresentationPolicyService::GetSingleton().ApplyResizeBuffersPolicy(context);
        service.DispatchResizeBuffers(context);
        PresentationPolicyService::GetSingleton().ApplyResizeBuffersPolicy(context);
        const auto changed =
            context.bufferCount != a_bufferCount ||
            context.width != a_width ||
            context.height != a_height ||
            context.format != static_cast<std::uint32_t>(a_newFormat) ||
            context.swapChainFlags != a_swapChainFlags;

        const auto result = original(
            a_swapChain,
            context.bufferCount,
            context.width,
            context.height,
            static_cast<REX::W32::DXGI_FORMAT>(context.format),
            context.swapChainFlags);
        if (result >= 0 || !changed) {
            if (result >= 0) {
                PresentationPolicyService::GetSingleton().ObserveSwapChain(
                    static_cast<HF_NativeHandle>(reinterpret_cast<std::uintptr_t>(a_swapChain)));
            }
            return result;
        }

        Diagnostics::ReportFrameworkWarningForModule(
            "HolyFramework",
            HF_INVALID_LOG_HANDLE,
            HF_ERROR_PRESENTATION_POLICY_FALLBACK);
        const auto fallback = original(
            a_swapChain,
            a_bufferCount,
            a_width,
            a_height,
            a_newFormat,
            a_swapChainFlags);
        if (fallback >= 0) {
            PresentationPolicyService::GetSingleton().ObserveSwapChain(
                static_cast<HF_NativeHandle>(reinterpret_cast<std::uintptr_t>(a_swapChain)));
        }
        return fallback;
    }
}
