#include "pch.h"
#include "ErrorCatalog.h"

namespace HolyFramework
{
    namespace
    {
        struct ErrorEntry
        {
            std::string name;
            std::string description;
        };

        struct ParsedEntry
        {
            std::uint32_t code{};
            std::string name;
            std::string description;
        };

        std::mutex g_lock;
        std::unordered_map<std::string, ErrorEntry> g_entries;
        std::unordered_map<std::string, std::string> g_modulePrefixes;
        std::unordered_map<std::string, std::string> g_prefixOwners;
        bool g_initialized{};

        [[nodiscard]] std::string_view Trim(std::string_view text) noexcept
        {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
            return text;
        }

        [[nodiscard]] std::string ToLower(std::string_view text)
        {
            std::string result{ text };
            std::ranges::transform(result, result.begin(), [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return result;
        }

        [[nodiscard]] std::optional<std::string> NormalizePrefix(std::string_view prefix)
        {
            prefix = Trim(prefix);
            if (prefix.size() != 3) return std::nullopt;
            std::string result;
            for (const unsigned char ch : prefix) {
                if (ch >= static_cast<unsigned char>('a') && ch <= static_cast<unsigned char>('z')) {
                    result.push_back(static_cast<char>(
                        ch - static_cast<unsigned char>('a') + static_cast<unsigned char>('A')));
                } else if (ch >= static_cast<unsigned char>('A') && ch <= static_cast<unsigned char>('Z')) {
                    result.push_back(static_cast<char>(ch));
                } else {
                    return std::nullopt;
                }
            }
            return result;
        }

        [[nodiscard]] std::string Key(std::string_view prefix, std::uint32_t code)
        {
            return std::format("{}:{}", prefix, code);
        }

        [[nodiscard]] std::string StripComment(std::string_view line)
        {
            bool quoted = false;
            bool escaped = false;
            for (std::size_t i = 0; i < line.size(); ++i) {
                const char ch = line[i];
                if (escaped) { escaped = false; continue; }
                if (ch == '\\' && quoted) { escaped = true; continue; }
                if (ch == '"') { quoted = !quoted; continue; }
                if (ch == '#' && !quoted) return std::string{ Trim(line.substr(0, i)) };
            }
            return std::string{ Trim(line) };
        }

        [[nodiscard]] std::optional<std::string> ParseString(std::string_view value)
        {
            value = Trim(value);
            if (value.size() < 2 || value.front() != '"' || value.back() != '"') return std::nullopt;
            std::string out;
            bool escaped = false;
            for (std::size_t i = 1; i + 1 < value.size(); ++i) {
                const char ch = value[i];
                if (escaped) {
                    switch (ch) {
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case '\\': out.push_back('\\'); break;
                    case '"': out.push_back('"'); break;
                    default: return std::nullopt;
                    }
                    escaped = false;
                } else if (ch == '\\') {
                    escaped = true;
                } else {
                    out.push_back(ch);
                }
            }
            return escaped ? std::nullopt : std::optional<std::string>{ std::move(out) };
        }

        [[nodiscard]] std::optional<std::uint32_t> ParseTable(std::string_view line)
        {
            line = Trim(line);
            constexpr std::string_view begin = "[error.";
            if (!line.starts_with(begin) || !line.ends_with(']')) return std::nullopt;
            const auto digits = line.substr(begin.size(), line.size() - begin.size() - 1);
            if (digits.empty() || digits.size() > 5) return std::nullopt;
            std::uint32_t code = 0;
            for (const char ch : digits) {
                if (!std::isdigit(static_cast<unsigned char>(ch))) return std::nullopt;
                code = code * 10 + static_cast<std::uint32_t>(ch - '0');
            }
            return code >= 1 && code <= 99999 ? std::optional<std::uint32_t>{ code } : std::nullopt;
        }

        [[nodiscard]] bool ParseStrict(
            const std::filesystem::path& path,
            std::string_view prefix,
            std::vector<ParsedEntry>& out,
            std::string& detail,
            const bool requireEntries)
        {
            std::ifstream input{ path };
            if (!input) { detail = std::format("Could not open '{}'", path.filename().string()); return false; }

            std::optional<std::uint32_t> currentCode;
            ParsedEntry current{};
            std::unordered_map<std::uint32_t, bool> seen;
            const auto commit = [&]() -> bool {
                if (!currentCode) return true;
                if (current.name.empty() || current.description.empty()) {
                    detail = std::format("[error.{:05}] requires both name and description", *currentCode);
                    return false;
                }
                const auto expectedPrefix = std::string{ prefix } + "_";
                if (!current.name.starts_with(expectedPrefix)) {
                    detail = std::format("Error name '{}' must start with '{}_'", current.name, prefix);
                    return false;
                }
                if (seen.contains(*currentCode)) {
                    detail = std::format("Duplicate [error.{:05}] table", *currentCode);
                    return false;
                }
                seen.emplace(*currentCode, true);
                current.code = *currentCode;
                out.push_back(current);
                current = {};
                currentCode.reset();
                return true;
            };

            std::string raw;
            std::size_t lineNo = 0;
            while (std::getline(input, raw)) {
                ++lineNo;
                if (!raw.empty() && raw.back() == '\r') raw.pop_back();
                const auto line = StripComment(raw);
                if (line.empty()) continue;

                if (line.front() == '[') {
                    if (!commit()) return false;
                    currentCode = ParseTable(line);
                    if (!currentCode) {
                        detail = std::format("Only [error.xxxxx] tables are allowed (line {})", lineNo);
                        return false;
                    }
                    continue;
                }

                if (!currentCode) {
                    detail = std::format("Only comments and [error.xxxxx] tables are allowed (line {})", lineNo);
                    return false;
                }
                const auto equals = line.find('=');
                if (equals == std::string::npos) { detail = std::format("Invalid TOML assignment at line {}", lineNo); return false; }
                const auto key = Trim(std::string_view{ line }.substr(0, equals));
                const auto value = ParseString(std::string_view{ line }.substr(equals + 1));
                if (!value) { detail = std::format("Catalog values must be quoted strings (line {})", lineNo); return false; }
                if (key == "name") {
                    if (!current.name.empty()) { detail = std::format("Duplicate name key (line {})", lineNo); return false; }
                    current.name = *value;
                } else if (key == "description") {
                    if (!current.description.empty()) { detail = std::format("Duplicate description key (line {})", lineNo); return false; }
                    current.description = *value;
                } else {
                    detail = std::format("Only name/description keys are allowed (line {})", lineNo);
                    return false;
                }
            }
            if (!commit()) return false;
            if (requireEntries && out.empty()) { detail = "Error catalog contains no [error.xxxxx] entries"; return false; }
            return true;
        }

        [[nodiscard]] std::filesystem::path FrameworkDirectory()
        {
            std::wstring buffer(32768, L'\0');
            const auto length = REX::W32::GetModuleFileNameW(REX::W32::GetCurrentModule(), buffer.data(), static_cast<std::uint32_t>(buffer.size()));
            if (length == 0 || length >= buffer.size()) return {};
            buffer.resize(length);
            return std::filesystem::path{ buffer }.parent_path();
        }

        [[nodiscard]] const ErrorEntry* Find(std::string_view prefix, HF_ErrorCode code)
        {
            const auto normalized = NormalizePrefix(prefix);
            if (!normalized) return nullptr;
            const auto it = g_entries.find(Key(*normalized, static_cast<std::uint32_t>(code)));
            return it == g_entries.end() ? nullptr : &it->second;
        }

        [[nodiscard]] const char* FrameworkFallbackName(HF_ErrorCode code) noexcept
        {
            const auto value = static_cast<std::uint32_t>(code);
            if (value >= 35000 && value < 36000) return "HFW_ERROR_POST_PROCESS";
            if (value >= 34000 && value < 35000) return "HFW_ERROR_PLAYER_MOVEMENT";
            if (value >= 33000 && value < 34000) return "HFW_ERROR_CONFIG_DOCUMENT";
            if (value >= 32000 && value < 33000) return "HFW_ERROR_ACTOR_VALUE";
            if (value >= 31000 && value < 32000) return "HFW_ERROR_RUNTIME_TUNING";
            if (value >= 30000 && value < 31000) return "HFW_ERROR_CPU_SCHEDULING";
            if (value >= 29000 && value < 30000) return "HFW_ERROR_STATE_FPS";
            if (value >= 28000 && value < 29000) return "HFW_ERROR_PRESENTATION_POLICY";
            if (value >= 27000 && value < 28000) return "HFW_ERROR_WINDOW";
            if (value >= 26000 && value < 27000) return "HFW_ERROR_FRAME_PACING";
            if (value >= 25000 && value < 26000) return "HFW_ERROR_FRAME_TIMING";
            if (value >= 24000 && value < 25000) return "HFW_ERROR_PRESENTATION";
            if (value >= 23000 && value < 24000) return "HFW_ERROR_SERIALIZATION";
            if (value >= 22000 && value < 23000) return "HFW_ERROR_RENDER_PIPELINE";
            if (value >= 21000 && value < 22000) return "HFW_ERROR_GAME_SETTING";
            if (value >= 20000 && value < 21000) return "HFW_ERROR_CONFIGURATION";
            if (value >= 19000 && value < 20000) return "HFW_ERROR_PERFORMANCE";
            if (value >= 18000 && value < 19000) return "HFW_ERROR_RESOURCE_OR_CAPABILITY";
            if (value >= 17000 && value < 18000) return "HFW_ERROR_HOOK";
            if (value >= 16000 && value < 17000) return "HFW_ERROR_TASK";
            if (value >= 15000 && value < 16000) return "HFW_ERROR_NATIVE_EXCEPTION";
            if (value >= 14000 && value < 15000) return "HFW_ERROR_UI_OR_LOGGING";
            if (value >= 13000 && value < 14000) return "HFW_ERROR_RELOCATION_OR_MEMORY";
            if (value >= 12000 && value < 13000) return "HFW_ERROR_EVENT_OR_CALLBACK";
            if (value >= 11000 && value < 12000) return "HFW_ERROR_MODULE_LIFECYCLE";
            if (value >= 10000 && value < 11000) return "HFW_ERROR_FRAMEWORK_CORE";
            return value == 0 ? "HFW_ERROR_NONE" : "HFW_ERROR_UNKNOWN";
        }
    }

    void InitializeErrorCatalog() noexcept
    {
        try {
            std::scoped_lock lock{ g_lock };
            if (g_initialized) return;
            g_initialized = true;
            g_prefixOwners.emplace("HFW", "HolyFramework");
            g_modulePrefixes.emplace(ToLower("HolyFramework"), "HFW");

            const auto path = FrameworkDirectory() / L"HolyFramework.toml";
            std::vector<ParsedEntry> parsed;
            std::string detail;
            if (!ParseStrict(path, "HFW", parsed, detail, true)) {
                REX::WARN("HolyFramework core error catalog rejected: {}", detail);
                return;
            }
            for (auto& entry : parsed) {
                g_entries.emplace(Key("HFW", entry.code), ErrorEntry{ std::move(entry.name), std::move(entry.description) });
            }
            REX::INFO("Core error catalog loaded: {} HFW entries", parsed.size());
        } catch (...) {
            REX::WARN("HolyFramework core error catalog initialization failed; coded diagnostics remain available");
        }
    }

    bool RegisterModuleErrorCatalog(
        const std::string_view moduleName,
        const std::string_view prefixText,
        const std::filesystem::path& path) noexcept
    {
        try {
            std::string detail;
            const auto prefix = NormalizePrefix(prefixText);
            if (moduleName.empty() || !prefix || *prefix == "HFW") {
                return false;
            }
            std::vector<ParsedEntry> parsed;
            if (!ParseStrict(path, *prefix, parsed, detail, false)) return false;

            std::scoped_lock lock{ g_lock };
            const auto lowerName = ToLower(moduleName);
            if (g_modulePrefixes.contains(lowerName)) {
                return false;
            }
            if (const auto owner = g_prefixOwners.find(*prefix); owner != g_prefixOwners.end()) {
                return false;
            }
            for (const auto& entry : parsed) {
                if (g_entries.contains(Key(*prefix, entry.code))) {
                    return false;
                }
            }
            g_prefixOwners.emplace(*prefix, std::string{ moduleName });
            g_modulePrefixes.emplace(lowerName, *prefix);
            for (auto& entry : parsed) {
                g_entries.emplace(Key(*prefix, entry.code), ErrorEntry{ std::move(entry.name), std::move(entry.description) });
            }
            return true;
        } catch (const std::exception&) {
            return false;
        } catch (...) {
            return false;
        }
    }

    void UnregisterModuleErrorCatalog(const std::string_view moduleName) noexcept
    {
        try {
            std::scoped_lock lock{ g_lock };
            const auto lower = ToLower(moduleName);
            const auto it = g_modulePrefixes.find(lower);
            if (it == g_modulePrefixes.end() || it->second == "HFW") return;
            const auto prefix = it->second;
            std::erase_if(g_entries, [&](const auto& pair) { return pair.first.starts_with(prefix + ":"); });
            g_prefixOwners.erase(prefix);
            g_modulePrefixes.erase(it);
        } catch (...) {
        }
    }

    std::string ResolveModuleErrorPrefix(const std::string_view moduleName) noexcept
    {
        if (moduleName.empty()) return "UNK";
        try {
            std::scoped_lock lock{ g_lock };
            const auto it = g_modulePrefixes.find(ToLower(moduleName));
            return it == g_modulePrefixes.end() ? "UNK" : it->second;
        } catch (...) { return "UNK"; }
    }

    const char* GetErrorName(const std::string_view prefix, const HF_ErrorCode code) noexcept
    {
        std::scoped_lock lock{ g_lock };
        if (const auto* entry = Find(prefix, code)) return entry->name.c_str();
        if (prefix == "HFW") return FrameworkFallbackName(code);
        return static_cast<std::uint32_t>(code) == 0 ? "MODULE_ERROR_NONE" : "MODULE_ERROR_UNKNOWN";
    }

    const char* GetErrorDescription(const std::string_view prefix, const HF_ErrorCode code) noexcept
    {
        std::scoped_lock lock{ g_lock };
        if (const auto* entry = Find(prefix, code)) return entry->description.c_str();
        if (static_cast<std::uint32_t>(code) == 0) return "No failure.";
        return prefix == "HFW" ? "HolyFramework diagnostic code is not present in the HFW catalog." : "Module diagnostic code is not present in its error catalog.";
    }

    std::string FormatErrorCode(const std::string_view prefix, const HF_ErrorCode code)
    {
        return std::format("{}-{:05}", NormalizePrefix(prefix).value_or("UNK"), static_cast<std::uint32_t>(code));
    }

    const char* GetModuleErrorName(const std::string_view moduleName, const HF_ErrorCode code) noexcept { return GetErrorName(ResolveModuleErrorPrefix(moduleName), code); }
    const char* GetModuleErrorDescription(const std::string_view moduleName, const HF_ErrorCode code) noexcept { return GetErrorDescription(ResolveModuleErrorPrefix(moduleName), code); }
    std::string FormatModuleErrorCode(const std::string_view moduleName, const HF_ErrorCode code) { return FormatErrorCode(ResolveModuleErrorPrefix(moduleName), code); }
}
