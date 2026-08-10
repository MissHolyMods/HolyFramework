#include "pch.h"
#include "PlayerMovementService.h"

#include <Windows.h>
#ifdef ERROR
#  undef ERROR
#endif

#include "Diagnostics.h"
#include "FormService.h"
#include "ModuleContext.h"
#include "RuntimeState.h"

namespace HolyFramework
{
    namespace
    {
        inline constexpr std::size_t kMovementVTableIndex = 5;
        inline constexpr std::size_t kGetControllerOutputIndex = 1;
        thread_local HF_PlayerMovementSubscriptionHandle g_currentMovementSubscription =
            HF_INVALID_PLAYER_MOVEMENT_SUBSCRIPTION_HANDLE;
    }

    PlayerMovementService& PlayerMovementService::GetSingleton() noexcept
    {
        static PlayerMovementService* instance = new PlayerMovementService();
        return *instance;
    }

    bool PlayerMovementService::NamesEqual(
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

    void PlayerMovementService::PublishSnapshot(
        std::shared_ptr<const SubscriptionList> a_snapshot) noexcept
    {
        _subscriptions.store(std::move(a_snapshot), std::memory_order_release);
    }

    std::shared_ptr<const PlayerMovementService::SubscriptionList>
    PlayerMovementService::LoadSnapshot() const noexcept
    {
        return _subscriptions.load(std::memory_order_acquire);
    }

    bool PlayerMovementService::IsAvailable() const noexcept
    {
        return RuntimeState::GetSingleton().HasState(HF_RUNTIME_STATE_INPUT_READY) &&
               !_compromised.load(std::memory_order_acquire);
    }

    bool PlayerMovementService::GetLatest(HF_PlayerMovementSampleV1& a_outSample) const noexcept
    {
        a_outSample = {};
        a_outSample.structSize = sizeof(HF_PlayerMovementSampleV1);
        std::scoped_lock lock{ _sampleLock };
        if (!_hasLatest) {
            return false;
        }
        a_outSample = _latest;
        return true;
    }

    bool PlayerMovementService::EnsureInstalled() noexcept
    {
        if (_compromised.load(std::memory_order_acquire)) {
            return false;
        }
        if (_installed.load(std::memory_order_acquire)) {
            return true;
        }

        std::scoped_lock installLock{ _installLock };
        if (_installed.load(std::memory_order_relaxed)) {
            return true;
        }

        try {
            REL::Relocation<std::uintptr_t> vtable{
                RE::PlayerControls::VTABLE[kMovementVTableIndex]
            };
            const auto vtableAddress = vtable.address();
            if (vtableAddress == 0) {
                Diagnostics::ReportFrameworkFailureForModule(
                    "HolyFramework",
                    HF_INVALID_LOG_HANDLE,
                    HF_ERROR_PLAYER_MOVEMENT_HOOK_UNAVAILABLE);
                return false;
            }

            auto* const slot = reinterpret_cast<std::uintptr_t*>(
                vtableAddress + sizeof(void*) * kGetControllerOutputIndex);
            const auto originalAddress = *slot;
            if (originalAddress == 0) {
                Diagnostics::ReportFrameworkFailureForModule(
                    "HolyFramework",
                    HF_INVALID_LOG_HANDLE,
                    HF_ERROR_PLAYER_MOVEMENT_HOOK_UNAVAILABLE);
                return false;
            }

            const auto thunkAddress = reinterpret_cast<std::uintptr_t>(&Thunk);
            if (originalAddress == thunkAddress) {
                if (_original.load(std::memory_order_acquire)) {
                    _slotAddress.store(
                        reinterpret_cast<std::uintptr_t>(slot),
                        std::memory_order_release);
                    _installed.store(true, std::memory_order_release);
                    return true;
                }
                return false;
            }

            _original.store(
                reinterpret_cast<Movement_t>(originalAddress),
                std::memory_order_release);
            vtable.write_vfunc(kGetControllerOutputIndex, &Thunk);

            if (*slot != thunkAddress) {
                _original.store(nullptr, std::memory_order_release);
                Diagnostics::ReportFrameworkFailureForModule(
                    "HolyFramework",
                    HF_INVALID_LOG_HANDLE,
                    HF_ERROR_PLAYER_MOVEMENT_HOOK_INSTALL_FAILED);
                return false;
            }

            _slotAddress.store(
                reinterpret_cast<std::uintptr_t>(slot),
                std::memory_order_release);
            _installed.store(true, std::memory_order_release);
            REX::INFO("HolyFramework player movement observer installed");
            return true;
        } catch (const std::exception&) {
            Diagnostics::ReportFrameworkFailureForModule(
                "HolyFramework",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_PLAYER_MOVEMENT_HOOK_INSTALL_FAILED);
            return false;
        } catch (...) {
            Diagnostics::ReportFrameworkFailureForModule(
                "HolyFramework",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_PLAYER_MOVEMENT_HOOK_INSTALL_FAILED);
            return false;
        }
    }

    void PlayerMovementService::TryRestoreIfUnused() noexcept
    {
        {
            std::scoped_lock lock{ _subscriptionLock };
            const auto current = LoadSnapshot();
            if (current && !current->empty()) {
                return;
            }
        }
        if (!_installed.load(std::memory_order_acquire)) {
            return;
        }

        std::scoped_lock installLock{ _installLock };
        if (!_installed.load(std::memory_order_relaxed)) {
            return;
        }

        const auto slotAddress = _slotAddress.load(std::memory_order_acquire);
        const auto original = _original.load(std::memory_order_acquire);
        if (!slotAddress || !original) {
            return;
        }

        auto* const slot = reinterpret_cast<std::uintptr_t*>(slotAddress);
        const auto thunkAddress = reinterpret_cast<std::uintptr_t>(&Thunk);
        if (*slot != thunkAddress) {
            // Another component replaced the vtable after HolyFramework. Do not
            // overwrite it: that component may still chain through our thunk.
            // Preserve _original permanently so such a chain keeps reaching the
            // Fallout target, and refuse future subscriptions rather than install
            // a second copy of the observer into an unknown chain.
            _installed.store(false, std::memory_order_release);
            _compromised.store(true, std::memory_order_release);
            _slotAddress.store(0, std::memory_order_release);
            return;
        }

        try {
            REL::Relocation<std::uintptr_t> vtable{
                RE::PlayerControls::VTABLE[kMovementVTableIndex]
            };
            vtable.write_vfunc(kGetControllerOutputIndex, original);
            if (*slot == reinterpret_cast<std::uintptr_t>(original)) {
                _installed.store(false, std::memory_order_release);
                _slotAddress.store(0, std::memory_order_release);
                _original.store(nullptr, std::memory_order_release);
                REX::INFO("HolyFramework player movement observer removed");
            }
        } catch (...) {
            // Keep ownership if restoration failed so a later release/cleanup can retry.
        }
    }

    HF_PlayerMovementSubscriptionHandle PlayerMovementService::Subscribe(
        const HF_PlayerMovementCallback a_callback,
        void* const a_userData,
        const std::string_view a_moduleName,
        const HF_LogHandle a_logger) noexcept
    {
        if (!a_callback || a_moduleName.empty()) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName.empty() ? "<unknown>" : a_moduleName,
                a_logger,
                HF_ERROR_PLAYER_MOVEMENT_INVALID_REQUEST);
            return HF_INVALID_PLAYER_MOVEMENT_SUBSCRIPTION_HANDLE;
        }
        if (!EnsureInstalled()) {
            return HF_INVALID_PLAYER_MOVEMENT_SUBSCRIPTION_HANDLE;
        }

