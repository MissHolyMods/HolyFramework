#pragma once

namespace HolyFramework
{
    class EventBus final
    {
    public:
        static EventBus& GetSingleton() noexcept;

        HF_SubscriptionHandle Subscribe(HF_Event a_event, HF_EventCallback a_callback, void* a_userData);
        HF_SubscriptionHandle SubscribeOwned(
            HF_Event a_event,
            HF_EventCallback a_callback,
            void* a_userData,
            std::string_view a_moduleName,
            HF_LogHandle a_logger);
        bool Unsubscribe(HF_SubscriptionHandle a_handle);
        bool UnsubscribeOwned(
            HF_SubscriptionHandle a_handle,
            std::string_view a_moduleName,
            std::string* a_outActualOwner = nullptr);
        std::uint32_t UnsubscribeOwnedBy(std::string_view a_moduleName);
        [[nodiscard]] bool HasSubscribers(HF_Event a_event) const noexcept;
        void Dispatch(HF_Event a_event);

    private:
        struct Subscription
        {
            HF_SubscriptionHandle handle{ 0 };
            HF_Event event{ HF_EVENT_POST_LOAD };
            HF_EventCallback callback{ nullptr };
            void* userData{ nullptr };
            std::string moduleName;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
        };

        EventBus() = default;

        [[nodiscard]] static bool IsValidEvent(HF_Event a_event) noexcept;

        mutable std::mutex _lock;
        std::vector<Subscription> _subscriptions;
        std::atomic<HF_SubscriptionHandle> _nextHandle{ 1 };
    };
}
