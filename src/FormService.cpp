#include "pch.h"
#include "FormService.h"

#include "RuntimeState.h"

namespace HolyFramework
{
    FormService& FormService::GetSingleton() noexcept
    {
        static FormService* instance = new FormService();
        return *instance;
    }

    bool FormService::IsDynamicFormID(const std::uint32_t a_formID) noexcept
    {
        return (a_formID & 0xFF000000u) == 0xFF000000u;
    }

    HF_FormHandle FormService::MakeHandle(RE::TESForm* const a_form) noexcept
    {
        if (!a_form) {
            return HF_INVALID_FORM_HANDLE;
        }
        const auto formID = a_form->GetFormID();
        if (formID == 0) {
            return HF_INVALID_FORM_HANDLE;
        }

        // Static/load-order forms are stable across save transitions and use a
        // zero generation tag. Runtime-created FFxxxxxx forms are bound to the
        // current HolyFramework session generation so a stale handle cannot
        // silently resolve to a different temporary reference after loading.
        std::uint64_t generation = 0;
        if (IsDynamicFormID(formID)) {
            if (!RuntimeState::GetSingleton().HasState(HF_RUNTIME_STATE_SESSION_ACTIVE)) {
                return HF_INVALID_FORM_HANDLE;
            }
            generation = RuntimeState::GetSingleton().GetSessionGeneration() & 0xFFFFFFFFull;
            if (generation == 0) {
                return HF_INVALID_FORM_HANDLE;
            }
        }
        return (generation << 32) | static_cast<std::uint64_t>(formID);
    }

    RE::TESForm* FormService::ResolveHandle(const HF_FormHandle a_handle) noexcept
    {
        if (a_handle == HF_INVALID_FORM_HANDLE) {
            return nullptr;
        }
        const auto formID = static_cast<std::uint32_t>(a_handle & 0xFFFFFFFFull);
        const auto generation = static_cast<std::uint32_t>(a_handle >> 32);
        if (formID == 0) {
            return nullptr;
        }
        if (IsDynamicFormID(formID)) {
            if (!RuntimeState::GetSingleton().HasState(HF_RUNTIME_STATE_SESSION_ACTIVE)) {
                return nullptr;
            }
            const auto current = static_cast<std::uint32_t>(RuntimeState::GetSingleton().GetSessionGeneration() & 0xFFFFFFFFull);
            if (generation == 0 || generation != current) {
                return nullptr;
            }
        } else if (generation != 0) {
            return nullptr;
        }

        try {
            return RE::TESForm::GetFormByID(formID);
        } catch (...) {
            return nullptr;
        }
    }

    HF_FormHandle FormService::LookupByID(const std::uint32_t a_formID) const noexcept
    {
        if (a_formID == 0 ||
            !RuntimeState::GetSingleton().HasState(HF_RUNTIME_STATE_GAME_DATA_READY)) {
            return HF_INVALID_FORM_HANDLE;
        }
        try {
            return MakeHandle(RE::TESForm::GetFormByID(a_formID));
        } catch (...) {
            return HF_INVALID_FORM_HANDLE;
        }
    }

    HF_FormHandle FormService::LookupByEditorID(const std::string_view a_editorID) const noexcept
    {
        if (a_editorID.empty() || a_editorID.size() > 127 ||
            !RuntimeState::GetSingleton().HasState(HF_RUNTIME_STATE_GAME_DATA_READY)) {
            return HF_INVALID_FORM_HANDLE;
        }
        try {
            return MakeHandle(RE::TESForm::GetFormByEditorID(RE::BSFixedString{ a_editorID }));
        } catch (...) {
            return HF_INVALID_FORM_HANDLE;
        }
    }

    bool FormService::IsValid(const HF_FormHandle a_handle) const noexcept
    {
        return ResolveHandle(a_handle) != nullptr;
    }

    bool FormService::GetInfo(const HF_FormHandle a_handle, HF_FormInfoV1& a_outInfo) const noexcept
    {
        a_outInfo = {};
        a_outInfo.structSize = sizeof(HF_FormInfoV1);
        auto* const form = ResolveHandle(a_handle);
        if (!form) {
            return false;
        }

        try {
            a_outInfo.formID = form->GetFormID();
            a_outInfo.formType = static_cast<std::uint32_t>(std::to_underlying(form->GetFormType()));
            a_outInfo.formFlags = form->GetFormFlags();
            const auto* const typeCode = form->GetFormTypeString();
            const auto* const editorID = form->GetFormEditorID();
            std::snprintf(a_outInfo.typeCode, sizeof(a_outInfo.typeCode), "%s", typeCode ? typeCode : "");
            std::snprintf(a_outInfo.editorID, sizeof(a_outInfo.editorID), "%s", editorID ? editorID : "");
            return true;
        } catch (...) {
            a_outInfo = {};
            a_outInfo.structSize = sizeof(HF_FormInfoV1);
            return false;
        }
    }

    bool FormService::IsType(const HF_FormHandle a_handle, const std::string_view a_typeCode) const noexcept
    {
        if (a_typeCode.empty()) {
            return false;
        }
        auto* const form = ResolveHandle(a_handle);
        if (!form) {
            return false;
        }
        try {
            const char* const actual = form->GetFormTypeString();
            if (!actual) {
                return false;
            }
            const std::string_view actualView{ actual };
            if (actualView.size() != a_typeCode.size()) {
                return false;
            }
            for (std::size_t i = 0; i < actualView.size(); ++i) {
                if (std::toupper(static_cast<unsigned char>(actualView[i])) !=
                    std::toupper(static_cast<unsigned char>(a_typeCode[i]))) {
                    return false;
                }
            }
            return true;
        } catch (...) {
            return false;
        }
    }
}
