#include "pch.h"
#include "ReferenceActorService.h"

#include "Diagnostics.h"
#include "FormService.h"
#include "ModuleContext.h"
#include "RuntimeState.h"

namespace HolyFramework
{
    ReferenceActorService& ReferenceActorService::GetSingleton() noexcept
    {
        static ReferenceActorService* instance = new ReferenceActorService();
        return *instance;
    }

    RE::TESObjectREFR* ReferenceActorService::ResolveReference(const HF_FormHandle a_handle) noexcept
    {
        if (!RuntimeState::GetSingleton().HasState(HF_RUNTIME_STATE_GAME_DATA_READY)) {
            return nullptr;
        }
        if (auto* form = FormService::ResolveHandle(a_handle)) {
            try {
                return form->As<RE::TESObjectREFR>();
            } catch (...) {
            }
        }
        return nullptr;
    }

    RE::Actor* ReferenceActorService::ResolveActor(const HF_FormHandle a_handle) noexcept
    {
        if (auto* ref = ResolveReference(a_handle)) {
            try {
                return ref->As<RE::Actor>();
            } catch (...) {
            }
        }
        return nullptr;
    }

    RE::ActorValueInfo* ReferenceActorService::ResolveActorValue(const HF_ActorValue a_value) noexcept
    {
        RE::ActorValue* values = nullptr;
        try {
            values = RE::ActorValue::GetSingleton();
        } catch (...) {
            return nullptr;
        }
        if (!values) {
            return nullptr;
        }

        switch (a_value) {
        case HF_ACTOR_VALUE_HEALTH: return values->health;
        case HF_ACTOR_VALUE_ACTION_POINTS: return values->actionPoints;
        case HF_ACTOR_VALUE_RADS: return values->rads;
        case HF_ACTOR_VALUE_FATIGUE: return values->fatigue;
        case HF_ACTOR_VALUE_SPEED_MULT: return values->speedMult;
        case HF_ACTOR_VALUE_CARRY_WEIGHT: return values->carryWeight;
        case HF_ACTOR_VALUE_STRENGTH: return values->strength;
        case HF_ACTOR_VALUE_PERCEPTION: return values->perception;
        case HF_ACTOR_VALUE_ENDURANCE: return values->endurance;
        case HF_ACTOR_VALUE_CHARISMA: return values->charisma;
        case HF_ACTOR_VALUE_INTELLIGENCE: return values->intelligence;
        case HF_ACTOR_VALUE_AGILITY: return values->agility;
        case HF_ACTOR_VALUE_LUCK: return values->luck;
        case HF_ACTOR_VALUE_DAMAGE_RESISTANCE: return values->damageResistance;
        case HF_ACTOR_VALUE_ENERGY_RESISTANCE: return values->energyResistance;
        case HF_ACTOR_VALUE_RAD_EXPOSURE_RESISTANCE: return values->radExposureResistance;
        case HF_ACTOR_VALUE_RAD_INGESTION_RESISTANCE: return values->radIngestionResistance;
        case HF_ACTOR_VALUE_POWER_ARMOR_BATTERY: return values->powerArmorBattery;
        case HF_ACTOR_VALUE_STAMINA: return values->stamina;
        default: return nullptr;
        }
    }

    RE::ActorValueInfo* ReferenceActorService::ResolveActorValueForm(const HF_FormHandle a_handle) noexcept
    {
        auto* const form = FormService::ResolveHandle(a_handle);
        if (!form) {
            return nullptr;
        }
        try {
            return form->As<RE::ActorValueInfo>();
        } catch (...) {
            return nullptr;
        }
    }

    std::optional<RE::ACTOR_VALUE_MODIFIER> ReferenceActorService::MapModifier(
        const HF_ActorValueModifier a_modifier) noexcept
    {
        switch (a_modifier) {
        case HF_ACTOR_VALUE_MODIFIER_PERMANENT:
            return RE::ACTOR_VALUE_MODIFIER::kPermanent;
        case HF_ACTOR_VALUE_MODIFIER_TEMPORARY:
            return RE::ACTOR_VALUE_MODIFIER::kTemporary;
        case HF_ACTOR_VALUE_MODIFIER_DAMAGE:
            return RE::ACTOR_VALUE_MODIFIER::kDamage;
        default:
            return std::nullopt;
        }
    }

