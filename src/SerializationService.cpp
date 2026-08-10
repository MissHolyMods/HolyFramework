#include "pch.h"
#include "SerializationService.h"

#include "Diagnostics.h"

namespace HolyFramework
{
    namespace
    {
        [[nodiscard]] constexpr std::uint32_t MakeFourCC(
            const char a_a,
            const char a_b,
            const char a_c,
            const char a_d) noexcept
        {
            return static_cast<std::uint32_t>(static_cast<unsigned char>(a_a)) |
                   (static_cast<std::uint32_t>(static_cast<unsigned char>(a_b)) << 8u) |
                   (static_cast<std::uint32_t>(static_cast<unsigned char>(a_c)) << 16u) |
                   (static_cast<std::uint32_t>(static_cast<unsigned char>(a_d)) << 24u);
        }

        inline constexpr std::uint32_t kFrameworkSerializationID = MakeFourCC('H', 'F', 'W', 'K');
        inline constexpr std::uint32_t kModuleDataRecordType = MakeFourCC('H', 'F', 'M', 'D');
        inline constexpr std::uint32_t kEnvelopeVersion = 1;
        inline constexpr std::uint32_t kMaximumOwnerNameBytes = 255;
        inline constexpr std::uint64_t kMaximumOwnerBytes = 16ull * 1024ull * 1024ull;
        inline constexpr std::uint64_t kMaximumLoadedBytes = 64ull * 1024ull * 1024ull;
        inline constexpr std::uint32_t kMaximumLoadedRecords = 4096;

        struct PersistHeaderV1
        {
            std::uint32_t headerSize;
            std::uint32_t envelopeVersion;
            std::uint32_t ownerNameSize;
            std::uint32_t key;
            std::uint32_t recordVersion;
            std::uint32_t dataSize;
        };

        static_assert(sizeof(PersistHeaderV1) == 24);
    }

    SerializationService& SerializationService::GetSingleton() noexcept
    {
        static SerializationService* instance = new SerializationService();
        return *instance;
    }

    bool SerializationService::Initialize() noexcept
    {
        bool expected = false;
        if (!_initialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return IsAvailable();
        }

        try {
            const auto* const intfc = F4SE::GetSerializationInterface();
            if (!intfc) {
                ReportFailure(
                    HF_ERROR_SERIALIZATION_UNAVAILABLE);
                return false;
            }

            _interface = intfc;
            intfc->SetUniqueID(kFrameworkSerializationID);
            intfc->SetSaveCallback(&SaveCallback);
            intfc->SetLoadCallback(&LoadCallback);
            intfc->SetRevertCallback(&RevertCallback);
            REX::INFO("HolyFramework serialization service initialized: HFWK");
            return true;
        } catch (const std::exception&) {
            _interface = nullptr;
            ReportFailure(HF_ERROR_SERIALIZATION_UNAVAILABLE);
        } catch (...) {
            _interface = nullptr;
            ReportFailure(
                HF_ERROR_SERIALIZATION_UNAVAILABLE);
        }
        return false;
    }

    bool SerializationService::IsAvailable() const noexcept
    {
        return _interface != nullptr;
    }

    std::string SerializationService::NormalizeOwner(const std::string_view a_owner)
    {
        std::string key{ a_owner };
        for (auto& ch : key) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return key;
    }

    std::uint64_t SerializationService::OwnerDataSize(const OwnerRecords& a_owner) noexcept
    {
        std::uint64_t size = 0;
        for (const auto& [key, record] : a_owner.records) {
            (void)key;
            size += record.data.size();
        }
        return size;
    }

    void SerializationService::ReportFailure(const HF_ErrorCode a_code) noexcept
    {
        Diagnostics::ReportFrameworkFailureForModule(
            "HolyFramework",
            HF_INVALID_LOG_HANDLE,
            a_code);
    }

