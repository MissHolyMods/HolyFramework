#pragma once

namespace HolyFramework
{
    class ReferenceActorService final
    {
    public:
        static ReferenceActorService& GetSingleton() noexcept;

        [[nodiscard]] bool IsReference(HF_FormHandle a_handle) const noexcept;
        [[nodiscard]] std::uint32_t GetReferenceStateFlags(HF_FormHandle a_handle) const noexcept;
        bool GetPosition(HF_FormHandle a_handle, HF_Vector3& a_outPosition) const noexcept;
        [[nodiscard]] HF_FormHandle GetBaseForm(HF_FormHandle a_handle) const noexcept;
        [[nodiscard]] HF_FormHandle GetParentCell(HF_FormHandle a_handle) const noexcept;
        bool GetDisplayName(HF_FormHandle a_handle, std::string& a_outName) const noexcept;

        [[nodiscard]] bool IsActor(HF_FormHandle a_handle) const noexcept;
        [[nodiscard]] std::uint32_t GetActorStateFlags(HF_FormHandle a_handle) const noexcept;
        [[nodiscard]] HF_FormHandle GetBaseActor(HF_FormHandle a_handle) const noexcept;
        bool GetActorValue(HF_FormHandle a_handle, HF_ActorValue a_value, float& a_outValue) const noexcept;
        bool GetBaseActorValue(HF_FormHandle a_handle, HF_ActorValue a_value, float& a_outValue) const noexcept;
        bool GetPermanentActorValue(HF_FormHandle a_handle, HF_ActorValue a_value, float& a_outValue) const noexcept;
        [[nodiscard]] HF_FormHandle GetActorValueForm(HF_ActorValue a_value) const noexcept;

        [[nodiscard]] bool IsActorValueForm(HF_FormHandle a_actorValue) const noexcept;
        bool GetActorValueByForm(HF_FormHandle a_actor, HF_FormHandle a_actorValue, float& a_outValue) const noexcept;
        bool GetBaseActorValueByForm(HF_FormHandle a_actor, HF_FormHandle a_actorValue, float& a_outValue) const noexcept;
        bool GetPermanentActorValueByForm(HF_FormHandle a_actor, HF_FormHandle a_actorValue, float& a_outValue) const noexcept;
        bool GetModifierByForm(HF_FormHandle a_actor, HF_FormHandle a_actorValue, HF_ActorValueModifier a_modifier, float& a_outValue) const noexcept;
        bool ModifyActorValueByForm(HF_FormHandle a_actor, HF_FormHandle a_actorValue, HF_ActorValueModifier a_modifier, float a_delta) noexcept;

        HF_ActorValueAdjustmentHandle AcquireAdjustment(
            HF_FormHandle a_actor,
            HF_FormHandle a_actorValue,
            HF_ActorValueModifier a_modifier,
            float a_amount) noexcept;
        bool UpdateAdjustment(HF_ActorValueAdjustmentHandle a_handle, float a_amount) noexcept;
        bool ReleaseAdjustment(HF_ActorValueAdjustmentHandle a_handle) noexcept;
        std::uint32_t ReleaseAdjustmentsOwnedBy(std::string_view a_moduleName) noexcept;

        [[nodiscard]] bool IsPlayerAvailable() const noexcept;
        [[nodiscard]] HF_FormHandle GetPlayerHandle() const noexcept;
        [[nodiscard]] bool IsPlayerGodMode() const noexcept;
        [[nodiscard]] bool IsPlayerImmortal() const noexcept;
        [[nodiscard]] bool IsPlayerPipboyLightOn() const noexcept;
        [[nodiscard]] HF_FormHandle GetPlayerDialogueTarget() const noexcept;

    private:
        struct Adjustment
        {
            HF_ActorValueAdjustmentHandle handle{ HF_INVALID_ACTOR_VALUE_ADJUSTMENT_HANDLE };
            HF_FormHandle actor{ HF_INVALID_FORM_HANDLE };
            HF_FormHandle actorValue{ HF_INVALID_FORM_HANDLE };
            HF_ActorValueModifier modifier{ HF_ACTOR_VALUE_MODIFIER_TEMPORARY };
            float amount{ 0.0F };
            std::string owner;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
        };

        ReferenceActorService() = default;

        [[nodiscard]] static RE::TESObjectREFR* ResolveReference(HF_FormHandle a_handle) noexcept;
        [[nodiscard]] static RE::Actor* ResolveActor(HF_FormHandle a_handle) noexcept;
        [[nodiscard]] static RE::ActorValueInfo* ResolveActorValue(HF_ActorValue a_value) noexcept;
        [[nodiscard]] static RE::ActorValueInfo* ResolveActorValueForm(HF_FormHandle a_handle) noexcept;
        [[nodiscard]] static std::optional<RE::ACTOR_VALUE_MODIFIER> MapModifier(HF_ActorValueModifier a_modifier) noexcept;
        [[nodiscard]] static bool NamesEqual(std::string_view a_left, std::string_view a_right) noexcept;
        static bool ApplyDelta(
            HF_FormHandle a_actor,
            HF_FormHandle a_actorValue,
            HF_ActorValueModifier a_modifier,
            float a_delta) noexcept;

        std::mutex _adjustmentLock;
        std::vector<Adjustment> _adjustments;
        std::atomic<std::uint64_t> _nextAdjustmentHandle{ 1 };
    };
}
