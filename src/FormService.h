#pragma once

namespace HolyFramework
{
    class FormService final
    {
    public:
        static FormService& GetSingleton() noexcept;

        [[nodiscard]] HF_FormHandle LookupByID(std::uint32_t a_formID) const noexcept;
        [[nodiscard]] HF_FormHandle LookupByEditorID(std::string_view a_editorID) const noexcept;
        [[nodiscard]] bool IsValid(HF_FormHandle a_handle) const noexcept;
        bool GetInfo(HF_FormHandle a_handle, HF_FormInfoV1& a_outInfo) const noexcept;
        [[nodiscard]] bool IsType(HF_FormHandle a_handle, std::string_view a_typeCode) const noexcept;

        // Internal bridge for higher-level HolyFramework services. These stay in
        // src/ and never cross the public ABI.
        [[nodiscard]] static HF_FormHandle MakeHandle(RE::TESForm* a_form) noexcept;
        [[nodiscard]] static RE::TESForm* ResolveHandle(HF_FormHandle a_handle) noexcept;

    private:
        FormService() = default;
        [[nodiscard]] static bool IsDynamicFormID(std::uint32_t a_formID) noexcept;
    };
}