    bool SerializationService::SetRecordOwned(
        const std::string_view a_owner,
        const HF_LogHandle a_logger,
        const std::uint32_t a_key,
        const std::uint32_t a_version,
        const void* const a_data,
        const std::uint32_t a_dataSize) noexcept
    {
        if (!IsAvailable()) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_owner.empty() ? std::string_view{ "<unknown>" } : a_owner,
                a_logger,
                HF_ERROR_SERIALIZATION_UNAVAILABLE);
            return false;
        }
        if (a_owner.empty() || a_owner.size() > kMaximumOwnerNameBytes || a_key == 0 ||
            (a_dataSize != 0 && !a_data)) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_owner.empty() ? std::string_view{ "<unknown>" } : a_owner,
                a_logger,
                HF_ERROR_SERIALIZATION_INVALID_REQUEST);
            return false;
        }
        if (a_dataSize > HF_SERIALIZATION_MAX_RECORD_SIZE) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_owner,
                a_logger,
                HF_ERROR_SERIALIZATION_RECORD_TOO_LARGE);
            return false;
        }

        try {
            std::vector<std::uint8_t> bytes(a_dataSize);
            if (a_dataSize != 0) {
                std::memcpy(bytes.data(), a_data, a_dataSize);
            }

            std::scoped_lock lock{ _lock };
            const auto ownerKey = NormalizeOwner(a_owner);
            auto& owner = _store[ownerKey];
            if (owner.displayName.empty()) {
                owner.displayName = std::string{ a_owner };
            }

            const auto existing = owner.records.find(a_key);
            auto projected = OwnerDataSize(owner);
            if (existing != owner.records.end()) {
                projected -= existing->second.data.size();
            }
            projected += bytes.size();
            if (projected > kMaximumOwnerBytes) {
                Diagnostics::ReportFrameworkFailureForModule(
                    a_owner,
                    a_logger,
                    HF_ERROR_SERIALIZATION_MODULE_QUOTA_EXCEEDED);
                return false;
            }

            owner.records[a_key] = Record{ .version = a_version, .data = std::move(bytes) };
            _generation.fetch_add(1, std::memory_order_relaxed);
            return true;
        } catch (...) {
            Diagnostics::ReportFrameworkFailureForModule(
                a_owner,
                a_logger,
                HF_ERROR_SERIALIZATION_WRITE_FAILED);
            return false;
        }
    }

    bool SerializationService::GetRecordInfoOwned(
        const std::string_view a_owner,
        const std::uint32_t a_key,
        HF_SerializationRecordInfoV1& a_outInfo) const noexcept
    {
        a_outInfo = {};
        a_outInfo.structSize = sizeof(HF_SerializationRecordInfoV1);
        if (a_owner.empty() || a_key == 0) {
            return false;
        }

        try {
            std::scoped_lock lock{ _lock };
            const auto ownerIt = _store.find(NormalizeOwner(a_owner));
            if (ownerIt == _store.end()) {
                return false;
            }
            const auto recordIt = ownerIt->second.records.find(a_key);
            if (recordIt == ownerIt->second.records.end()) {
                return false;
            }
            a_outInfo.key = a_key;
            a_outInfo.version = recordIt->second.version;
            a_outInfo.dataSize = static_cast<std::uint32_t>(recordIt->second.data.size());
            return true;
        } catch (...) {
            return false;
        }
    }

    bool SerializationService::ReadRecordOwned(
        const std::string_view a_owner,
        const std::uint32_t a_key,
        void* const a_buffer,
        const std::uint32_t a_bufferSize,
        std::uint32_t* const a_outBytes) const noexcept
    {
        if (a_outBytes) {
            *a_outBytes = 0;
        }
        if (a_owner.empty() || a_key == 0) {
            return false;
        }

        try {
            std::scoped_lock lock{ _lock };
            const auto ownerIt = _store.find(NormalizeOwner(a_owner));
            if (ownerIt == _store.end()) {
                return false;
            }
            const auto recordIt = ownerIt->second.records.find(a_key);
            if (recordIt == ownerIt->second.records.end()) {
                return false;
            }

            const auto required = static_cast<std::uint32_t>(recordIt->second.data.size());
            if (a_outBytes) {
                *a_outBytes = required;
            }
            if (required == 0) {
                return true;
            }
            if (!a_buffer || a_bufferSize < required) {
                return false;
            }
            std::memcpy(a_buffer, recordIt->second.data.data(), required);
            return true;
        } catch (...) {
            return false;
        }
    }

    bool SerializationService::RemoveRecordOwned(
        const std::string_view a_owner,
        const std::uint32_t a_key) noexcept
    {
        if (a_owner.empty() || a_key == 0) {
            return false;
        }
        try {
            std::scoped_lock lock{ _lock };
            const auto ownerKey = NormalizeOwner(a_owner);
            const auto ownerIt = _store.find(ownerKey);
            if (ownerIt == _store.end()) {
                return false;
            }
            if (ownerIt->second.records.erase(a_key) == 0) {
                return false;
            }
            if (ownerIt->second.records.empty()) {
                _store.erase(ownerIt);
            }
            _generation.fetch_add(1, std::memory_order_relaxed);
            return true;
        } catch (...) {
            return false;
        }
    }

    std::uint32_t SerializationService::ClearRecordsOwned(const std::string_view a_owner) noexcept
    {
        if (a_owner.empty()) {
            return 0;
        }
        try {
            std::scoped_lock lock{ _lock };
            const auto ownerIt = _store.find(NormalizeOwner(a_owner));
            if (ownerIt == _store.end()) {
                return 0;
            }
            const auto count = static_cast<std::uint32_t>(ownerIt->second.records.size());
            _store.erase(ownerIt);
            if (count != 0) {
                _generation.fetch_add(1, std::memory_order_relaxed);
            }
            return count;
        } catch (...) {
            return 0;
        }
    }

    std::uint32_t SerializationService::GetRecordCountOwned(const std::string_view a_owner) const noexcept
    {
        if (a_owner.empty()) {
            return 0;
        }
        try {
            std::scoped_lock lock{ _lock };
            const auto ownerIt = _store.find(NormalizeOwner(a_owner));
            return ownerIt == _store.end() ? 0u : static_cast<std::uint32_t>(ownerIt->second.records.size());
        } catch (...) {
            return 0;
        }
    }

    std::uint64_t SerializationService::GetGeneration() const noexcept
    {
        return _generation.load(std::memory_order_acquire);
    }

    void SerializationService::SaveCallback(const F4SE::SerializationInterface* const a_intfc)
    {
        GetSingleton().OnSave(a_intfc);
    }

    void SerializationService::LoadCallback(const F4SE::SerializationInterface* const a_intfc)
    {
        GetSingleton().OnLoad(a_intfc);
    }

    void SerializationService::RevertCallback(const F4SE::SerializationInterface*)
    {
        GetSingleton().OnRevert();
    }

    void SerializationService::OnSave(const F4SE::SerializationInterface* const a_intfc) noexcept
    {
        if (!a_intfc) {
            ReportFailure(HF_ERROR_SERIALIZATION_WRITE_FAILED);
            return;
        }

        std::vector<PersistRecord> records;
        try {
            {
                std::scoped_lock lock{ _lock };
                std::size_t count = 0;
                for (const auto& [key, owner] : _store) {
                    (void)key;
                    count += owner.records.size();
                }
                records.reserve(count);
                for (const auto& [key, owner] : _store) {
                    (void)key;
                    for (const auto& [recordKey, record] : owner.records) {
                        records.push_back(PersistRecord{
                            .owner = owner.displayName,
                            .key = recordKey,
                            .version = record.version,
                            .data = record.data
                        });
                    }
                }
            }

            std::stable_sort(records.begin(), records.end(), [](const PersistRecord& a_left, const PersistRecord& a_right) {
                const auto left = NormalizeOwner(a_left.owner);
                const auto right = NormalizeOwner(a_right.owner);
                if (left != right) {
                    return left < right;
                }
                return a_left.key < a_right.key;
            });

            std::uint32_t failed = 0;
            for (const auto& record : records) {
                const PersistHeaderV1 header{
                    .headerSize = sizeof(PersistHeaderV1),
                    .envelopeVersion = kEnvelopeVersion,
                    .ownerNameSize = static_cast<std::uint32_t>(record.owner.size()),
                    .key = record.key,
                    .recordVersion = record.version,
                    .dataSize = static_cast<std::uint32_t>(record.data.size())
                };

                bool ok = a_intfc->OpenRecord(kModuleDataRecordType, kEnvelopeVersion);
                ok = ok && a_intfc->WriteRecordData(&header, sizeof(header));
                if (ok && !record.owner.empty()) {
                    ok = a_intfc->WriteRecordData(record.owner.data(), static_cast<std::uint32_t>(record.owner.size()));
                }
                if (ok && !record.data.empty()) {
                    ok = a_intfc->WriteRecordData(record.data.data(), static_cast<std::uint32_t>(record.data.size()));
                }
                if (!ok) {
                    ++failed;
                }
            }

            if (failed != 0) {
                ReportFailure(
                    HF_ERROR_SERIALIZATION_WRITE_FAILED);
            }
        } catch (const std::exception&) {
            ReportFailure(HF_ERROR_SERIALIZATION_WRITE_FAILED);
        } catch (...) {
            ReportFailure(HF_ERROR_SERIALIZATION_WRITE_FAILED);
        }
    }

    bool SerializationService::SkipRecordData(
        const F4SE::SerializationInterface* const a_intfc,
        std::uint32_t a_length) noexcept
    {
        if (!a_intfc) {
            return false;
        }
        std::array<std::uint8_t, 4096> scratch{};
        while (a_length != 0) {
            const auto chunk = (std::min)(a_length, static_cast<std::uint32_t>(scratch.size()));
            const auto read = a_intfc->ReadRecordData(scratch.data(), chunk);
            if (read != chunk) {
                return false;
            }
            a_length -= chunk;
        }
        return true;
    }

    void SerializationService::OnLoad(const F4SE::SerializationInterface* const a_intfc) noexcept
    {
        if (!a_intfc) {
            ReportFailure(HF_ERROR_SERIALIZATION_READ_FAILED);
            return;
        }

        Store loaded;
        std::uint64_t totalBytes = 0;
        std::uint32_t accepted = 0;
        std::uint32_t skipped = 0;

        try {
            std::uint32_t type = 0;
            std::uint32_t version = 0;
            std::uint32_t length = 0;
            while (a_intfc->GetNextRecordInfo(type, version, length)) {
                if (type != kModuleDataRecordType || version != kEnvelopeVersion) {
                    if (!SkipRecordData(a_intfc, length)) {
                        ReportFailure(HF_ERROR_SERIALIZATION_READ_FAILED);
                        break;
                    }
                    ++skipped;
                    continue;
                }

                if (length < sizeof(PersistHeaderV1)) {
                    SkipRecordData(a_intfc, length);
                    ++skipped;
                    continue;
                }

                PersistHeaderV1 header{};
                const auto headerRead = a_intfc->ReadRecordData(&header, sizeof(header));
                if (headerRead != sizeof(header)) {
                    const auto remaining = length > headerRead ? length - headerRead : 0;
                    SkipRecordData(a_intfc, remaining);
                    ++skipped;
                    continue;
                }

                const auto remaining = length - static_cast<std::uint32_t>(sizeof(header));
                const std::uint64_t payloadBytes = static_cast<std::uint64_t>(header.ownerNameSize) + header.dataSize;
                const bool valid =
                    header.headerSize == sizeof(PersistHeaderV1) &&
                    header.envelopeVersion == kEnvelopeVersion &&
                    header.ownerNameSize != 0 &&
                    header.ownerNameSize <= kMaximumOwnerNameBytes &&
                    header.key != 0 &&
                    header.dataSize <= HF_SERIALIZATION_MAX_RECORD_SIZE &&
                    payloadBytes == remaining &&
                    accepted < kMaximumLoadedRecords &&
                    totalBytes + header.dataSize <= kMaximumLoadedBytes;

                if (!valid) {
                    SkipRecordData(a_intfc, remaining);
                    ++skipped;
                    continue;
                }

                std::string owner(header.ownerNameSize, '\0');
                if (a_intfc->ReadRecordData(owner.data(), header.ownerNameSize) != header.ownerNameSize) {
                    const auto dataRemaining = remaining > header.ownerNameSize ? remaining - header.ownerNameSize : 0;
                    SkipRecordData(a_intfc, dataRemaining);
                    ++skipped;
                    continue;
                }
                if (owner.find('\0') != std::string::npos) {
                    SkipRecordData(a_intfc, header.dataSize);
                    ++skipped;
                    continue;
                }

                std::vector<std::uint8_t> data(header.dataSize);
                if (header.dataSize != 0) {
                    const auto dataRead = a_intfc->ReadRecordData(data.data(), header.dataSize);
                    if (dataRead != header.dataSize) {
                        const auto unread = header.dataSize > dataRead ? header.dataSize - dataRead : 0;
                        SkipRecordData(a_intfc, unread);
                        ++skipped;
                        continue;
                    }
                }

                const auto ownerKey = NormalizeOwner(owner);
                auto& ownerRecords = loaded[ownerKey];
                if (ownerRecords.displayName.empty()) {
                    ownerRecords.displayName = owner;
                }
                const auto existing = ownerRecords.records.find(header.key);
                auto projected = OwnerDataSize(ownerRecords);
                if (existing != ownerRecords.records.end()) {
                    projected -= existing->second.data.size();
                }
                projected += data.size();
                if (projected > kMaximumOwnerBytes) {
                    ++skipped;
                    continue;
                }

                ownerRecords.records[header.key] = Record{
                    .version = header.recordVersion,
                    .data = std::move(data)
                };
                totalBytes += header.dataSize;
                ++accepted;
            }

            {
                std::scoped_lock lock{ _lock };
                _store = std::move(loaded);
                _generation.fetch_add(1, std::memory_order_release);
            }

            if (skipped != 0) {
                ReportFailure(
                    HF_ERROR_SERIALIZATION_RECORD_MALFORMED);
            }
        } catch (const std::exception&) {
            ReportFailure(HF_ERROR_SERIALIZATION_READ_FAILED);
        } catch (...) {
            ReportFailure(HF_ERROR_SERIALIZATION_READ_FAILED);
        }
    }

    void SerializationService::OnRevert() noexcept
    {
        try {
            std::scoped_lock lock{ _lock };
            _store.clear();
            _generation.fetch_add(1, std::memory_order_release);
        } catch (...) {
        }
    }
}
