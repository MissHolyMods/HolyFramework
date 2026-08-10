#include "pch.h"
#include "EventBus.h"
#include "Diagnostics.h"
#include "ModuleContext.h"
#include "PerformanceMonitor.h"

namespace HolyFramework
{
    EventBus& EventBus::GetSingleton() noexcept
    {
        static EventBus* instance = new EventBus();
        return *instance;
    }

    bool EventBus::IsValidEvent(const HF_Event a_event) noexcept
    {
        const auto value = static_cast<std::uint32_t>(a_event);
        return value >= static_cast<std::uint32_t>(HF_EVENT_POST_LOAD) &&
               value <= static_cast<std::uint32_t>(HF_EVENT_UI_STATE_CHANGED);
    }

    HF_SubscriptionHandle EventBus::Subscribe(
        const HF_Event a_event,
        const HF_EventCallback a_callback,
        void* const a_userData)
    {
        const auto context = ModuleContext::Current();
        return SubscribeOwned(
            a_event,
            a_callback,
            a_userData,
            context.name ? std::string_view{ context.name } : std::string_view{},
            context.logger);
    }

    HF_SubscriptionHandle EventBus::SubscribeOwned(
        const HF_Event a_event,
        const HF_EventCallback a_callback,
        void* const a_userData,
        const std::string_view a_moduleName,
        const HF_LogHandle a_logger)
    {
        if (!IsValidEvent(a_event) || !a_callback || a_moduleName.empty()) {
            return 0;
        }

        auto handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
        if (handle == 0) {
            handle = _nextHandle.fetch_add(1, std::memory_order_relaxed);
        }

        std::scoped_lock lock{ _lock };
        _subscriptions.push_back(Subscription{
            .handle = handle,
            .event = a_event,
            .callback = a_callback,
            .userData = a_userData,
            .moduleName = std::string{ a_moduleName },
            .logger = a_logger
        });
        return handle;
    }

    bool EventBus::Unsubscribe(const HF_SubscriptionHandle a_handle)
    {
        if (a_handle == 0) {
            return false;
        }

        std::scoped_lock lock{ _lock };
        const auto oldSize = _subscriptions.size();
        std::erase_if(_subscriptions, [a_handle](const Subscription& a_subscription) {
            return a_subscription.handle == a_handle;
        });
        return _subscriptions.size() != oldSize;
    }

    bool EventBus::UnsubscribeOwned(
        const HF_SubscriptionHandle a_handle,
        const std::string_view a_moduleName,
        std::string* const a_outActualOwner)
    {
        if (a_handle == 0 || a_moduleName.empty()) {
            return false;
        }

        std::scoped_lock lock{ _lock };
        const auto it = std::ranges::find_if(_subscriptions, [a_handle](const Subscription& a_subscription) {
            return a_subscription.handle == a_handle;
        });
        if (it == _subscriptions.end()) {
            return false;
        }
        if (it->moduleName != a_moduleName) {
            if (a_outActualOwner) {
                *a_outActualOwner = it->moduleName;
            }
            return false;
        }
        _subscriptions.erase(it);
        return true;
    }

    std::uint32_t EventBus::UnsubscribeOwnedBy(const std::string_view a_moduleName)
    {
        if (a_moduleName.empty()) {
            return 0;
        }

        std::scoped_lock lock{ _lock };
        const auto oldSize = _subscriptions.size();
        std::erase_if(_subscriptions, [a_moduleName](const Subscription& a_subscription) {
            return a_subscription.moduleName == a_moduleName;
        });
        return static_cast<std::uint32_t>(oldSize - _subscriptions.size());
    }

    bool EventBus::HasSubscribers(const HF_Event a_event) const noexcept
    {
        if (!IsValidEvent(a_event)) return false;
        std::scoped_lock lock{ _lock };
        return std::ranges::any_of(_subscriptions, [a_event](const Subscription& sub) { return sub.event == a_event; });
    }

    void EventBus::Dispatch(const HF_Event a_event)
    {
        std::vector<Subscription> listeners;
        {
            std::scoped_lock lock{ _lock };
            listeners.reserve(_subscriptions.size());
            for (const auto& subscription : _subscriptions) {
                if (subscription.event == a_event) {
                    listeners.push_back(subscription);
                }
            }
        }

        for (const auto& listener : listeners) {
            const char* const moduleName = listener.moduleName.empty() ? nullptr : listener.moduleName.c_str();
            ModuleContext::Scope scope{ moduleName, listener.logger };
            const auto perfLabel = std::format("event.{}", static_cast<std::uint32_t>(a_event));
            PerformanceMonitor::Scope perfScope{ listener.moduleName, perfLabel };
            try {
                listener.callback(a_event, listener.userData);
            } catch (const std::exception&) {
                Diagnostics::ReportFrameworkFailureForModule(
                    listener.moduleName,
                    listener.logger,
                    HF_ERROR_EVENT_CALLBACK_EXCEPTION);
                Unsubscribe(listener.handle);
            } catch (...) {
                Diagnostics::ReportFrameworkFailureForModule(
                    listener.moduleName,
                    listener.logger,
                    HF_ERROR_EVENT_CALLBACK_EXCEPTION);
                Unsubscribe(listener.handle);
            }
        }
    }
}
