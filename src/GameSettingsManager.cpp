#include "pch.h"
#include "GameSettingsManager.h"

#include "Diagnostics.h"
#include "ModuleContext.h"

namespace HolyFramework
{
    GameSettingsManager& GameSettingsManager::GetSingleton() noexcept
    {
        static GameSettingsManager* instance = new GameSettingsManager();
        return *instance;
    }

    bool GameSettingsManager::ValidSource(const HF_GameSettingSource a_source) noexcept
    {
        return a_source == HF_GAME_SETTING_SOURCE_FALLOUT_INI || a_source == HF_GAME_SETTING_SOURCE_GAME;
    }

    bool GameSettingsManager::NamesEqual(const std::string_view a_left, const std::string_view a_right) noexcept
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

    RE::Setting* GameSettingsManager::Resolve(const HF_GameSettingSource a_source, const std::string_view a_name) noexcept
    {
        if (!ValidSource(a_source) || a_name.empty()) {
            return nullptr;
        }
        try {
            if (a_source == HF_GAME_SETTING_SOURCE_FALLOUT_INI) {
                return RE::GetINISetting(a_name);
            }
            auto* const collection = RE::GameSettingCollection::GetSingleton();
            return collection ? collection->GetSetting(a_name) : nullptr;
        } catch (...) {
            return nullptr;
        }
    }

    HF_GameSettingType GameSettingsManager::MapType(const RE::Setting::SETTING_TYPE a_type) noexcept
    {
        switch (a_type) {
        case RE::Setting::SETTING_TYPE::kBinary:
            return HF_GAME_SETTING_TYPE_BOOL;
        case RE::Setting::SETTING_TYPE::kInt:
            return HF_GAME_SETTING_TYPE_INT32;
        case RE::Setting::SETTING_TYPE::kUInt:
            return HF_GAME_SETTING_TYPE_UINT32;
        case RE::Setting::SETTING_TYPE::kFloat:
            return HF_GAME_SETTING_TYPE_FLOAT;
        case RE::Setting::SETTING_TYPE::kString:
            return HF_GAME_SETTING_TYPE_STRING;
        case RE::Setting::SETTING_TYPE::kChar:
            return HF_GAME_SETTING_TYPE_CHAR;
        case RE::Setting::SETTING_TYPE::kUChar:
            return HF_GAME_SETTING_TYPE_UCHAR;
        default:
            return HF_GAME_SETTING_TYPE_UNKNOWN;
        }
    }

    bool GameSettingsManager::ReadValue(RE::Setting* const a_setting, Value& a_out) noexcept
    {
        if (!a_setting) {
            return false;
        }
        try {
            a_out = {};
            a_out.type = MapType(a_setting->GetType());
            switch (a_out.type) {
            case HF_GAME_SETTING_TYPE_BOOL:
                a_out.b = a_setting->GetBinary();
                return true;
            case HF_GAME_SETTING_TYPE_INT32:
                a_out.i = a_setting->GetInt();
                return true;
            case HF_GAME_SETTING_TYPE_UINT32:
                a_out.u = a_setting->GetUInt();
                return true;
            case HF_GAME_SETTING_TYPE_FLOAT:
                a_out.f = a_setting->GetFloat();
                return true;
            case HF_GAME_SETTING_TYPE_CHAR:
                a_out.i = static_cast<std::int32_t>(a_setting->GetChar());
                return true;
            case HF_GAME_SETTING_TYPE_UCHAR:
                a_out.u = static_cast<std::uint32_t>(a_setting->GetUChar());
                return true;
            default:
                return false;
            }
        } catch (...) {
            return false;
        }
    }

    bool GameSettingsManager::ValuesEqual(const Value& a_left, const Value& a_right) noexcept
    {
        if (a_left.type != a_right.type) {
            return false;
        }
        switch (a_left.type) {
        case HF_GAME_SETTING_TYPE_BOOL:
            return a_left.b == a_right.b;
        case HF_GAME_SETTING_TYPE_INT32:
        case HF_GAME_SETTING_TYPE_CHAR:
            return a_left.i == a_right.i;
        case HF_GAME_SETTING_TYPE_UINT32:
        case HF_GAME_SETTING_TYPE_UCHAR:
            return a_left.u == a_right.u;
        case HF_GAME_SETTING_TYPE_FLOAT:
            return a_left.f == a_right.f;
        default:
            return false;
        }
    }

