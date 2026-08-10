#pragma once

namespace HolyFramework
{
    class GameSettingsManager final
    {
    public:
        static GameSettingsManager& GetSingleton() noexcept;

        [[nodiscard]] bool Exists(HF_GameSettingSource a_source, std::string_view a_name) const noexcept;
        [[nodiscard]] HF_GameSettingType GetType(HF_GameSettingSource a_source, std::string_view a_name) const noexcept;
        bool GetBool(HF_GameSettingSource a_source, std::string_view a_name, bool& a_out) const noexcept;
        bool GetInt32(HF_GameSettingSource a_source, std::string_view a_name, std::int32_t& a_out) const noexcept;
        bool GetUInt32(HF_GameSettingSource a_source, std::string_view a_name, std::uint32_t& a_out) const noexcept;
        bool GetFloat(HF_GameSettingSource a_source, std::string_view a_name, float& a_out) const noexcept;
        bool GetString(HF_GameSettingSource a_source, std::string_view a_name, std::string& a_out) const noexcept;

        bool SetBool(HF_GameSettingSource a_source, std::string_view a_name, bool a_value) noexcept;
        bool SetInt32(HF_GameSettingSource a_source, std::string_view a_name, std::int32_t a_value) noexcept;
        bool SetUInt32(HF_GameSettingSource a_source, std::string_view a_name, std::uint32_t a_value) noexcept;
        bool SetFloat(HF_GameSettingSource a_source, std::string_view a_name, float a_value) noexcept;
        bool Release(HF_GameSettingSource a_source, std::string_view a_name, bool a_restoreOriginal) noexcept;

        // Cleanup path for a module that rejected load or is otherwise being
        // retired. Safe restores are attempted; conflicts are left untouched.
        std::uint32_t ReleaseOwnedBy(std::string_view a_moduleName) noexcept;

    private:
        struct Value
        {
            HF_GameSettingType type{ HF_GAME_SETTING_TYPE_UNKNOWN };
            bool b{ false };
            std::int32_t i{ 0 };
            std::uint32_t u{ 0 };
            float f{ 0.0F };
        };

        struct Mutation
        {
            HF_GameSettingSource source{ HF_GAME_SETTING_SOURCE_FALLOUT_INI };
            std::string name;
            std::string owner;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
            Value original{};
            Value lastWritten{};
        };

        GameSettingsManager() = default;

        [[nodiscard]] static RE::Setting* Resolve(HF_GameSettingSource a_source, std::string_view a_name) noexcept;
        [[nodiscard]] static HF_GameSettingType MapType(RE::Setting::SETTING_TYPE a_type) noexcept;
        [[nodiscard]] static bool ReadValue(RE::Setting* a_setting, Value& a_out) noexcept;
        [[nodiscard]] static bool ValuesEqual(const Value& a_left, const Value& a_right) noexcept;
        [[nodiscard]] static bool WriteValue(RE::Setting* a_setting, const Value& a_value) noexcept;
        [[nodiscard]] static bool ValidSource(HF_GameSettingSource a_source) noexcept;
        [[nodiscard]] static bool NamesEqual(std::string_view a_left, std::string_view a_right) noexcept;

        bool SetValue(HF_GameSettingSource a_source, std::string_view a_name, const Value& a_value) noexcept;
        bool ReleaseMutation(std::size_t a_index, bool a_restore, bool a_forceRemoveOnConflict) noexcept;

        mutable std::mutex _lock;
        std::vector<Mutation> _mutations;
    };
}
