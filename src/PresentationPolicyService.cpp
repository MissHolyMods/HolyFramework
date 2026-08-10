#include "pch.h"
#include "PresentationPolicyService.h"

#include "PresentationService.h"

namespace HolyFramework
{
    namespace
    {
        inline constexpr std::uint32_t kSupportedRequestFlags =
            HF_PRESENTATION_POLICY_REQUEST_SYNC_INTERVAL |
            HF_PRESENTATION_POLICY_REQUEST_ALLOW_TEARING |
            HF_PRESENTATION_POLICY_REQUEST_PREFER_FLIP_MODEL |
            HF_PRESENTATION_POLICY_REQUEST_BUFFER_COUNT;

        enum ObservedSwapChainFlags : std::uint32_t
        {
            ObservedNone = 0,
            ObservedFlipModel = 1u << 0,
            ObservedAllowTearing = 1u << 1
        };
    }

    PresentationPolicyService& PresentationPolicyService::GetSingleton() noexcept
    {
        static PresentationPolicyService* instance = new PresentationPolicyService();
        return *instance;
    }

    bool PresentationPolicyService::NamesEqualInsensitive(
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

    bool PresentationPolicyService::ValidateRequest(
        const HF_PresentationPolicyRequestV1& a_request) noexcept
    {
        if (a_request.structSize != sizeof(HF_PresentationPolicyRequestV1) ||
            (a_request.flags & ~kSupportedRequestFlags) != 0 ||
            a_request.reserved0 != 0 || a_request.reserved1 != 0) {
            return false;
        }
        if ((a_request.flags & HF_PRESENTATION_POLICY_REQUEST_SYNC_INTERVAL) != 0 &&
            a_request.syncInterval > 4) {
            return false;
        }
        if ((a_request.flags & HF_PRESENTATION_POLICY_REQUEST_BUFFER_COUNT) != 0 &&
            (a_request.bufferCount < HF_PRESENTATION_POLICY_MIN_BUFFER_COUNT ||
             a_request.bufferCount > HF_PRESENTATION_POLICY_MAX_BUFFER_COUNT)) {
            return false;
        }
        if ((a_request.flags & HF_PRESENTATION_POLICY_REQUEST_ALLOW_TEARING) != 0 &&
            a_request.allowTearing != HF_FALSE && a_request.allowTearing != HF_TRUE) {
            return false;
        }
        return true;
    }

    bool PresentationPolicyService::IsFlipModel(const std::uint32_t a_swapEffect) noexcept
    {
        return a_swapEffect == HF_SWAP_EFFECT_FLIP_SEQUENTIAL ||
            a_swapEffect == HF_SWAP_EFFECT_FLIP_DISCARD;
    }

    bool PresentationPolicyService::WinsScalar(
        const PolicyRecord& a_candidate,
        const PolicyRecord* const a_current) const noexcept
    {
        if (!a_current) {
            return true;
        }
        if (a_candidate.request.priority != a_current->request.priority) {
            return a_candidate.request.priority > a_current->request.priority;
        }
        return a_candidate.handle < a_current->handle;
    }

    void PresentationPolicyService::UpdateHookDemandLocked() noexcept
    {
        const auto flags = _effectiveFlags.load(std::memory_order_relaxed);
        const auto needsVTable = (flags & (EffectiveSyncInterval | EffectiveAllowTearing | EffectiveBufferCount)) != 0;
        const auto needsCreate = (flags & (EffectiveAllowTearing | EffectivePreferFlipModel | EffectiveBufferCount)) != 0;
        auto& presentation = PresentationService::GetSingleton();
        presentation.SetFrameworkPresentMaintenance(
            FrameworkPresentMaintenanceReason::PresentationPolicy,
            needsVTable);
        presentation.SetFrameworkSwapChainCreatePolicy(needsCreate);
    }

    void PresentationPolicyService::RecomputeEffectiveLocked() noexcept
    {
        const PolicyRecord* syncWinner = nullptr;
        const PolicyRecord* tearingWinner = nullptr;
        const PolicyRecord* bufferWinner = nullptr;
        bool preferFlip = false;

        for (const auto& policy : _policies) {
            const auto flags = policy.request.flags;
            if ((flags & HF_PRESENTATION_POLICY_REQUEST_SYNC_INTERVAL) != 0 &&
                WinsScalar(policy, syncWinner)) {
                syncWinner = std::addressof(policy);
            }
            if ((flags & HF_PRESENTATION_POLICY_REQUEST_ALLOW_TEARING) != 0 &&
                WinsScalar(policy, tearingWinner)) {
                tearingWinner = std::addressof(policy);
            }
            if ((flags & HF_PRESENTATION_POLICY_REQUEST_BUFFER_COUNT) != 0 &&
                WinsScalar(policy, bufferWinner)) {
                bufferWinner = std::addressof(policy);
            }
            if ((flags & HF_PRESENTATION_POLICY_REQUEST_PREFER_FLIP_MODEL) != 0) {
                preferFlip = true;
            }
        }

        std::uint32_t effectiveFlags = EffectiveNone;
        std::uint32_t syncInterval = 0;
        std::uint32_t bufferCount = 0;
        std::uint32_t allowTearing = 0;
        if (syncWinner) {
            effectiveFlags |= EffectiveSyncInterval;
            syncInterval = syncWinner->request.syncInterval;
        }
        if (tearingWinner) {
            effectiveFlags |= EffectiveAllowTearing;
            allowTearing = tearingWinner->request.allowTearing != HF_FALSE ? 1u : 0u;
        }
        if (bufferWinner) {
            effectiveFlags |= EffectiveBufferCount;
            bufferCount = bufferWinner->request.bufferCount;
        }
        if (preferFlip) {
            effectiveFlags |= EffectivePreferFlipModel;
        }

        const auto previousFlags = _effectiveFlags.load(std::memory_order_relaxed);
        const auto previousSync = _effectiveSyncInterval.load(std::memory_order_relaxed);
        const auto previousBuffer = _effectiveBufferCount.load(std::memory_order_relaxed);
        const auto previousTearing = _effectiveAllowTearing.load(std::memory_order_relaxed);
        const auto changed = previousFlags != effectiveFlags ||
            previousSync != syncInterval || previousBuffer != bufferCount ||
            previousTearing != allowTearing;

        _requestCount.store(static_cast<std::uint32_t>(
            std::min<std::size_t>(_policies.size(), std::numeric_limits<std::uint32_t>::max())),
            std::memory_order_release);
        _effectiveSyncInterval.store(syncInterval, std::memory_order_release);
        _effectiveBufferCount.store(bufferCount, std::memory_order_release);
        _effectiveAllowTearing.store(allowTearing, std::memory_order_release);
        _effectiveFlags.store(effectiveFlags, std::memory_order_release);
        if (changed) {
            _generation.fetch_add(1, std::memory_order_acq_rel);
        }
        UpdateHookDemandLocked();
    }

    bool PresentationPolicyService::IsAvailable() noexcept
    {
        return PresentationService::GetSingleton().IsAvailable();
    }

    bool PresentationPolicyService::GetState(HF_PresentationPolicyStateV1& a_outState) noexcept
    {
        a_outState = {};
        a_outState.structSize = sizeof(HF_PresentationPolicyStateV1);
        if (IsAvailable()) {
            a_outState.flags |= HF_PRESENTATION_POLICY_STATE_AVAILABLE;
        }

        const auto effective = _effectiveFlags.load(std::memory_order_acquire);
        a_outState.requestCount = _requestCount.load(std::memory_order_acquire);
        a_outState.syncInterval = _effectiveSyncInterval.load(std::memory_order_acquire);
        a_outState.bufferCount = _effectiveBufferCount.load(std::memory_order_acquire);
        a_outState.allowTearing = _effectiveAllowTearing.load(std::memory_order_acquire) != 0 ? HF_TRUE : HF_FALSE;
        a_outState.preferFlipModel = (effective & EffectivePreferFlipModel) != 0 ? HF_TRUE : HF_FALSE;
        a_outState.generation = _generation.load(std::memory_order_acquire);

        if (effective != 0) a_outState.flags |= HF_PRESENTATION_POLICY_STATE_ACTIVE;
        if ((effective & EffectiveSyncInterval) != 0) a_outState.flags |= HF_PRESENTATION_POLICY_STATE_SYNC_INTERVAL_OVERRIDE;
        if ((effective & EffectiveAllowTearing) != 0) {
            a_outState.flags |= HF_PRESENTATION_POLICY_STATE_ALLOW_TEARING_OVERRIDE;
            if (a_outState.allowTearing != HF_FALSE) {
                a_outState.flags |= HF_PRESENTATION_POLICY_STATE_ALLOW_TEARING;
            }
        }
        if ((effective & EffectivePreferFlipModel) != 0) a_outState.flags |= HF_PRESENTATION_POLICY_STATE_PREFER_FLIP_MODEL;
        if ((effective & EffectiveBufferCount) != 0) a_outState.flags |= HF_PRESENTATION_POLICY_STATE_BUFFER_COUNT_OVERRIDE;

        const auto observed = _swapChainFlags.load(std::memory_order_acquire);
        if ((observed & ObservedFlipModel) != 0) a_outState.flags |= HF_PRESENTATION_POLICY_STATE_SWAP_CHAIN_FLIP_MODEL;
        if ((observed & ObservedAllowTearing) != 0) a_outState.flags |= HF_PRESENTATION_POLICY_STATE_SWAP_CHAIN_ALLOW_TEARING;
        return (a_outState.flags & HF_PRESENTATION_POLICY_STATE_AVAILABLE) != 0;
    }

    HF_PresentationPolicyHandle PresentationPolicyService::AcquirePolicyOwned(
        const HF_PresentationPolicyRequestV1& a_request,
        const std::string_view a_moduleName,
        const HF_LogHandle a_logger) noexcept
    {
        if (a_moduleName.empty() || !ValidateRequest(a_request)) {
            return HF_INVALID_PRESENTATION_POLICY_HANDLE;
        }
        try {
            auto handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
            if (handle == HF_INVALID_PRESENTATION_POLICY_HANDLE) {
                handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
            }
            std::scoped_lock lock{ _lock };
            _policies.push_back(PolicyRecord{
                .handle = handle,
                .request = a_request,
                .moduleName = std::string{ a_moduleName },
                .logger = a_logger
            });
            RecomputeEffectiveLocked();
            return handle;
        } catch (...) {
            return HF_INVALID_PRESENTATION_POLICY_HANDLE;
        }
    }

    bool PresentationPolicyService::UpdatePolicyOwned(
        const HF_PresentationPolicyHandle a_handle,
        const HF_PresentationPolicyRequestV1& a_request,
        const std::string_view a_moduleName,
        std::string* const a_outActualOwner) noexcept
    {
        if (a_outActualOwner) a_outActualOwner->clear();
        if (a_handle == HF_INVALID_PRESENTATION_POLICY_HANDLE || a_moduleName.empty() || !ValidateRequest(a_request)) {
            return false;
        }
        std::scoped_lock lock{ _lock };
        const auto it = std::ranges::find_if(_policies, [a_handle](const PolicyRecord& record) {
            return record.handle == a_handle;
        });
        if (it == _policies.end()) return false;
        if (!NamesEqualInsensitive(it->moduleName, a_moduleName)) {
            if (a_outActualOwner) *a_outActualOwner = it->moduleName;
            return false;
        }
        it->request = a_request;
        RecomputeEffectiveLocked();
        return true;
    }

    bool PresentationPolicyService::ReleasePolicyOwned(
        const HF_PresentationPolicyHandle a_handle,
        const std::string_view a_moduleName,
        std::string* const a_outActualOwner) noexcept
    {
        if (a_outActualOwner) a_outActualOwner->clear();
        if (a_handle == HF_INVALID_PRESENTATION_POLICY_HANDLE || a_moduleName.empty()) return false;
        std::scoped_lock lock{ _lock };
        const auto it = std::ranges::find_if(_policies, [a_handle](const PolicyRecord& record) {
            return record.handle == a_handle;
        });
        if (it == _policies.end()) return false;
        if (!NamesEqualInsensitive(it->moduleName, a_moduleName)) {
            if (a_outActualOwner) *a_outActualOwner = it->moduleName;
            return false;
        }
        _policies.erase(it);
        RecomputeEffectiveLocked();
        return true;
    }

    std::uint32_t PresentationPolicyService::ReleaseOwnedBy(const std::string_view a_moduleName) noexcept
    {
        if (a_moduleName.empty()) return 0;
        std::scoped_lock lock{ _lock };
        const auto before = _policies.size();
        std::erase_if(_policies, [&](const PolicyRecord& record) {
            return NamesEqualInsensitive(record.moduleName, a_moduleName);
        });
        const auto removed = before - _policies.size();
        if (removed != 0) RecomputeEffectiveLocked();
        return static_cast<std::uint32_t>(
            std::min<std::size_t>(removed, std::numeric_limits<std::uint32_t>::max()));
    }

    void PresentationPolicyService::ApplyPresentPolicy(HF_PresentContextV1& a_context) noexcept
    {
        if ((a_context.flags & HF_PRESENTATION_CONTEXT_TEST_PRESENT) != 0) return;
        if (a_context.swapChain != 0 &&
            _observedSwapChainHandle.load(std::memory_order_acquire) != a_context.swapChain) {
            ObserveSwapChain(a_context.swapChain);
        }
        const auto effective = _effectiveFlags.load(std::memory_order_acquire);
        if ((effective & EffectiveSyncInterval) != 0) {
            a_context.syncInterval = _effectiveSyncInterval.load(std::memory_order_acquire);
        }
        if ((effective & EffectiveAllowTearing) == 0) return;

        const auto tearingFlag = HF_PRESENT_FLAG_ALLOW_TEARING;
        const auto allow = _effectiveAllowTearing.load(std::memory_order_acquire) != 0;
        const auto observed = _swapChainFlags.load(std::memory_order_acquire);
        if (allow && a_context.syncInterval == 0 && (observed & ObservedAllowTearing) != 0) {
            a_context.presentFlags |= tearingFlag;
        } else {
            a_context.presentFlags &= ~tearingFlag;
        }
    }

    void PresentationPolicyService::ApplyResizeBuffersPolicy(HF_ResizeBuffersContextV1& a_context) noexcept
    {
        const auto effective = _effectiveFlags.load(std::memory_order_acquire);
        if ((effective & (EffectiveBufferCount | EffectiveAllowTearing)) == 0 || a_context.swapChain == 0) return;

        auto* const swapChain = reinterpret_cast<REX::W32::IDXGISwapChain*>(
            static_cast<std::uintptr_t>(a_context.swapChain));
        REX::W32::DXGI_SWAP_CHAIN_DESC current{};
        if (!swapChain || swapChain->GetDesc(std::addressof(current)) < 0) return;

        const auto flip = IsFlipModel(static_cast<std::uint32_t>(current.swapEffect));
        if (flip && (effective & EffectiveBufferCount) != 0) {
            a_context.bufferCount = _effectiveBufferCount.load(std::memory_order_acquire);
        }
        // DXGI requires ALLOW_TEARING to be preserved by ResizeBuffers when the
        // swap chain was created with it. It cannot be enabled retroactively.
        if ((current.flags & HF_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0) {
            a_context.swapChainFlags |= HF_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        }
    }

    void PresentationPolicyService::ApplySwapChainCreatePolicy(HF_SwapChainCreateContextV1& a_context) noexcept
    {
        const auto effective = _effectiveFlags.load(std::memory_order_acquire);
        if (effective == 0 || a_context.windowed == HF_FALSE) return;

        HF_PresentationCapabilitiesV1 caps{};
        const auto haveCaps = PresentationService::GetSingleton().GetCapabilities(caps);
        if ((effective & EffectivePreferFlipModel) != 0 && haveCaps) {
            if ((caps.flags & HF_PRESENTATION_CAPABILITY_FLIP_DISCARD) != 0) {
                a_context.swapEffect = HF_SWAP_EFFECT_FLIP_DISCARD;
            } else if ((caps.flags & HF_PRESENTATION_CAPABILITY_FLIP_SEQUENTIAL) != 0) {
                a_context.swapEffect = HF_SWAP_EFFECT_FLIP_SEQUENTIAL;
            }
        }

        const auto flip = IsFlipModel(a_context.swapEffect);
        if (flip) {
            if (a_context.sampleCount != 1 || a_context.sampleQuality != 0) {
                a_context.sampleCount = 1;
                a_context.sampleQuality = 0;
            }
            if ((effective & EffectiveBufferCount) != 0) {
                a_context.bufferCount = _effectiveBufferCount.load(std::memory_order_acquire);
            }
        }

        if ((effective & EffectiveAllowTearing) != 0) {
            const auto allow = _effectiveAllowTearing.load(std::memory_order_acquire) != 0;
            const auto supported = haveCaps && (caps.flags & HF_PRESENTATION_CAPABILITY_ALLOW_TEARING) != 0;
            if (allow && supported && flip) {
                a_context.swapChainFlags |= HF_SWAP_CHAIN_FLAG_ALLOW_TEARING;
            } else {
                a_context.swapChainFlags &= ~HF_SWAP_CHAIN_FLAG_ALLOW_TEARING;
            }
        }
    }

    void PresentationPolicyService::ObserveSwapChain(const HF_NativeHandle a_swapChain) noexcept
    {
        std::uint32_t observed = ObservedNone;
        auto* const swapChain = reinterpret_cast<REX::W32::IDXGISwapChain*>(
            static_cast<std::uintptr_t>(a_swapChain));
        if (swapChain) {
            REX::W32::DXGI_SWAP_CHAIN_DESC desc{};
            if (swapChain->GetDesc(std::addressof(desc)) >= 0) {
                if (IsFlipModel(static_cast<std::uint32_t>(desc.swapEffect))) observed |= ObservedFlipModel;
                if ((desc.flags & HF_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0) observed |= ObservedAllowTearing;
            }
        }
        _swapChainFlags.store(observed, std::memory_order_release);
        _observedSwapChainHandle.store(a_swapChain, std::memory_order_release);
    }
}