    bool GameSettingsManager::WriteValue(RE::Setting* const a_setting, const Value& a_value) noexcept
    {
        if (!a_setting || MapType(a_setting->GetType()) != a_value.type) {
            return false;
        }
        try {
            switch (a_value.type) {
            case HF_GAME_SETTING_TYPE_BOOL:
                a_setting->SetBinary(a_value.b);
                break;
            case HF_GAME_SETTING_TYPE_INT32:
                a_setting->SetInt(a_value.i);
                break;
            case HF_GAME_SETTING_TYPE_UINT32:
                a_setting->SetUInt(a_value.u);
                break;
            case HF_GAME_SETTING_TYPE_FLOAT:
                a_setting->SetFloat(a_value.f);
                break;
            case HF_GAME_SETTING_TYPE_CHAR:
                a_setting->SetChar(static_cast<char>(a_value.i));
                break;
            case HF_GAME_SETTING_TYPE_UCHAR:
                a_setting->SetUChar(static_cast<std::uint8_t>(a_value.u));
                break;
            default:
                return false;
            }

            Value verify{};
            return ReadValue(a_setting, verify) && ValuesEqual(verify, a_value);
        } catch (...) {
            return false;
        }
    }

    bool GameSettingsManager::Exists(const HF_GameSettingSource a_source, const std::string_view a_name) const noexcept
    {
        return Resolve(a_source, a_name) != nullptr;
    }

    HF_GameSettingType GameSettingsManager::GetType(const HF_GameSettingSource a_source, const std::string_view a_name) const noexcept
    {
        auto* const setting = Resolve(a_source, a_name);
        if (!setting) {
            return HF_GAME_SETTING_TYPE_UNKNOWN;
        }
        try {
            return MapType(setting->GetType());
        } catch (...) {
            return HF_GAME_SETTING_TYPE_UNKNOWN;
        }
    }

    bool GameSettingsManager::GetBool(const HF_GameSettingSource a_source, const std::string_view a_name, bool& a_out) const noexcept
    {
        auto* const setting = Resolve(a_source, a_name);
        if (!setting || MapType(setting->GetType()) != HF_GAME_SETTING_TYPE_BOOL) {
            return false;
        }
        a_out = setting->GetBinary();
        return true;
    }

    bool GameSettingsManager::GetInt32(const HF_GameSettingSource a_source, const std::string_view a_name, std::int32_t& a_out) const noexcept
    {
        auto* const setting = Resolve(a_source, a_name);
        if (!setting) {
            return false;
        }
        const auto type = MapType(setting->GetType());
        if (type == HF_GAME_SETTING_TYPE_INT32) {
            a_out = setting->GetInt();
            return true;
        }
        if (type == HF_GAME_SETTING_TYPE_CHAR) {
            a_out = static_cast<std::int32_t>(setting->GetChar());
            return true;
        }
        return false;
    }

    bool GameSettingsManager::GetUInt32(const HF_GameSettingSource a_source, const std::string_view a_name, std::uint32_t& a_out) const noexcept
    {
        auto* const setting = Resolve(a_source, a_name);
        if (!setting) {
            return false;
        }
        const auto type = MapType(setting->GetType());
        if (type == HF_GAME_SETTING_TYPE_UINT32) {
            a_out = setting->GetUInt();
            return true;
        }
        if (type == HF_GAME_SETTING_TYPE_UCHAR) {
            a_out = static_cast<std::uint32_t>(setting->GetUChar());
            return true;
        }
        return false;
    }

