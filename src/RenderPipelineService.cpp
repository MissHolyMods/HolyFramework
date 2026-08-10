#include "pch.h"

#include <Windows.h>
#ifdef ERROR
#  undef ERROR
#endif
#include "RenderPipelineService.h"

#include "Diagnostics.h"
#include "GraphicsService.h"
#include "ModuleContext.h"
#include "RuntimeState.h"

namespace HolyFramework
{
    namespace
    {
        inline constexpr std::size_t kHDRRenderVTableIndex = 1;
        inline constexpr std::uint64_t kSlowCallbackMicroseconds = 16'000;
        inline constexpr std::int64_t kSlowWarningCooldownMilliseconds = 30'000;
        thread_local HF_RenderSubscriptionHandle g_currentRenderSubscription = HF_INVALID_RENDER_SUBSCRIPTION_HANDLE;
    }

    RenderPipelineService& RenderPipelineService::GetSingleton() noexcept
    {
        static RenderPipelineService* instance = new RenderPipelineService();
        return *instance;
    }

    bool RenderPipelineService::IsStageSupported(const HF_RenderStage a_stage) const noexcept
    {
        return a_stage == HF_RENDER_STAGE_POST_HDR_WORLD &&
               !_compromised.load(std::memory_order_acquire);
    }

    bool RenderPipelineService::NamesEqual(
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

    std::int64_t RenderPipelineService::SteadyMilliseconds() noexcept
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void RenderPipelineService::WaitForQuiescence(
        const std::shared_ptr<SubscriptionRuntime>& a_runtime,
        const HF_RenderSubscriptionHandle a_handle) noexcept
    {
        if (!a_runtime || g_currentRenderSubscription == a_handle) {
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

    void RenderPipelineService::LeaveCallback(
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

    void RenderPipelineService::PublishSnapshot(
        std::shared_ptr<const SubscriptionList> a_snapshot) noexcept
    {
        _subscriptions.store(std::move(a_snapshot), std::memory_order_release);
    }

    std::shared_ptr<const RenderPipelineService::SubscriptionList>
    RenderPipelineService::LoadSnapshot() const noexcept
    {
        return _subscriptions.load(std::memory_order_acquire);
    }

    bool RenderPipelineService::EnsureHookInstalled() noexcept
    {
        if (_compromised.load(std::memory_order_acquire)) {
            return false;
        }
        if (_installed.load(std::memory_order_acquire)) {
            return true;
        }

        std::scoped_lock lock{ _installLock };
        if (_installed.load(std::memory_order_relaxed)) {
            return true;
        }

        try {
            REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE::ImageSpaceEffectHDR[0] };
            const auto vtableAddress = vtable.address();
            if (vtableAddress == 0) {
                Diagnostics::ReportFrameworkWarningForModule(
                    "HolyFramework",
                    HF_INVALID_LOG_HANDLE,
                    HF_ERROR_RENDER_STAGE_HOOK_UNAVAILABLE);
                return false;
            }

            auto* const slot = reinterpret_cast<std::uintptr_t*>(
                vtableAddress + sizeof(void*) * kHDRRenderVTableIndex);
            const auto originalAddress = *slot;
            if (originalAddress == 0) {
                Diagnostics::ReportFrameworkWarningForModule(
                    "HolyFramework",
                    HF_INVALID_LOG_HANDLE,
                    HF_ERROR_RENDER_STAGE_HOOK_UNAVAILABLE);
                return false;
            }

            const auto thunkAddress = reinterpret_cast<std::uintptr_t>(&RenderThunk);
            if (originalAddress == thunkAddress) {
                // A duplicate installation request after an unexpected state change
                // is harmless if the framework thunk is already in the slot.
                if (_originalRender.load(std::memory_order_acquire) != nullptr) {
                    _slotAddress.store(reinterpret_cast<std::uintptr_t>(slot), std::memory_order_release);
                    _installed.store(true, std::memory_order_release);
                    return true;
                }
                Diagnostics::ReportFrameworkFailureForModule(
                    "HolyFramework",
                    HF_INVALID_LOG_HANDLE,
                    HF_ERROR_RENDER_STAGE_HOOK_INSTALL_FAILED);
                return false;
            }

            // Publish the original target before replacing the vtable slot so a
            // render-thread call can never observe the thunk without a chain target.
            _originalRender.store(
                reinterpret_cast<Render_t>(originalAddress),
                std::memory_order_release);

            vtable.write_vfunc(kHDRRenderVTableIndex, &RenderThunk);
            if (*slot != thunkAddress) {
                _originalRender.store(nullptr, std::memory_order_release);
                Diagnostics::ReportFrameworkFailureForModule(
                    "HolyFramework",
                    HF_INVALID_LOG_HANDLE,
                    HF_ERROR_RENDER_STAGE_HOOK_INSTALL_FAILED);
                return false;
            }

            _slotAddress.store(reinterpret_cast<std::uintptr_t>(slot), std::memory_order_release);
            _installed.store(true, std::memory_order_release);
            REX::INFO("HolyFramework render stage observer installed: post-hdr-world");
            return true;
        } catch (const std::exception&) {
            _originalRender.store(nullptr, std::memory_order_release);
            Diagnostics::ReportFrameworkFailureForModule(
                "HolyFramework",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_RENDER_STAGE_HOOK_INSTALL_FAILED);
            return false;
        } catch (...) {
            _originalRender.store(nullptr, std::memory_order_release);
            Diagnostics::ReportFrameworkFailureForModule(
                "HolyFramework",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_RENDER_STAGE_HOOK_INSTALL_FAILED);
            return false;
        }
    }

    bool RenderPipelineService::AuditHook() noexcept
    {
        if (!_installed.load(std::memory_order_acquire)) {
            return true;
        }
        if (_compromised.load(std::memory_order_acquire)) {
            return false;
        }

        const auto slotAddress = _slotAddress.load(std::memory_order_acquire);
        if (slotAddress == 0) {
            return true;
        }

        std::uintptr_t currentTarget = 0;
        SIZE_T bytesRead = 0;
        const auto ok = ::ReadProcessMemory(
            ::GetCurrentProcess(),
            reinterpret_cast<const void*>(slotAddress),
            &currentTarget,
            sizeof(currentTarget),
            &bytesRead);
        if (ok == FALSE || bytesRead != sizeof(currentTarget)) {
            return true;
        }

        const auto expected = reinterpret_cast<std::uintptr_t>(&RenderThunk);
        if (currentTarget == expected) {
            return true;
        }

        bool expectedCompromised = false;
        if (_compromised.compare_exchange_strong(
                expectedCompromised, true, std::memory_order_acq_rel)) {
            Diagnostics::ReportFrameworkWarningForModule(
                "HolyFramework",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_RENDER_STAGE_HOOK_CHANGED_EXTERNALLY);
        }
        return false;
    }

    HF_RenderSubscriptionHandle RenderPipelineService::SubscribeOwned(
        const HF_RenderStage a_stage,
        const std::int32_t a_priority,
        const HF_RenderStageCallback a_callback,
        void* const a_userData,
        const std::string_view a_moduleName,
        const HF_LogHandle a_logger) noexcept
    {
        if (!IsStageSupported(a_stage) || !a_callback || a_moduleName.empty()) {
            return HF_INVALID_RENDER_SUBSCRIPTION_HANDLE;
        }
        if (!EnsureHookInstalled()) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName,
                a_logger,
                HF_ERROR_RENDER_STAGE_HOOK_UNAVAILABLE);
            return HF_INVALID_RENDER_SUBSCRIPTION_HANDLE;
        }

        auto handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
        if (handle == HF_INVALID_RENDER_SUBSCRIPTION_HANDLE) {
            handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
        }

        try {
            auto runtime = std::make_shared<SubscriptionRuntime>();
            std::scoped_lock lock{ _subscriptionLock };
            const auto current = LoadSnapshot();
            auto next = std::make_shared<SubscriptionList>(current ? *current : SubscriptionList{});
            next->push_back(Subscription{
                .handle = handle,
                .stage = a_stage,
                .priority = a_priority,
                .callback = a_callback,
                .userData = a_userData,
                .moduleName = std::string{ a_moduleName },
                .logger = a_logger,
                .runtime = std::move(runtime)
            });
            std::stable_sort(next->begin(), next->end(), [](const Subscription& left, const Subscription& right) {
                if (left.stage != right.stage) {
                    return static_cast<std::uint32_t>(left.stage) < static_cast<std::uint32_t>(right.stage);
                }
                return left.priority < right.priority;
            });
            PublishSnapshot(next);
            return handle;
        } catch (...) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName,
                a_logger,
                HF_ERROR_RENDER_STAGE_SUBSCRIBE_FAILED);
            return HF_INVALID_RENDER_SUBSCRIPTION_HANDLE;
        }
    }

    bool RenderPipelineService::UnsubscribeOwned(
        const HF_RenderSubscriptionHandle a_handle,
        const std::string_view a_moduleName,
        std::string* const a_outActualOwner) noexcept
    {
        if (a_handle == HF_INVALID_RENDER_SUBSCRIPTION_HANDLE || a_moduleName.empty()) {
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

    std::uint32_t RenderPipelineService::UnsubscribeOwnedBy(
        const std::string_view a_moduleName) noexcept
    {
        if (a_moduleName.empty()) {
            return 0;
        }

        std::vector<std::pair<HF_RenderSubscriptionHandle, std::shared_ptr<SubscriptionRuntime>>> retired;
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

    void RenderPipelineService::RenderThunk(
        RE::ImageSpaceEffectHDR* const a_self,
        RE::BSTriShape* const a_geometry,
        RE::ImageSpaceEffectParam* const a_param) noexcept
    {
        auto& service = GetSingleton();
        const auto original = service._originalRender.load(std::memory_order_acquire);
        if (!original) {
            return;
        }

        original(a_self, a_geometry, a_param);
        service.Dispatch(HF_RENDER_STAGE_POST_HDR_WORLD);
    }

    void RenderPipelineService::Dispatch(const HF_RenderStage a_stage) noexcept
    {
        const auto listeners = LoadSnapshot();
        if (!listeners || listeners->empty()) {
            return;
        }

        HF_RenderStageContextV1 context{};
        context.structSize = sizeof(HF_RenderStageContextV1);
        context.stage = a_stage;
        context.sequence = _sequence.fetch_add(1, std::memory_order_relaxed) + 1;
        context.sessionGeneration = RuntimeState::GetSingleton().GetSessionGeneration();

        const auto runtimeFlags = RuntimeState::GetSingleton().GetFlags();
        if ((runtimeFlags & HF_RUNTIME_STATE_SESSION_ACTIVE) != 0) {
            context.flags |= HF_RENDER_CONTEXT_SESSION_ACTIVE;
        }
        if ((runtimeFlags & HF_RUNTIME_STATE_SESSION_READY) != 0) {
            context.flags |= HF_RENDER_CONTEXT_SESSION_READY;
        }

        HF_GraphicsNativeHandlesV1 handles{};
        if (GraphicsService::GetSingleton().GetNativeHandles(handles)) {
            context.windowHandle = handles.windowHandle;
            context.swapChain = handles.swapChain;
            context.device = handles.device;
            context.immediateContext = handles.immediateContext;
            if (context.windowHandle != 0) context.flags |= HF_RENDER_CONTEXT_WINDOW_AVAILABLE;
            if (context.swapChain != 0) context.flags |= HF_RENDER_CONTEXT_SWAP_CHAIN_AVAILABLE;
            if (context.device != 0) context.flags |= HF_RENDER_CONTEXT_DEVICE_AVAILABLE;
            if (context.immediateContext != 0) context.flags |= HF_RENDER_CONTEXT_CONTEXT_AVAILABLE;
        }

        for (const auto& listener : *listeners) {
            if (listener.stage != a_stage || !listener.callback || !listener.runtime) {
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

            const auto previousSubscription = g_currentRenderSubscription;
            g_currentRenderSubscription = listener.handle;
            ModuleContext::Scope scope{ listener.moduleName.c_str(), listener.logger };
            const auto start = std::chrono::steady_clock::now();
            bool callbackFailed = false;
            try {
                listener.callback(&context, listener.userData);
            } catch (const std::exception&) {
                callbackFailed = true;
                Diagnostics::ReportFrameworkFailureForModule(
                    listener.moduleName,
                    listener.logger,
                    HF_ERROR_RENDER_STAGE_CALLBACK_EXCEPTION);
            } catch (...) {
                callbackFailed = true;
                Diagnostics::ReportFrameworkFailureForModule(
                    listener.moduleName,
                    listener.logger,
                    HF_ERROR_RENDER_STAGE_CALLBACK_EXCEPTION);
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
                            "render.post_hdr_world",
                            static_cast<std::uint64_t>(elapsed),
                            static_cast<std::uint32_t>(kSlowCallbackMicroseconds),
                            calls);
                    }
                }
            }

            if (callbackFailed) {
                UnsubscribeOwned(listener.handle, listener.moduleName);
            }
            g_currentRenderSubscription = previousSubscription;
            LeaveCallback(runtime);
        }
    }
}