    bool ReferenceActorService::NamesEqual(
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

    bool ReferenceActorService::ApplyDelta(
        const HF_FormHandle a_actor,
        const HF_FormHandle a_actorValue,
        const HF_ActorValueModifier a_modifier,
        const float a_delta) noexcept
    {
        if (!std::isfinite(a_delta)) {
            return false;
        }
        auto* const actor = ResolveActor(a_actor);
        auto* const actorValue = ResolveActorValueForm(a_actorValue);
        const auto modifier = MapModifier(a_modifier);
        if (!actor || !actorValue || !modifier) {
            return false;
        }
        try {
            actor->ModActorValue(*modifier, *actorValue, a_delta);
            return true;
        } catch (...) {
            return false;
        }
    }


    bool ReferenceActorService::IsReference(const HF_FormHandle a_handle) const noexcept
    {
        return ResolveReference(a_handle) != nullptr;
    }

    std::uint32_t ReferenceActorService::GetReferenceStateFlags(const HF_FormHandle a_handle) const noexcept
    {
        auto* const ref = ResolveReference(a_handle);
        if (!ref) {
            return HF_REFERENCE_STATE_NONE;
        }

        std::uint32_t flags = HF_REFERENCE_STATE_VALID;
        try {
            if (ref->IsDisabled()) {
                flags |= HF_REFERENCE_STATE_DISABLED;
            }
            if (ref->IsDeleted()) {
                flags |= HF_REFERENCE_STATE_DELETED;
            }
            if (ref->As<RE::Actor>()) {
                flags |= HF_REFERENCE_STATE_ACTOR;
            }
            if (ref == RE::PlayerCharacter::GetSingleton()) {
                flags |= HF_REFERENCE_STATE_PLAYER;
            }
        } catch (...) {
            return HF_REFERENCE_STATE_NONE;
        }
        return flags;
    }

    bool ReferenceActorService::GetPosition(const HF_FormHandle a_handle, HF_Vector3& a_outPosition) const noexcept
    {
        a_outPosition = {};
        auto* const ref = ResolveReference(a_handle);
        if (!ref) {
            return false;
        }
        try {
            const auto position = ref->GetPosition();
            a_outPosition = { position.x, position.y, position.z };
            return true;
        } catch (...) {
            return false;
        }
    }

    HF_FormHandle ReferenceActorService::GetBaseForm(const HF_FormHandle a_handle) const noexcept
    {
        auto* const ref = ResolveReference(a_handle);
        if (!ref) {
            return HF_INVALID_FORM_HANDLE;
        }
        try {
            return FormService::MakeHandle(ref->GetObjectReference());
        } catch (...) {
            return HF_INVALID_FORM_HANDLE;
        }
    }

    HF_FormHandle ReferenceActorService::GetParentCell(const HF_FormHandle a_handle) const noexcept
    {
        auto* const ref = ResolveReference(a_handle);
        if (!ref) {
            return HF_INVALID_FORM_HANDLE;
        }
        try {
            return FormService::MakeHandle(ref->GetParentCell());
        } catch (...) {
            return HF_INVALID_FORM_HANDLE;
        }
    }

    bool ReferenceActorService::GetDisplayName(
        const HF_FormHandle a_handle,
        std::string& a_outName) const noexcept
    {
        a_outName.clear();
        auto* const ref = ResolveReference(a_handle);
        if (!ref) {
            return false;
        }
        try {
            const char* const name = ref->GetDisplayFullName();
            if (name) {
                a_outName = name;
            }
            return true;
        } catch (...) {
            a_outName.clear();
            return false;
        }
    }


    bool ReferenceActorService::IsActor(const HF_FormHandle a_handle) const noexcept
    {
        return ResolveActor(a_handle) != nullptr;
    }

    std::uint32_t ReferenceActorService::GetActorStateFlags(const HF_FormHandle a_handle) const noexcept
    {
        auto* const actor = ResolveActor(a_handle);
        if (!actor) {
            return HF_ACTOR_STATE_NONE;
        }

        std::uint32_t flags = HF_ACTOR_STATE_VALID;
        try {
            if (actor->IsDead(false)) {
                flags |= HF_ACTOR_STATE_DEAD;
            }
            if (actor->IsInCombat()) {
                flags |= HF_ACTOR_STATE_IN_COMBAT;
            }
            if (actor->IsSneaking()) {
                flags |= HF_ACTOR_STATE_SNEAKING;
            }
            if (actor->IsVisible()) {
                flags |= HF_ACTOR_STATE_VISIBLE;
            }
            if (actor == RE::PlayerCharacter::GetSingleton()) {
                flags |= HF_ACTOR_STATE_PLAYER;
            }
        } catch (...) {
            return HF_ACTOR_STATE_NONE;
        }
        return flags;
    }

    HF_FormHandle ReferenceActorService::GetBaseActor(const HF_FormHandle a_handle) const noexcept
    {
        auto* const actor = ResolveActor(a_handle);
        if (!actor) {
            return HF_INVALID_FORM_HANDLE;
        }
        try {
            return FormService::MakeHandle(actor->GetNPC());
        } catch (...) {
            return HF_INVALID_FORM_HANDLE;
        }
    }

    bool ReferenceActorService::GetActorValue(const HF_FormHandle a_handle, const HF_ActorValue a_value, float& a_outValue) const noexcept
    {
        a_outValue = 0.0F;
        auto* const actor = ResolveActor(a_handle);
        auto* const info = ResolveActorValue(a_value);
        if (!actor || !info) {
            return false;
        }
        try {
            a_outValue = actor->GetActorValue(*info);
            return true;
        } catch (...) {
            return false;
        }
    }

    bool ReferenceActorService::GetBaseActorValue(const HF_FormHandle a_handle, const HF_ActorValue a_value, float& a_outValue) const noexcept
    {
        a_outValue = 0.0F;
        auto* const actor = ResolveActor(a_handle);
        auto* const info = ResolveActorValue(a_value);
        if (!actor || !info) {
            return false;
        }
        try {
            a_outValue = actor->GetBaseActorValue(*info);
            return true;
        } catch (...) {
            return false;
        }
    }

    bool ReferenceActorService::GetPermanentActorValue(const HF_FormHandle a_handle, const HF_ActorValue a_value, float& a_outValue) const noexcept
    {
        a_outValue = 0.0F;
        auto* const actor = ResolveActor(a_handle);
        auto* const info = ResolveActorValue(a_value);
        if (!actor || !info) {
            return false;
        }
        try {
            a_outValue = actor->GetPermanentActorValue(*info);
            return true;
        } catch (...) {
            return false;
        }
    }

    HF_FormHandle ReferenceActorService::GetActorValueForm(const HF_ActorValue a_value) const noexcept
    {
        auto* const info = ResolveActorValue(a_value);
        return info ? FormService::MakeHandle(info) : HF_INVALID_FORM_HANDLE;
    }

    bool ReferenceActorService::IsActorValueForm(const HF_FormHandle a_actorValue) const noexcept
    {
        return ResolveActorValueForm(a_actorValue) != nullptr;
    }

    bool ReferenceActorService::GetActorValueByForm(
        const HF_FormHandle a_actor,
        const HF_FormHandle a_actorValue,
        float& a_outValue) const noexcept
    {
        a_outValue = 0.0F;
        auto* const actor = ResolveActor(a_actor);
        auto* const actorValue = ResolveActorValueForm(a_actorValue);
        if (!actor || !actorValue) {
            return false;
        }
        try {
            a_outValue = actor->GetActorValue(*actorValue);
            return std::isfinite(a_outValue);
        } catch (...) {
            a_outValue = 0.0F;
            return false;
        }
    }

    bool ReferenceActorService::GetBaseActorValueByForm(
        const HF_FormHandle a_actor,
        const HF_FormHandle a_actorValue,
        float& a_outValue) const noexcept
    {
        a_outValue = 0.0F;
        auto* const actor = ResolveActor(a_actor);
        auto* const actorValue = ResolveActorValueForm(a_actorValue);
        if (!actor || !actorValue) {
            return false;
        }
        try {
            a_outValue = actor->GetBaseActorValue(*actorValue);
            return std::isfinite(a_outValue);
        } catch (...) {
            a_outValue = 0.0F;
            return false;
        }
    }

    bool ReferenceActorService::GetPermanentActorValueByForm(
        const HF_FormHandle a_actor,
        const HF_FormHandle a_actorValue,
        float& a_outValue) const noexcept
    {
        a_outValue = 0.0F;
        auto* const actor = ResolveActor(a_actor);
        auto* const actorValue = ResolveActorValueForm(a_actorValue);
        if (!actor || !actorValue) {
            return false;
        }
        try {
            a_outValue = actor->GetPermanentActorValue(*actorValue);
            return std::isfinite(a_outValue);
        } catch (...) {
            a_outValue = 0.0F;
            return false;
        }
    }

    bool ReferenceActorService::GetModifierByForm(
        const HF_FormHandle a_actor,
        const HF_FormHandle a_actorValue,
        const HF_ActorValueModifier a_modifier,
        float& a_outValue) const noexcept
    {
        a_outValue = 0.0F;
        auto* const actor = ResolveActor(a_actor);
        auto* const actorValue = ResolveActorValueForm(a_actorValue);
        const auto modifier = MapModifier(a_modifier);
        if (!actor || !actorValue || !modifier) {
            return false;
        }
        try {
            a_outValue = actor->GetModifier(*modifier, *actorValue);
            return std::isfinite(a_outValue);
        } catch (...) {
            a_outValue = 0.0F;
            return false;
        }
    }

    bool ReferenceActorService::ModifyActorValueByForm(
        const HF_FormHandle a_actor,
        const HF_FormHandle a_actorValue,
        const HF_ActorValueModifier a_modifier,
        const float a_delta) noexcept
    {
        const auto context = ModuleContext::Current();
        if (!context.name || !*context.name) {
            Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_ACTOR_VALUE_OWNER_UNKNOWN);
            return false;
        }
        if (!ApplyDelta(a_actor, a_actorValue, a_modifier, a_delta)) {
            Diagnostics::ReportFrameworkFailureForModule(
                context.name,
                context.logger,
                HF_ERROR_ACTOR_VALUE_INVALID_REQUEST);
            return false;
        }
        return true;
    }