    bool GameSettingsManager::GetFloat(const HF_GameSettingSource a_source, const std::string_view a_name, float& a_out) const noexcept
    {
        auto* const setting = Resolve(a_source, a_name);
        if (!setting || MapType(setting->GetType()) != HF_GAME_SETTING_TYPE_FLOAT) {
            return false;
        }
        a_out = setting->GetFloat();
        return true;
    }

    bool GameSettingsManager::GetString(const HF_GameSettingSource a_source, const std::string_view a_name, std::string& a_out) const noexcept
    {
        auto* const setting = Resolve(a_source, a_name);
        if (!setting || MapType(setting->GetType()) != HF_GAME_SETTING_TYPE_STRING) {
            return false;
        }
        try {
            a_out = setting->GetString();
            return true;
        } catch (...) {
            return false;
        }
    }

    bool GameSettingsManager::SetValue(
        const HF_GameSettingSource a_source,
        const std::string_view a_name,
        const Value& a_value) noexcept
    {
        const auto context = ModuleContext::Current();
        if (!context.name || !*context.name) {
            Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_GAME_SETTING_OWNER_UNKNOWN);
            return false;
        }
        if (!ValidSource(a_source) || a_name.empty() || a_name.size() > 127) {
            Diagnostics::ReportFrameworkFailureForModule(
                context.name, context.logger, HF_ERROR_GAME_SETTING_INVALID_REQUEST);
            return false;
        }

        auto* const setting = Resolve(a_source, a_name);
        if (!setting) {
            return false;
        }
        if (MapType(setting->GetType()) != a_value.type) {
            Diagnostics::ReportFrameworkFailureForModule(
                context.name, context.logger, HF_ERROR_GAME_SETTING_TYPE_MISMATCH);
            return false;
        }

        {
            std::scoped_lock lock{ _lock };
            auto it = std::ranges::find_if(_mutations, [&](const Mutation& a_mutation) {
                return a_mutation.source == a_source && NamesEqual(a_mutation.name, a_name);
            });

            Value current{};
            if (!ReadValue(setting, current)) {
                return false;
            }

            if (it != _mutations.end()) {
                if (NamesEqual(it->owner, context.name) &&
                    ValuesEqual(current, it->lastWritten)) {
                    if (WriteValue(setting, a_value)) {
                        it->lastWritten = a_value;
                        return true;
                    }
                    Diagnostics::ReportFrameworkFailureForModule(
                        context.name, context.logger, HF_ERROR_GAME_SETTING_WRITE_FAILED);
                    return false;
                }
            } else {
                Mutation mutation{};
                mutation.source = a_source;
                mutation.name = std::string{ a_name };
                mutation.owner = context.name;
                mutation.logger = context.logger;
                mutation.original = current;
                mutation.lastWritten = current;
                if (!WriteValue(setting, a_value)) {
                    Diagnostics::ReportFrameworkFailureForModule(
                        context.name, context.logger, HF_ERROR_GAME_SETTING_WRITE_FAILED);
                    return false;
                }
                mutation.lastWritten = a_value;
                _mutations.push_back(std::move(mutation));
                return true;
            }
        }

