#include "pch.h"
#include "ConfigManager.h"

#include "Diagnostics.h"
#include "ModuleContext.h"

#include <Windows.h>
#ifdef ERROR
#  undef ERROR
#endif

#include <charconv>
#include <cstdlib>

namespace HolyFramework
{
    namespace
    {
        std::string Trim(const std::string_view a_value)
        {
            std::size_t first = 0;
            while (first < a_value.size() && std::isspace(static_cast<unsigned char>(a_value[first]))) {
                ++first;
            }
            std::size_t last = a_value.size();
            while (last > first && std::isspace(static_cast<unsigned char>(a_value[last - 1]))) {
                --last;
            }
            return std::string{ a_value.substr(first, last - first) };
        }

        std::string StripIniComment(const std::string_view a_line)
        {
            bool inString = false;
            bool escaped = false;
            for (std::size_t i = 0; i < a_line.size(); ++i) {
                const char ch = a_line[i];
                if (inString) {
                    if (escaped) {
                        escaped = false;
                    } else if (ch == '\\') {
                        escaped = true;
                    } else if (ch == '"') {
                        inString = false;
                    }
                    continue;
                }
                if (ch == '"') {
                    inString = true;
                    continue;
                }
                if ((ch == ';' || ch == '#') &&
                    (i == 0 || std::isspace(static_cast<unsigned char>(a_line[i - 1])))) {
                    return Trim(a_line.substr(0, i));
                }
            }
            return Trim(a_line);
        }

        std::optional<std::string> ParseQuotedString(const std::string_view a_value)
        {
            if (a_value.size() < 2 || a_value.front() != '"' || a_value.back() != '"') {
                return std::nullopt;
            }
            std::string result;
            result.reserve(a_value.size() - 2);
            bool escaped = false;
            for (std::size_t i = 1; i + 1 < a_value.size(); ++i) {
                const char ch = a_value[i];
                if (escaped) {
                    switch (ch) {
                    case 'n': result.push_back('\n'); break;
                    case 'r': result.push_back('\r'); break;
                    case 't': result.push_back('\t'); break;
                    case '"': result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    default:
                        // INI files commonly contain Windows paths. Preserve
                        // unknown escape sequences literally instead of
                        // rejecting an otherwise valid user setting.
                        result.push_back('\\');
                        result.push_back(ch);
                        break;
                    }
                    escaped = false;
                } else if (ch == '\\') {
                    escaped = true;
                } else {
                    result.push_back(ch);
                }
            }
            if (escaped) {
                result.push_back('\\');
            }
            return result;
        }

        bool ParseSection(const std::string_view a_line, std::string& a_outSection)
        {
            a_outSection.clear();
            if (a_line.size() < 2 || a_line.front() != '[' || a_line.back() != ']') {
                return false;
            }
            a_outSection = Trim(a_line.substr(1, a_line.size() - 2));
            return !a_outSection.empty();
        }

        std::string ToLower(const std::string_view a_value)
        {
            std::string result;
            result.reserve(a_value.size());
            for (const unsigned char ch : a_value) {
                result.push_back(static_cast<char>(std::tolower(ch)));
            }
            return result;
        }
    }

    ConfigManager& ConfigManager::GetSingleton() noexcept
    {
        static ConfigManager* instance = new ConfigManager();
        return *instance;
    }

    std::string ConfigManager::NormalizeModuleName(const std::string_view a_name) noexcept
    {
        return ToLower(a_name);
    }

    bool ConfigManager::IsValidKey(const std::string_view a_key) noexcept
    {
        if (a_key.empty() || a_key.size() > 191) {
            return false;
        }
        for (const unsigned char ch : a_key) {
            if (std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-') {
                continue;
            }
            return false;
        }
        return true;
    }

    std::string ConfigManager::NormalizeKey(const std::string_view a_key) noexcept
    {
        if (!IsValidKey(a_key)) {
            return {};
        }
        return ToLower(a_key);
    }

