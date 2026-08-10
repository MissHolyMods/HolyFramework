#pragma once

namespace HolyFramework
{
    class ConfigManager final
    {
    public:
        static ConfigManager& GetSingleton() noexcept;

        void RegisterModule(std::string_view a_moduleName, const std::filesystem::path& a_iniPath) noexcept;
        void UnregisterModule(std::string_view a_moduleName) noexcept;

        [[nodiscard]] bool HasKey(std::string_view a_moduleName, std::string_view a_key) noexcept;
        [[nodiscard]] bool GetBool(std::string_view a_moduleName, std::string_view a_key, bool a_defaultValue, bool& a_outValue) noexcept;
        [[nodiscard]] bool GetInt64(std::string_view a_moduleName, std::string_view a_key, std::int64_t a_defaultValue, std::int64_t& a_outValue) noexcept;
        [[nodiscard]] bool GetDouble(std::string_view a_moduleName, std::string_view a_key, double a_defaultValue, double& a_outValue) noexcept;
        [[nodiscard]] bool GetString(std::string_view a_moduleName, std::string_view a_key, std::string_view a_defaultValue, std::string& a_outValue) noexcept;
        [[nodiscard]] bool Reload(std::string_view a_moduleName) noexcept;
        [[nodiscard]] std::uint64_t GetGeneration(std::string_view a_moduleName) noexcept;

        HF_ConfigDocumentHandle OpenDocument(
            HF_ConfigDocumentRoot a_root,
            std::string_view a_relativePath,
            std::string_view a_moduleName,
            HF_LogHandle a_logger) noexcept;
        bool CloseDocument(
            HF_ConfigDocumentHandle a_handle,
            std::string_view a_moduleName,
            std::string* a_outActualOwner = nullptr) noexcept;
        bool GetDocumentState(
            HF_ConfigDocumentHandle a_handle,
            std::string_view a_moduleName,
            HF_ConfigDocumentStateV1& a_outState) noexcept;
        bool RefreshDocument(
            HF_ConfigDocumentHandle a_handle,
            std::string_view a_moduleName,
            std::uint32_t a_minCheckIntervalMs,
            bool& a_outChanged) noexcept;
        bool DocumentHasKey(
            HF_ConfigDocumentHandle a_handle,
            std::string_view a_moduleName,
            std::string_view a_key) noexcept;
        bool DocumentGetBool(
            HF_ConfigDocumentHandle a_handle,
            std::string_view a_moduleName,
            std::string_view a_key,
            bool a_defaultValue,
            bool& a_outValue) noexcept;
        bool DocumentGetInt64(
            HF_ConfigDocumentHandle a_handle,
            std::string_view a_moduleName,
            std::string_view a_key,
            std::int64_t a_defaultValue,
            std::int64_t& a_outValue) noexcept;
        bool DocumentGetDouble(
            HF_ConfigDocumentHandle a_handle,
            std::string_view a_moduleName,
            std::string_view a_key,
            double a_defaultValue,
            double& a_outValue) noexcept;
        bool DocumentGetString(
            HF_ConfigDocumentHandle a_handle,
            std::string_view a_moduleName,
            std::string_view a_key,
            std::string_view a_defaultValue,
            std::string& a_outValue) noexcept;
        std::uint32_t CloseDocumentsOwnedBy(std::string_view a_moduleName) noexcept;

    private:
        enum class ValueType : std::uint8_t
        {
            Boolean,
            Integer,
            Floating,
            String
        };

        struct Value
        {
            ValueType type{ ValueType::String };
            bool booleanValue{ false };
            std::int64_t integerValue{ 0 };
            double floatingValue{ 0.0 };
            std::string stringValue;
        };

        struct ModuleConfig
        {
            std::string displayName;
            std::filesystem::path path;
            std::unordered_map<std::string, Value> values;
            std::uint64_t generation{ 0 };
            bool loaded{ false };
            bool attempted{ false };
        };

        struct Document
        {
            HF_ConfigDocumentHandle handle{ HF_INVALID_CONFIG_DOCUMENT_HANDLE };
            std::string owner;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
            std::filesystem::path path;
            std::unordered_map<std::string, Value> values;
            std::filesystem::file_time_type writeTime{};
            std::chrono::steady_clock::time_point lastCheck{};
            std::uint64_t generation{ 0 };
            std::uint64_t fileSize{ 0 };
            bool fileExists{ false };
            bool loaded{ false };
            bool stampValid{ false };
        };

        ConfigManager() = default;

        [[nodiscard]] static std::string NormalizeModuleName(std::string_view a_name) noexcept;
        [[nodiscard]] static std::string NormalizeKey(std::string_view a_key) noexcept;
        [[nodiscard]] static bool IsValidKey(std::string_view a_key) noexcept;
        [[nodiscard]] bool EnsureLoaded(std::string_view a_moduleName) noexcept;
        [[nodiscard]] bool ReloadLocked(const std::string& a_moduleKey, ModuleConfig& a_config) noexcept;

        [[nodiscard]] static bool NamesEqual(std::string_view a_left, std::string_view a_right) noexcept;
        [[nodiscard]] static std::filesystem::path ResolveDocumentPath(
            HF_ConfigDocumentRoot a_root,
            std::string_view a_relativePath) noexcept;
        [[nodiscard]] static bool ParseIniFile(
            const std::filesystem::path& a_path,
            std::string_view a_owner,
            HF_LogHandle a_logger,
            std::unordered_map<std::string, Value>& a_outValues,
            bool& a_outExists,
            std::uint64_t& a_outSize,
            std::filesystem::file_time_type& a_outWriteTime,
            HF_ErrorCode a_sourceUnavailableError = HF_ERROR_CONFIG_SOURCE_UNAVAILABLE) noexcept;
        [[nodiscard]] static bool ReadDocumentValue(
            const Document& a_document,
            std::string_view a_key,
            const Value*& a_outValue) noexcept;
        [[nodiscard]] Document* FindDocumentLocked(
            HF_ConfigDocumentHandle a_handle,
            std::string_view a_moduleName,
            std::string* a_outActualOwner = nullptr) noexcept;
        [[nodiscard]] bool ReloadDocumentLocked(Document& a_document, bool& a_outChanged) noexcept;

        mutable std::mutex _lock;
        std::unordered_map<std::string, ModuleConfig> _modules;
        std::vector<Document> _documents;
        std::atomic<std::uint64_t> _nextDocumentHandle{ 1 };
    };
}