        Diagnostics::ReportFrameworkWarningForModule(
            context.name, context.logger, HF_ERROR_GAME_SETTING_WRITE_CONFLICT);
        return false;
    }

    bool GameSettingsManager::SetBool(const HF_GameSettingSource a_source, const std::string_view a_name, const bool a_value) noexcept
    {
        Value value{};
        value.type = HF_GAME_SETTING_TYPE_BOOL;
        value.b = a_value;
        return SetValue(a_source, a_name, value);
    }

    bool GameSettingsManager::SetInt32(const HF_GameSettingSource a_source, const std::string_view a_name, const std::int32_t a_value) noexcept
    {
        Value value{};
        value.type = GetType(a_source, a_name) == HF_GAME_SETTING_TYPE_CHAR ?
            HF_GAME_SETTING_TYPE_CHAR : HF_GAME_SETTING_TYPE_INT32;
        if (value.type == HF_GAME_SETTING_TYPE_CHAR &&
            (a_value < static_cast<std::int32_t>((std::numeric_limits<char>::min)()) ||
             a_value > static_cast<std::int32_t>((std::numeric_limits<char>::max)()))) {
            return false;
        }
        value.i = a_value;
        return SetValue(a_source, a_name, value);
    }

    bool GameSettingsManager::SetUInt32(const HF_GameSettingSource a_source, const std::string_view a_name, const std::uint32_t a_value) noexcept
    {
        Value value{};
        value.type = GetType(a_source, a_name) == HF_GAME_SETTING_TYPE_UCHAR ?
            HF_GAME_SETTING_TYPE_UCHAR : HF_GAME_SETTING_TYPE_UINT32;
        if (value.type == HF_GAME_SETTING_TYPE_UCHAR && a_value > 0xFFu) {
            return false;
        }
        value.u = a_value;
        return SetValue(a_source, a_name, value);
    }

    bool GameSettingsManager::SetFloat(const HF_GameSettingSource a_source, const std::string_view a_name, const float a_value) noexcept
    {
        Value value{};
        value.type = HF_GAME_SETTING_TYPE_FLOAT;
        value.f = a_value;
        return SetValue(a_source, a_name, value);
    }

    bool GameSettingsManager::ReleaseMutation(const std::size_t a_index, const bool a_restore, const bool a_forceRemoveOnConflict) noexcept
    {
        if (a_index >= _mutations.size()) {
            return false;
        }
        auto& mutation = _mutations[a_index];
        if (!a_restore) {
            _mutations.erase(_mutations.begin() + static_cast<std::ptrdiff_t>(a_index));
            return true;
        }

        auto* const setting = Resolve(mutation.source, mutation.name);
        Value current{};
        if (!setting || !ReadValue(setting, current) || !ValuesEqual(current, mutation.lastWritten)) {
            Diagnostics::ReportFrameworkWarningForModule(
                mutation.owner, mutation.logger, HF_ERROR_GAME_SETTING_RESTORE_CONFLICT);
            if (a_forceRemoveOnConflict) {
                _mutations.erase(_mutations.begin() + static_cast<std::ptrdiff_t>(a_index));
            }
            return false;
        }
        if (!WriteValue(setting, mutation.original)) {
            Diagnostics::ReportFrameworkFailureForModule(
                mutation.owner, mutation.logger, HF_ERROR_GAME_SETTING_WRITE_FAILED);
            if (a_forceRemoveOnConflict) {
                _mutations.erase(_mutations.begin() + static_cast<std::ptrdiff_t>(a_index));
            }
            return false;
        }
        _mutations.erase(_mutations.begin() + static_cast<std::ptrdiff_t>(a_index));
        return true;
    }

    bool GameSettingsManager::Release(
        const HF_GameSettingSource a_source,
        const std::string_view a_name,
        const bool a_restoreOriginal) noexcept
    {
        const auto context = ModuleContext::Current();
        if (!context.name || !*context.name) {
            return false;
        }
        std::scoped_lock lock{ _lock };
        const auto it = std::ranges::find_if(_mutations, [&](const Mutation& a_mutation) {
            return a_mutation.source == a_source && NamesEqual(a_mutation.name, a_name);
        });
        if (it == _mutations.end()) {
            return false;
        }
        if (!NamesEqual(it->owner, context.name)) {
            Diagnostics::ReportFrameworkWarningForModule(
                context.name, context.logger, HF_ERROR_GAME_SETTING_WRITE_CONFLICT);
            return false;
        }
        return ReleaseMutation(static_cast<std::size_t>(std::distance(_mutations.begin(), it)), a_restoreOriginal, false);
    }

    std::uint32_t GameSettingsManager::ReleaseOwnedBy(const std::string_view a_moduleName) noexcept
    {
        if (a_moduleName.empty()) {
            return 0;
        }
        std::scoped_lock lock{ _lock };
        std::uint32_t removed = 0;
        for (std::size_t i = _mutations.size(); i-- > 0;) {
            if (!NamesEqual(_mutations[i].owner, a_moduleName)) {
                continue;
            }
            ReleaseMutation(i, true, true);
            ++removed;
        }
        return removed;
    }
}