        try {
            auto handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
            if (handle == HF_INVALID_PLAYER_MOVEMENT_SUBSCRIPTION_HANDLE) {
                handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
            }
            auto runtime = std::make_shared<Runtime>();
            std::scoped_lock lock{ _subscriptionLock };
            const auto current = LoadSnapshot();
            auto next = std::make_shared<SubscriptionList>(
                current ? *current : SubscriptionList{});
            next->push_back(Subscription{
                .handle = handle,
                .callback = a_callback,
                .userData = a_userData,
                .owner = std::string{ a_moduleName },
                .logger = a_logger,
                .runtime = std::move(runtime)
            });
            PublishSnapshot(next);
            return handle;
        } catch (...) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName,
                a_logger,
                HF_ERROR_PLAYER_MOVEMENT_SUBSCRIBE_FAILED);
            TryRestoreIfUnused();
            return HF_INVALID_PLAYER_MOVEMENT_SUBSCRIPTION_HANDLE;
        }
    }

    void PlayerMovementService::LeaveCallback(
        const std::shared_ptr<Runtime>& a_runtime) noexcept
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

    void PlayerMovementService::WaitForQuiescence(
        const std::shared_ptr<Runtime>& a_runtime,
        const HF_PlayerMovementSubscriptionHandle a_handle) noexcept
    {
        if (!a_runtime || g_currentMovementSubscription == a_handle) {
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

    bool PlayerMovementService::Unsubscribe(
        const HF_PlayerMovementSubscriptionHandle a_handle,
        const std::string_view a_moduleName,
        std::string* const a_outActualOwner) noexcept
    {
        if (a_handle == HF_INVALID_PLAYER_MOVEMENT_SUBSCRIPTION_HANDLE ||
            a_moduleName.empty()) {
            return false;
        }

        std::shared_ptr<Runtime> runtime;
        {
            std::scoped_lock lock{ _subscriptionLock };
            const auto current = LoadSnapshot();
            if (!current) {
                return false;
            }
            const auto it = std::ranges::find_if(
                *current,
                [a_handle](const Subscription& a_subscription) {
                    return a_subscription.handle == a_handle;
                });
            if (it == current->end()) {
                return false;
            }
            if (!NamesEqual(it->owner, a_moduleName)) {
                if (a_outActualOwner) {
                    *a_outActualOwner = it->owner;
                }
                return false;
            }
            runtime = it->runtime;
            if (runtime) {
                runtime->enabled.store(false, std::memory_order_release);
            }
            auto next = std::make_shared<SubscriptionList>(*current);
            std::erase_if(*next, [a_handle](const Subscription& a_subscription) {
                return a_subscription.handle == a_handle;
            });
            PublishSnapshot(next);
        }

        WaitForQuiescence(runtime, a_handle);
        TryRestoreIfUnused();
        return true;
    }

    std::uint32_t PlayerMovementService::UnsubscribeOwnedBy(
        const std::string_view a_moduleName) noexcept
    {
        if (a_moduleName.empty()) {
            return 0;
        }

        std::vector<std::pair<
            HF_PlayerMovementSubscriptionHandle,
            std::shared_ptr<Runtime>>> retired;
        {
            std::scoped_lock lock{ _subscriptionLock };
            const auto current = LoadSnapshot();
            if (!current || current->empty()) {
                return 0;
            }
            auto next = std::make_shared<SubscriptionList>();
            next->reserve(current->size());
            for (const auto& sub : *current) {
                if (!NamesEqual(sub.owner, a_moduleName)) {
                    next->push_back(sub);
                    continue;
                }
                if (sub.runtime) {
                    sub.runtime->enabled.store(false, std::memory_order_release);
                }
                retired.emplace_back(sub.handle, sub.runtime);
            }
            if (retired.empty()) {
                return 0;
            }
            PublishSnapshot(next);
        }

        for (const auto& [handle, runtime] : retired) {
            WaitForQuiescence(runtime, handle);
        }
        TryRestoreIfUnused();
        return static_cast<std::uint32_t>(retired.size());
    }

    void PlayerMovementService::Thunk(
        RE::IMovementPlayerControls* const a_controls,
        const std::uint32_t a_numericID,
        RE::PlayerControlsMovementData& a_output) noexcept
    {
        auto& service = GetSingleton();
        const auto original = service._original.load(std::memory_order_acquire);
        service.Dispatch(a_controls);
        if (original) {
            original(a_controls, a_numericID, a_output);
        }
    }

    void PlayerMovementService::Dispatch(
        RE::IMovementPlayerControls* const a_controls) noexcept
    {
        const auto listeners = LoadSnapshot();
        if (!listeners || listeners->empty()) {
            return;
        }

        HF_PlayerMovementSampleV1 sample{};
        sample.structSize = sizeof(HF_PlayerMovementSampleV1);
        sample.sequence = _sequence.fetch_add(1, std::memory_order_relaxed) + 1;
        sample.sessionGeneration =
            RuntimeState::GetSingleton().GetSessionGeneration();

        try {
            auto* const controls = static_cast<RE::PlayerControls*>(a_controls);
            auto* const player = RE::PlayerCharacter::GetSingleton();
            if (controls && player) {
                sample.flags |= HF_PLAYER_MOVEMENT_AVAILABLE;
                sample.player = FormService::MakeHandle(player);
                sample.moveX = controls->data.moveInputVec.x;
                sample.moveY = controls->data.moveInputVec.y;
                if (!std::isfinite(sample.moveX)) sample.moveX = 0.0F;
                if (!std::isfinite(sample.moveY)) sample.moveY = 0.0F;
                sample.magnitude = std::clamp(
                    std::hypot(sample.moveX, sample.moveY),
                    0.0F,
                    1.0F);

                if (controls->blockPlayerInput) {
                    sample.flags |= HF_PLAYER_MOVEMENT_INPUT_BLOCKED;
                }
                if (controls->data.autoMove) {
                    sample.flags |= HF_PLAYER_MOVEMENT_AUTO_MOVE;
                }
                if (controls->data.running) {
                    sample.flags |= HF_PLAYER_MOVEMENT_RUNNING;
                }
                if (player->DoGetSprinting()) {
                    sample.flags |= HF_PLAYER_MOVEMENT_SPRINTING;
                }
                if (player->IsSneaking()) {
                    sample.flags |= HF_PLAYER_MOVEMENT_SNEAKING;
                }
                if (player->IsSwimming()) {
                    sample.flags |= HF_PLAYER_MOVEMENT_SWIMMING;
                }
                if (player->DoGetCharacterState() ==
                    RE::IMovementState::CHARACTER_STATE::kOnGround) {
                    sample.flags |= HF_PLAYER_MOVEMENT_ON_GROUND;
                }
                if (RE::PowerArmor::ActorInPowerArmor(*player)) {
                    sample.flags |= HF_PLAYER_MOVEMENT_POWER_ARMOR;
                }
            }
        } catch (...) {
            sample.flags = HF_PLAYER_MOVEMENT_NONE;
            sample.player = HF_INVALID_FORM_HANDLE;
            sample.moveX = 0.0F;
            sample.moveY = 0.0F;
            sample.magnitude = 0.0F;
        }

        {
            std::scoped_lock lock{ _sampleLock };
            _latest = sample;
            _hasLatest = true;
        }

        std::vector<std::pair<HF_PlayerMovementSubscriptionHandle, std::string>> failedListeners;
        for (const auto& listener : *listeners) {
            if (!listener.callback || !listener.runtime ||
                !listener.runtime->enabled.load(std::memory_order_acquire)) {
                continue;
            }

            listener.runtime->inFlight.fetch_add(1, std::memory_order_acq_rel);
            if (!listener.runtime->enabled.load(std::memory_order_acquire)) {
                LeaveCallback(listener.runtime);
                continue;
            }

            const auto previous = g_currentMovementSubscription;
            g_currentMovementSubscription = listener.handle;
            ModuleContext::Scope scope{ listener.owner.c_str(), listener.logger };
            try {
                listener.callback(&sample, listener.userData);
            } catch (const std::exception&) {
                listener.runtime->enabled.store(false, std::memory_order_release);
                Diagnostics::ReportFrameworkFailureForModule(
                    listener.owner,
                    listener.logger,
                    HF_ERROR_PLAYER_MOVEMENT_CALLBACK_EXCEPTION);
                failedListeners.emplace_back(listener.handle, listener.owner);
            } catch (...) {
                listener.runtime->enabled.store(false, std::memory_order_release);
                Diagnostics::ReportFrameworkFailureForModule(
                    listener.owner,
                    listener.logger,
                    HF_ERROR_PLAYER_MOVEMENT_CALLBACK_EXCEPTION);
                failedListeners.emplace_back(listener.handle, listener.owner);
            }
            g_currentMovementSubscription = previous;
            LeaveCallback(listener.runtime);
        }

        for (const auto& [handle, owner] : failedListeners) {
            (void)Unsubscribe(handle, owner);
        }
    }
}
