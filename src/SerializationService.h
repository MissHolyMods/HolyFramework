#pragma once

namespace F4SE
{
    class SerializationInterface;
}

namespace HolyFramework
{
    class SerializationService final
    {
    public:
        static SerializationService& GetSingleton() noexcept;

        bool Initialize() noexcept;
        [[nodiscard]] bool IsAvailable() const noexcept;

        bool SetRecordOwned(
            std::string_view a_owner,
            HF_LogHandle a_logger,
            std::uint32_t a_key,
            std::uint32_t a_version,
            const void* a_data,
            std::uint32_t a_dataSize) noexcept;

        bool GetRecordInfoOwned(
            std::string_view a_owner,
            std::uint32_t a_key,
            HF_SerializationRecordInfoV1& a_outInfo) const noexcept;

        bool ReadRecordOwned(
            std::string_view a_owner,
            std::uint32_t a_key,
            void* a_buffer,
            std::uint32_t a_bufferSize,
            std::uint32_t* a_outBytes) const noexcept;

        bool RemoveRecordOwned(std::string_view a_owner, std::uint32_t a_key) noexcept;
        std::uint32_t ClearRecordsOwned(std::string_view a_owner) noexcept;
        std::uint32_t GetRecordCountOwned(std::string_view a_owner) const noexcept;
        [[nodiscard]] std::uint64_t GetGeneration() const noexcept;

    private:
        struct Record
        {
            std::uint32_t version{};
            std::vector<std::uint8_t> data;
        };

        struct OwnerRecords
        {
            std::string displayName;
            std::unordered_map<std::uint32_t, Record> records;
        };

        using Store = std::unordered_map<std::string, OwnerRecords>;

        struct PersistRecord
        {
            std::string owner;
            std::uint32_t key{};
            std::uint32_t version{};
            std::vector<std::uint8_t> data;
        };

        static void SaveCallback(const F4SE::SerializationInterface* a_intfc);
        static void LoadCallback(const F4SE::SerializationInterface* a_intfc);
        static void RevertCallback(const F4SE::SerializationInterface* a_intfc);

        void OnSave(const F4SE::SerializationInterface* a_intfc) noexcept;
        void OnLoad(const F4SE::SerializationInterface* a_intfc) noexcept;
        void OnRevert() noexcept;

        static std::string NormalizeOwner(std::string_view a_owner);
        static bool SkipRecordData(
            const F4SE::SerializationInterface* a_intfc,
            std::uint32_t a_length) noexcept;
        static std::uint64_t OwnerDataSize(const OwnerRecords& a_owner) noexcept;
        static void ReportFailure(HF_ErrorCode a_code) noexcept;

        mutable std::mutex _lock;
        Store _store;
        const F4SE::SerializationInterface* _interface{};
        std::atomic_bool _initialized{ false };
        std::atomic_uint64_t _generation{ 1 };
    };
}