    HF_ActorValueAdjustmentHandle ReferenceActorService::AcquireAdjustment(
        const HF_FormHandle a_actor,
        const HF_FormHandle a_actorValue,
        const HF_ActorValueModifier a_modifier,
        const float a_amount) noexcept
    {
        const auto context = ModuleContext::Current();
        if (!context.name || !*context.name) {
            Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_ACTOR_VALUE_OWNER_UNKNOWN);
            return HF_INVALID_ACTOR_VALUE_ADJUSTMENT_HANDLE;
        }
        if (!std::isfinite(a_amount) ||
            !ResolveActor(a_actor) ||
            !ResolveActorValueForm(a_actorValue) ||
            !MapModifier(a_modifier)) {
            Diagnostics::ReportFrameworkFailureForModule(
                context.name,
                context.logger,
                HF_ERROR_ACTOR_VALUE_INVALID_REQUEST);
            return HF_INVALID_ACTOR_VALUE_ADJUSTMENT_HANDLE;
        }

        auto handle = _nextAdjustmentHandle.fetch_add(1, std::memory_order_relaxed);
        if (handle == HF_INVALID_ACTOR_VALUE_ADJUSTMENT_HANDLE) {
            handle = _nextAdjustmentHandle.fetch_add(1, std::memory_order_relaxed);
        }
        if (!ApplyDelta(a_actor, a_actorValue, a_modifier, a_amount)) {
            Diagnostics::ReportFrameworkFailureForModule(
                context.name,
                context.logger,
                HF_ERROR_ACTOR_VALUE_APPLY_FAILED);
            return HF_INVALID_ACTOR_VALUE_ADJUSTMENT_HANDLE;
        }

