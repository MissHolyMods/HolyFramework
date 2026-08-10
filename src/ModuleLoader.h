#pragma once

namespace HolyFramework
{
    class ModuleLoader final
    {
    public:
        static ModuleLoader& GetSingleton() noexcept;

        std::uint32_t LoadAll(const HF_API* a_api);
        [[nodiscard]] std::uint32_t GetLoadedCount() const noexcept;
        [[nodiscard]] std::uint32_t GetHealthyCount() const noexcept;
        [[nodiscard]] std::uint32_t GetDegradedCount() const noexcept;
        [[nodiscard]] bool GetRecordByIndex(std::uint32_t a_index, HF_ModuleRecordV2& a_out) const noexcept;
        [[nodiscard]] bool FindRecordByName(std::string_view a_name, HF_ModuleRecordV2& a_out) const noexcept;
        [[nodiscard]] bool FindExecutionIdentityByCodeAddress(
            const void* a_address,
            std::string& a_moduleName,
            HF_LogHandle& a_logger) const noexcept;

        void MarkDegraded(
            std::string_view a_moduleName,
            std::string_view a_errorPrefix,
            HF_ErrorCode a_code) noexcept;

    private:
        struct LoadedModule
        {
            REX::W32::HMODULE handle{ nullptr };
            std::string name;
            HF_Version version{};
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
            HF_ModuleHealth health{ HF_MODULE_HEALTH_HEALTHY };
            HF_ErrorCode lastError{ HF_ERROR_NONE };
            std::string lastErrorPrefix;
        };

        struct ProvisionalModule
        {
            REX::W32::HMODULE handle{ nullptr };
            std::string name;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
            HF_ModuleHealth health{ HF_MODULE_HEALTH_HEALTHY };
            HF_ErrorCode lastError{ HF_ERROR_NONE };
            std::string lastErrorPrefix;
        };

        struct QuarantinedModule
        {
            REX::W32::HMODULE handle{ nullptr };
            std::string name;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
        };

        ModuleLoader() = default;

        [[nodiscard]] static std::filesystem::path GetModuleDirectory();
        [[nodiscard]] static bool NamesEqual(std::string_view a_left, std::string_view a_right) noexcept;
        static void FillRecord(const LoadedModule& a_module, HF_ModuleRecordV2& a_out) noexcept;
        bool LoadOne(const std::filesystem::path& a_path, const HF_API* a_api);

        mutable std::mutex _lock;
        std::vector<LoadedModule> _modules;
        std::vector<ProvisionalModule> _provisionalModules;
        std::vector<QuarantinedModule> _quarantinedModules;
    };
}
