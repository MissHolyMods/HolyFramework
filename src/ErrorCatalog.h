#pragma once

namespace HolyFramework
{
    // HolyFramework.toml is the HFW-only core error catalog. Module TOMLs are
    // registered later by ModuleLoader after the DLL declares a valid
    // HF_HolyFrameworkSignature marker and namespace.
    void InitializeErrorCatalog() noexcept;

    [[nodiscard]] bool RegisterModuleErrorCatalog(
        std::string_view a_moduleName,
        std::string_view a_prefix,
        const std::filesystem::path& a_path) noexcept;
    void UnregisterModuleErrorCatalog(std::string_view a_moduleName) noexcept;

    [[nodiscard]] std::string ResolveModuleErrorPrefix(std::string_view a_moduleName) noexcept;
    [[nodiscard]] const char* GetErrorName(std::string_view a_prefix, HF_ErrorCode a_code) noexcept;
    [[nodiscard]] const char* GetErrorDescription(std::string_view a_prefix, HF_ErrorCode a_code) noexcept;
    [[nodiscard]] std::string FormatErrorCode(std::string_view a_prefix, HF_ErrorCode a_code);
    [[nodiscard]] const char* GetModuleErrorName(std::string_view a_moduleName, HF_ErrorCode a_code) noexcept;
    [[nodiscard]] const char* GetModuleErrorDescription(std::string_view a_moduleName, HF_ErrorCode a_code) noexcept;
    [[nodiscard]] std::string FormatModuleErrorCode(std::string_view a_moduleName, HF_ErrorCode a_code);
}