    void ConfigManager::RegisterModule(
        const std::string_view a_moduleName,
        const std::filesystem::path& a_iniPath) noexcept
    {
        if (a_moduleName.empty() || a_iniPath.empty()) {
            return;
        }
        try {
            std::scoped_lock lock{ _lock };
            auto& config = _modules[NormalizeModuleName(a_moduleName)];
            config.displayName = std::string{ a_moduleName };
            config.path = a_iniPath;
            config.loaded = false;
            config.attempted = false;
            config.values.clear();
            config.generation = 0;
        } catch (...) {
        }
    }

    void ConfigManager::UnregisterModule(const std::string_view a_moduleName) noexcept
    {
        if (a_moduleName.empty()) {
            return;
        }
        try {
            std::scoped_lock lock{ _lock };
            _modules.erase(NormalizeModuleName(a_moduleName));
        } catch (...) {
        }
    }

    bool ConfigManager::NamesEqual(
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

    std::filesystem::path ConfigManager::ResolveDocumentPath(
        const HF_ConfigDocumentRoot a_root,
        const std::string_view a_relativePath) noexcept
    {
        if (a_relativePath.empty() || a_relativePath.size() > 511) {
            return {};
        }

        try {
            const std::filesystem::path relative{ std::string{ a_relativePath } };
            if (relative.is_absolute() || relative.has_root_name() ||
                relative.has_root_directory()) {
                return {};
            }
            for (const auto& part : relative) {
                if (part == "..") {
                    return {};
                }
            }

            wchar_t buffer[MAX_PATH]{};
            const auto length = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
            if (length == 0 || length >= MAX_PATH) {
                return {};
            }
            const auto gameRoot = std::filesystem::path{ buffer }.parent_path();

            std::filesystem::path root;
            switch (a_root) {
            case HF_CONFIG_DOCUMENT_ROOT_GAME:
                root = gameRoot;
                break;
            case HF_CONFIG_DOCUMENT_ROOT_DATA:
                root = gameRoot / "Data";
                break;
            case HF_CONFIG_DOCUMENT_ROOT_MODULE_DIRECTORY:
                root = gameRoot / "Data" / "F4SE" / "Plugins" / "HolyFramework";
                break;
            default:
                return {};
            }

            return (root / relative).lexically_normal();
        } catch (...) {
            return {};
        }
    }

    bool ConfigManager::ParseIniFile(
        const std::filesystem::path& a_path,
        const std::string_view a_owner,
        const HF_LogHandle a_logger,
        std::unordered_map<std::string, Value>& a_outValues,
        bool& a_outExists,
        std::uint64_t& a_outSize,
        std::filesystem::file_time_type& a_outWriteTime,
        const HF_ErrorCode a_sourceUnavailableError) noexcept
    {
        a_outValues.clear();
        a_outExists = false;
        a_outSize = 0;
        a_outWriteTime = {};

        try {
            std::error_code error;
            const bool exists = std::filesystem::is_regular_file(a_path, error);
            if (error) {
                Diagnostics::ReportFrameworkFailureForModule(
                    a_owner.empty() ? "<unknown>" : a_owner,
                    a_logger,
                    a_sourceUnavailableError);
                return false;
            }
            if (!exists) {
                return true;
            }

            a_outExists = true;
            const auto size = std::filesystem::file_size(a_path, error);
            if (!error) {
                a_outSize = static_cast<std::uint64_t>(size);
            }
            error.clear();
            a_outWriteTime = std::filesystem::last_write_time(a_path, error);
            if (error) {
                a_outWriteTime = {};
            }

            std::ifstream input{ a_path };
            if (!input) {
                Diagnostics::ReportFrameworkFailureForModule(
                    a_owner.empty() ? "<unknown>" : a_owner,
                    a_logger,
                    a_sourceUnavailableError);
                return false;
            }

            std::unordered_map<std::string, Value> parsed;
            bool parseFailed = false;
            std::string section;
            std::string rawLine;
            std::size_t lineNumber = 0;

            while (std::getline(input, rawLine)) {
                ++lineNumber;
                if (!rawLine.empty() && rawLine.back() == '\r') {
                    rawLine.pop_back();
                }
                const auto line = StripIniComment(rawLine);
                if (line.empty()) {
                    continue;
                }

                if (line.front() == '[') {
                    if (!ParseSection(line, section)) {
                        parseFailed = true;
                        Diagnostics::ReportFrameworkFailureForModule(
                            a_owner.empty() ? "<unknown>" : a_owner,
                            a_logger,
                            HF_ERROR_CONFIG_PARSE_FAILED);
                    }
                    continue;
                }

                const auto equals = line.find('=');
                if (equals == std::string::npos) {
                    parseFailed = true;
                    Diagnostics::ReportFrameworkFailureForModule(
                        a_owner.empty() ? "<unknown>" : a_owner,
                        a_logger,
                        HF_ERROR_CONFIG_PARSE_FAILED);
                    continue;
                }

                const auto rawKey = Trim(std::string_view{ line }.substr(0, equals));
                auto key = section.empty() ? rawKey : section + "." + rawKey;
                key = NormalizeKey(key);
                if (key.empty()) {
                    parseFailed = true;
                    Diagnostics::ReportFrameworkFailureForModule(
                        a_owner.empty() ? "<unknown>" : a_owner,
                        a_logger,
                        HF_ERROR_CONFIG_INVALID_KEY);
                    continue;
                }

                const auto valueText = Trim(std::string_view{ line }.substr(equals + 1));
                Value value{};

                if (const auto text = ParseQuotedString(valueText)) {
                    value.type = ValueType::String;
                    value.stringValue = *text;
                } else {
                    const auto lower = ToLower(valueText);
                    if (lower == "true" || lower == "yes" || lower == "on") {
                        value.type = ValueType::Boolean;
                        value.booleanValue = true;
                    } else if (lower == "false" || lower == "no" || lower == "off") {
                        value.type = ValueType::Boolean;
                        value.booleanValue = false;
                    } else {
                        std::int64_t integer{};
                        const auto* begin = valueText.data();
                        const auto* finish = valueText.data() + valueText.size();
                        const auto intResult =
                            std::from_chars(begin, finish, integer, 10);
                        if (!valueText.empty() &&
                            intResult.ec == std::errc{} &&
                            intResult.ptr == finish) {
                            value.type = ValueType::Integer;
                            value.integerValue = integer;
                        } else {
                            char* parseEnd = nullptr;
                            const auto numeric = std::strtod(
                                valueText.c_str(),
                                &parseEnd);
                            if (!valueText.empty() &&
                                parseEnd &&
                                parseEnd == valueText.c_str() + valueText.size()) {
                                value.type = ValueType::Floating;
                                value.floatingValue = numeric;
                            } else {
                                value.type = ValueType::String;
                                value.stringValue = valueText;
                            }
                        }
                    }
                }

                parsed.insert_or_assign(std::move(key), std::move(value));
            }

            if (parseFailed) {
                return false;
            }

            a_outValues = std::move(parsed);
            return true;
        } catch (const std::exception&) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_owner.empty() ? "<unknown>" : a_owner,
                a_logger,
                HF_ERROR_CONFIG_PARSE_FAILED);
        } catch (...) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_owner.empty() ? "<unknown>" : a_owner,
                a_logger,
                HF_ERROR_CONFIG_PARSE_FAILED);
        }
        return false;
    }

    bool ConfigManager::ReloadLocked(
        const std::string& a_moduleKey,
        ModuleConfig& a_config) noexcept
    {
        a_config.attempted = true;

        std::unordered_map<std::string, Value> values;
        bool exists = false;
        std::uint64_t size = 0;
        std::filesystem::file_time_type writeTime{};
        const auto context = ModuleContext::Current();
        const auto owner = a_config.displayName.empty() ?
            std::string_view{ a_moduleKey } :
            std::string_view{ a_config.displayName };

        if (!ParseIniFile(
                a_config.path,
                owner,
                context.logger,
                values,
                exists,
                size,
                writeTime)) {
            return false;
        }

        a_config.values = std::move(values);
        a_config.loaded = true;
        ++a_config.generation;
        return true;
    }

    bool ConfigManager::EnsureLoaded(const std::string_view a_moduleName) noexcept
    {
        const auto moduleKey = NormalizeModuleName(a_moduleName);
        std::scoped_lock lock{ _lock };
        const auto it = _modules.find(moduleKey);
        if (it == _modules.end()) {
            return false;
        }
        if (it->second.loaded) {
            return true;
        }
        if (it->second.attempted) {
            // Do not reparse and duplicate the same diagnostics on every getter.
            // A module can explicitly call Reload() after fixing/changing its INI.
            return false;
        }
        return ReloadLocked(moduleKey, it->second);
    }

    bool ConfigManager::HasKey(const std::string_view a_moduleName, const std::string_view a_key) noexcept
    {
        const auto key = NormalizeKey(a_key);
        if (key.empty() || !EnsureLoaded(a_moduleName)) {
            return false;
        }
        std::scoped_lock lock{ _lock };
        const auto moduleIt = _modules.find(NormalizeModuleName(a_moduleName));
        return moduleIt != _modules.end() && moduleIt->second.values.contains(key);
    }

    bool ConfigManager::GetBool(
        const std::string_view a_moduleName,
        const std::string_view a_key,
        const bool a_defaultValue,
        bool& a_outValue) noexcept
    {
        a_outValue = a_defaultValue;
        const auto key = NormalizeKey(a_key);
        if (key.empty() || !EnsureLoaded(a_moduleName)) {
            return false;
        }
        std::scoped_lock lock{ _lock };
        const auto moduleIt = _modules.find(NormalizeModuleName(a_moduleName));
        if (moduleIt == _modules.end()) return false;
        const auto valueIt = moduleIt->second.values.find(key);
        if (valueIt == moduleIt->second.values.end()) return false;
        if (valueIt->second.type == ValueType::Boolean) {
            a_outValue = valueIt->second.booleanValue;
            return true;
        }
        if (valueIt->second.type == ValueType::Integer &&
            (valueIt->second.integerValue == 0 || valueIt->second.integerValue == 1)) {
            a_outValue = valueIt->second.integerValue != 0;
            return true;
        }
        return false;
    }

    bool ConfigManager::GetInt64(
        const std::string_view a_moduleName,
        const std::string_view a_key,
        const std::int64_t a_defaultValue,
        std::int64_t& a_outValue) noexcept
    {
        a_outValue = a_defaultValue;
        const auto key = NormalizeKey(a_key);
        if (key.empty() || !EnsureLoaded(a_moduleName)) return false;
        std::scoped_lock lock{ _lock };
        const auto moduleIt = _modules.find(NormalizeModuleName(a_moduleName));
        if (moduleIt == _modules.end()) return false;
        const auto valueIt = moduleIt->second.values.find(key);
        if (valueIt == moduleIt->second.values.end() || valueIt->second.type != ValueType::Integer) return false;
        a_outValue = valueIt->second.integerValue;
        return true;
    }

    bool ConfigManager::GetDouble(
        const std::string_view a_moduleName,
        const std::string_view a_key,
        const double a_defaultValue,
        double& a_outValue) noexcept
    {
        a_outValue = a_defaultValue;
        const auto key = NormalizeKey(a_key);
        if (key.empty() || !EnsureLoaded(a_moduleName)) return false;
        std::scoped_lock lock{ _lock };
        const auto moduleIt = _modules.find(NormalizeModuleName(a_moduleName));
        if (moduleIt == _modules.end()) return false;
        const auto valueIt = moduleIt->second.values.find(key);
        if (valueIt == moduleIt->second.values.end()) return false;
        if (valueIt->second.type == ValueType::Floating) {
            a_outValue = valueIt->second.floatingValue;
            return true;
        }
        if (valueIt->second.type == ValueType::Integer) {
            a_outValue = static_cast<double>(valueIt->second.integerValue);
            return true;
        }
        return false;
    }

    bool ConfigManager::GetString(
        const std::string_view a_moduleName,
        const std::string_view a_key,
        const std::string_view a_defaultValue,
        std::string& a_outValue) noexcept
    {
        a_outValue = std::string{ a_defaultValue };
        const auto key = NormalizeKey(a_key);
        if (key.empty() || !EnsureLoaded(a_moduleName)) return false;
        std::scoped_lock lock{ _lock };
        const auto moduleIt = _modules.find(NormalizeModuleName(a_moduleName));
        if (moduleIt == _modules.end()) return false;
        const auto valueIt = moduleIt->second.values.find(key);
        if (valueIt == moduleIt->second.values.end() || valueIt->second.type != ValueType::String) return false;
        a_outValue = valueIt->second.stringValue;
        return true;
    }

    bool ConfigManager::Reload(const std::string_view a_moduleName) noexcept
    {
        const auto moduleKey = NormalizeModuleName(a_moduleName);
        std::scoped_lock lock{ _lock };
        const auto it = _modules.find(moduleKey);
        if (it == _modules.end()) {
            return false;
        }
        return ReloadLocked(moduleKey, it->second);
    }

    std::uint64_t ConfigManager::GetGeneration(const std::string_view a_moduleName) noexcept
    {
        if (!EnsureLoaded(a_moduleName)) {
            return 0;
        }
        std::scoped_lock lock{ _lock };
        const auto it = _modules.find(NormalizeModuleName(a_moduleName));
        return it == _modules.end() ? 0 : it->second.generation;
    }

    ConfigManager::Document* ConfigManager::FindDocumentLocked(
        const HF_ConfigDocumentHandle a_handle,
        const std::string_view a_moduleName,
        std::string* const a_outActualOwner) noexcept
    {
        const auto it = std::ranges::find_if(
            _documents,
            [a_handle](const Document& a_document) {
                return a_document.handle == a_handle;
            });
        if (it == _documents.end()) {
            return nullptr;
        }
        if (!NamesEqual(it->owner, a_moduleName)) {
            if (a_outActualOwner) {
                *a_outActualOwner = it->owner;
            }
            return nullptr;
        }
        return std::addressof(*it);
    }

    bool ConfigManager::ReadDocumentValue(
        const Document& a_document,
        const std::string_view a_key,
        const Value*& a_outValue) noexcept
    {
        a_outValue = nullptr;
        const auto key = NormalizeKey(a_key);
        if (key.empty() || !a_document.loaded) {
            return false;
        }
        const auto it = a_document.values.find(key);
        if (it == a_document.values.end()) {
            return false;
        }
        a_outValue = std::addressof(it->second);
        return true;
    }

    bool ConfigManager::ReloadDocumentLocked(
        Document& a_document,
        bool& a_outChanged) noexcept
    {
        a_outChanged = false;

        std::error_code error;
        const bool exists = std::filesystem::is_regular_file(a_document.path, error);
        if (error) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_document.owner,
                a_document.logger,
                HF_ERROR_CONFIG_DOCUMENT_SOURCE_UNAVAILABLE);
            return false;
        }

        std::uint64_t size = 0;
        std::filesystem::file_time_type writeTime{};
        if (exists) {
            const auto rawSize = std::filesystem::file_size(a_document.path, error);
            if (error) {
                return false;
            }
            size = static_cast<std::uint64_t>(rawSize);
            writeTime = std::filesystem::last_write_time(a_document.path, error);
            if (error) {
                return false;
            }
        }

        const bool stampChanged =
            !a_document.stampValid ||
            a_document.fileExists != exists ||
            (exists &&
                (a_document.fileSize != size ||
                 a_document.writeTime != writeTime));

        if (!stampChanged) {
            return true;
        }

        std::unordered_map<std::string, Value> parsed;
        bool parsedExists = false;
        std::uint64_t parsedSize = 0;
        std::filesystem::file_time_type parsedWriteTime{};
        if (!ParseIniFile(
                a_document.path,
                a_document.owner,
                a_document.logger,
                parsed,
                parsedExists,
                parsedSize,
                parsedWriteTime,
                HF_ERROR_CONFIG_DOCUMENT_SOURCE_UNAVAILABLE)) {
            return false;
        }

        a_document.values = std::move(parsed);
        a_document.fileExists = parsedExists;
        a_document.fileSize = parsedSize;
        a_document.writeTime = parsedWriteTime;
        a_document.stampValid = true;
        a_document.loaded = true;
        ++a_document.generation;
        a_outChanged = true;
        return true;
    }

    HF_ConfigDocumentHandle ConfigManager::OpenDocument(
        const HF_ConfigDocumentRoot a_root,
        const std::string_view a_relativePath,
        const std::string_view a_moduleName,
        const HF_LogHandle a_logger) noexcept
    {
        if (a_moduleName.empty()) {
            Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>",
                a_logger,
                HF_ERROR_CONFIG_DOCUMENT_OWNER_UNKNOWN);
            return HF_INVALID_CONFIG_DOCUMENT_HANDLE;
        }

        const auto path = ResolveDocumentPath(a_root, a_relativePath);
        if (path.empty()) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName,
                a_logger,
                HF_ERROR_CONFIG_DOCUMENT_INVALID_REQUEST);
            return HF_INVALID_CONFIG_DOCUMENT_HANDLE;
        }

        try {
            auto handle = _nextDocumentHandle.fetch_add(
                1,
                std::memory_order_relaxed);
            if (handle == HF_INVALID_CONFIG_DOCUMENT_HANDLE) {
                handle = _nextDocumentHandle.fetch_add(
                    1,
                    std::memory_order_relaxed);
            }

            Document document{};
            document.handle = handle;
            document.owner = std::string{ a_moduleName };
            document.logger = a_logger;
            document.path = path;
            document.lastCheck = std::chrono::steady_clock::now();

            bool changed = false;
            if (!ReloadDocumentLocked(document, changed)) {
                return HF_INVALID_CONFIG_DOCUMENT_HANDLE;
            }

            std::scoped_lock lock{ _lock };
            _documents.push_back(std::move(document));
            return handle;
        } catch (...) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_moduleName,
                a_logger,
                HF_ERROR_CONFIG_DOCUMENT_OPEN_FAILED);
            return HF_INVALID_CONFIG_DOCUMENT_HANDLE;
        }
    }

    bool ConfigManager::CloseDocument(
        const HF_ConfigDocumentHandle a_handle,
        const std::string_view a_moduleName,
        std::string* const a_outActualOwner) noexcept
    {
        if (a_handle == HF_INVALID_CONFIG_DOCUMENT_HANDLE ||
            a_moduleName.empty()) {
            return false;
        }

        std::scoped_lock lock{ _lock };
        const auto it = std::ranges::find_if(
            _documents,
            [a_handle](const Document& a_document) {
                return a_document.handle == a_handle;
            });
        if (it == _documents.end()) {
            return false;
        }
        if (!NamesEqual(it->owner, a_moduleName)) {
            if (a_outActualOwner) {
                *a_outActualOwner = it->owner;
            }
            return false;
        }
        _documents.erase(it);
        return true;
    }

    bool ConfigManager::GetDocumentState(
        const HF_ConfigDocumentHandle a_handle,
        const std::string_view a_moduleName,
        HF_ConfigDocumentStateV1& a_outState) noexcept
    {
        a_outState = {};
        a_outState.structSize = sizeof(HF_ConfigDocumentStateV1);
        std::scoped_lock lock{ _lock };
        const auto* const document =
            FindDocumentLocked(a_handle, a_moduleName);
        if (!document) {
            return false;
        }

        a_outState.flags = HF_CONFIG_DOCUMENT_STATE_OPEN;
        if (document->fileExists) {
            a_outState.flags |= HF_CONFIG_DOCUMENT_STATE_FILE_EXISTS;
        }
        if (document->loaded) {
            a_outState.flags |= HF_CONFIG_DOCUMENT_STATE_LOADED;
        }
        a_outState.generation = document->generation;
        a_outState.fileSize = document->fileSize;
        return true;
    }

    bool ConfigManager::RefreshDocument(
        const HF_ConfigDocumentHandle a_handle,
        const std::string_view a_moduleName,
        const std::uint32_t a_minCheckIntervalMs,
        bool& a_outChanged) noexcept
    {
        a_outChanged = false;
        std::scoped_lock lock{ _lock };
        auto* const document = FindDocumentLocked(
            a_handle,
            a_moduleName);
        if (!document) {
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        if (a_minCheckIntervalMs > 0 &&
            document->lastCheck.time_since_epoch().count() != 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - document->lastCheck);
            if (elapsed.count() >= 0 &&
                static_cast<std::uint64_t>(elapsed.count()) <
                    a_minCheckIntervalMs) {
                return true;
            }
        }

        document->lastCheck = now;
        return ReloadDocumentLocked(*document, a_outChanged);
    }

    bool ConfigManager::DocumentHasKey(
        const HF_ConfigDocumentHandle a_handle,
        const std::string_view a_moduleName,
        const std::string_view a_key) noexcept
    {
        std::scoped_lock lock{ _lock };
        const auto* const document = FindDocumentLocked(
            a_handle,
            a_moduleName);
        const Value* value = nullptr;
        return document && ReadDocumentValue(*document, a_key, value);
    }

    bool ConfigManager::DocumentGetBool(
        const HF_ConfigDocumentHandle a_handle,
        const std::string_view a_moduleName,
        const std::string_view a_key,
        const bool a_defaultValue,
        bool& a_outValue) noexcept
    {
        a_outValue = a_defaultValue;
        std::scoped_lock lock{ _lock };
        const auto* const document = FindDocumentLocked(
            a_handle,
            a_moduleName);
        const Value* value = nullptr;
        if (!document || !ReadDocumentValue(*document, a_key, value)) {
            return false;
        }
        if (value->type == ValueType::Boolean) {
            a_outValue = value->booleanValue;
            return true;
        }
        if (value->type == ValueType::Integer &&
            (value->integerValue == 0 || value->integerValue == 1)) {
            a_outValue = value->integerValue != 0;
            return true;
        }
        return false;
    }

    bool ConfigManager::DocumentGetInt64(
        const HF_ConfigDocumentHandle a_handle,
        const std::string_view a_moduleName,
        const std::string_view a_key,
        const std::int64_t a_defaultValue,
        std::int64_t& a_outValue) noexcept
    {
        a_outValue = a_defaultValue;
        std::scoped_lock lock{ _lock };
        const auto* const document = FindDocumentLocked(
            a_handle,
            a_moduleName);
        const Value* value = nullptr;
        if (!document || !ReadDocumentValue(*document, a_key, value) ||
            value->type != ValueType::Integer) {
            return false;
        }
        a_outValue = value->integerValue;
        return true;
    }

    bool ConfigManager::DocumentGetDouble(
        const HF_ConfigDocumentHandle a_handle,
        const std::string_view a_moduleName,
        const std::string_view a_key,
        const double a_defaultValue,
        double& a_outValue) noexcept
    {
        a_outValue = a_defaultValue;
        std::scoped_lock lock{ _lock };
        const auto* const document = FindDocumentLocked(
            a_handle,
            a_moduleName);
        const Value* value = nullptr;
        if (!document || !ReadDocumentValue(*document, a_key, value)) {
            return false;
        }
        if (value->type == ValueType::Floating) {
            a_outValue = value->floatingValue;
            return true;
        }
        if (value->type == ValueType::Integer) {
            a_outValue = static_cast<double>(value->integerValue);
            return true;
        }
        return false;
    }

    bool ConfigManager::DocumentGetString(
        const HF_ConfigDocumentHandle a_handle,
        const std::string_view a_moduleName,
        const std::string_view a_key,
        const std::string_view a_defaultValue,
        std::string& a_outValue) noexcept
    {
        a_outValue = std::string{ a_defaultValue };
        std::scoped_lock lock{ _lock };
        const auto* const document = FindDocumentLocked(
            a_handle,
            a_moduleName);
        const Value* value = nullptr;
        if (!document || !ReadDocumentValue(*document, a_key, value) ||
            value->type != ValueType::String) {
            return false;
        }
        a_outValue = value->stringValue;
        return true;
    }

    std::uint32_t ConfigManager::CloseDocumentsOwnedBy(
        const std::string_view a_moduleName) noexcept
    {
        if (a_moduleName.empty()) {
            return 0;
        }
        std::scoped_lock lock{ _lock };
        const auto before = _documents.size();
        std::erase_if(
            _documents,
            [a_moduleName](const Document& a_document) {
                return NamesEqual(a_document.owner, a_moduleName);
            });
        return static_cast<std::uint32_t>(before - _documents.size());
    }
}