        try {
            std::scoped_lock lock{ _adjustmentLock };
            _adjustments.push_back(Adjustment{
                .handle = handle,
                .actor = a_actor,
                .actorValue = a_actorValue,
                .modifier = a_modifier,
                .amount = a_amount,
                .owner = context.name,
                .logger = context.logger
            });
            return handle;
        } catch (...) {
            static_cast<void>(ApplyDelta(a_actor, a_actorValue, a_modifier, -a_amount));
            Diagnostics::ReportFrameworkFailureForModule(
                context.name,
                context.logger,
                HF_ERROR_ACTOR_VALUE_APPLY_FAILED);
            return HF_INVALID_ACTOR_VALUE_ADJUSTMENT_HANDLE;
        }
    }

    bool ReferenceActorService::UpdateAdjustment(
        const HF_ActorValueAdjustmentHandle a_handle,
        const float a_amount) noexcept
    {
        const auto context = ModuleContext::Current();
        if (!context.name || !*context.name || !std::isfinite(a_amount) ||
            a_handle == HF_INVALID_ACTOR_VALUE_ADJUSTMENT_HANDLE) {
            return false;
        }

        std::scoped_lock lock{ _adjustmentLock };
        const auto it = std::ranges::find_if(_adjustments, [a_handle](const Adjustment& a_adjustment) {
            return a_adjustment.handle == a_handle;
        });
        if (it == _adjustments.end()) {
            return false;
        }
        if (!NamesEqual(it->owner, context.name)) {
            Diagnostics::ReportFrameworkFailureForModule(
                context.name,
                context.logger,
                HF_ERROR_ACTOR_VALUE_OWNER_MISMATCH);
            return false;
        }

        const float delta = a_amount - it->amount;
        if (!std::isfinite(delta) || !ApplyDelta(it->actor, it->actorValue, it->modifier, delta)) {
            Diagnostics::ReportFrameworkFailureForModule(
                context.name,
                context.logger,
                HF_ERROR_ACTOR_VALUE_APPLY_FAILED);
            return false;
        }
        it->amount = a_amount;
        return true;
    }

    bool ReferenceActorService::ReleaseAdjustment(
        const HF_ActorValueAdjustmentHandle a_handle) noexcept
    {
        const auto context = ModuleContext::Current();
        if (!context.name || !*context.name ||
            a_handle == HF_INVALID_ACTOR_VALUE_ADJUSTMENT_HANDLE) {
            return false;
        }

        std::scoped_lock lock{ _adjustmentLock };
        const auto it = std::ranges::find_if(_adjustments, [a_handle](const Adjustment& a_adjustment) {
            return a_adjustment.handle == a_handle;
        });
        if (it == _adjustments.end()) {
            return false;
        }
        if (!NamesEqual(it->owner, context.name)) {
            Diagnostics::ReportFrameworkFailureForModule(
                context.name,
                context.logger,
                HF_ERROR_ACTOR_VALUE_OWNER_MISMATCH);
            return false;
        }

        if (std::abs(it->amount) > 0.0F &&
            !ApplyDelta(it->actor, it->actorValue, it->modifier, -it->amount)) {
            Diagnostics::ReportFrameworkFailureForModule(
                context.name,
                context.logger,
                HF_ERROR_ACTOR_VALUE_RESTORE_FAILED);
            return false;
        }
        _adjustments.erase(it);
        return true;
    }

    std::uint32_t ReferenceActorService::ReleaseAdjustmentsOwnedBy(
        const std::string_view a_moduleName) noexcept
    {
        if (a_moduleName.empty()) {
            return 0;
        }

        std::uint32_t released = 0;
        std::scoped_lock lock{ _adjustmentLock };
        for (std::size_t i = _adjustments.size(); i > 0; --i) {
            auto& adjustment = _adjustments[i - 1];
            if (!NamesEqual(adjustment.owner, a_moduleName)) {
                continue;
            }
            if (std::abs(adjustment.amount) > 0.0F &&
                !ApplyDelta(
                    adjustment.actor,
                    adjustment.actorValue,
                    adjustment.modifier,
                    -adjustment.amount)) {
                Diagnostics::ReportFrameworkWarningForModule(
                    adjustment.owner,
                    adjustment.logger,
                    HF_ERROR_ACTOR_VALUE_RESTORE_FAILED);
                continue;
            }
            _adjustments.erase(_adjustments.begin() + static_cast<std::ptrdiff_t>(i - 1));
            ++released;
        }
        return released;
    }


    bool ReferenceActorService::IsPlayerAvailable() const noexcept
    {
        if (!RuntimeState::GetSingleton().HasState(HF_RUNTIME_STATE_GAME_DATA_READY)) {
            return false;
        }
        try {
            return RE::PlayerCharacter::GetSingleton() != nullptr;
        } catch (...) {
            return false;
        }
    }

    HF_FormHandle ReferenceActorService::GetPlayerHandle() const noexcept
    {
        if (!IsPlayerAvailable()) {
            return HF_INVALID_FORM_HANDLE;
        }
        try {
            return FormService::MakeHandle(RE::PlayerCharacter::GetSingleton());
        } catch (...) {
            return HF_INVALID_FORM_HANDLE;
        }
    }

    bool ReferenceActorService::IsPlayerGodMode() const noexcept
    {
        try {
            auto* const player = RE::PlayerCharacter::GetSingleton();
            return player && player->IsGodMode();
        } catch (...) {
            return false;
        }
    }

    bool ReferenceActorService::IsPlayerImmortal() const noexcept
    {
        try {
            auto* const player = RE::PlayerCharacter::GetSingleton();
            return player && player->IsImmortal();
        } catch (...) {
            return false;
        }
    }

    bool ReferenceActorService::IsPlayerPipboyLightOn() const noexcept
    {
        try {
            auto* const player = RE::PlayerCharacter::GetSingleton();
            return player && player->IsPipboyLightOn();
        } catch (...) {
            return false;
        }
    }
    HF_FormHandle ReferenceActorService::GetPlayerDialogueTarget() const noexcept
    {
        if (!IsPlayerAvailable()) {
            return HF_INVALID_FORM_HANDLE;
        }
        try {
            auto* const player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                return HF_INVALID_FORM_HANDLE;
            }
            auto* const target = player->dialogueItemTarget.get().get();
            return FormService::MakeHandle(target);
        } catch (...) {
            return HF_INVALID_FORM_HANDLE;
        }
    }

}
