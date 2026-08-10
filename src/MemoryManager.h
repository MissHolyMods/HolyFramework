#pragma once

namespace HolyFramework
{
    class MemoryManager final
    {
    public:
        static MemoryManager& GetSingleton() noexcept;

        void StartIntegrityMonitor() noexcept;

        bool ResolveID(std::uint64_t a_id, std::int64_t a_offset, HF_Address& a_outAddress) noexcept;
        bool QueryRegion(HF_Address a_address, HF_MemoryRegionV1& a_outRegion) noexcept;
        bool Read(HF_Address a_address, void* a_outData, std::uint32_t a_size) noexcept;
        bool Compare(HF_Address a_address, const void* a_expected, std::uint32_t a_size) noexcept;

        HF_PatchHandle ApplyPatch(
            HF_Address a_address,
            const void* a_expected,
            const void* a_replacement,
            std::uint32_t a_size,
            const char* a_label) noexcept;

        bool RestorePatch(HF_PatchHandle a_handle) noexcept;
        bool VerifyPatch(HF_PatchHandle a_handle, HF_PatchStatus& a_outStatus) noexcept;
        std::uint32_t GetPatchCount() const noexcept;
        bool GetPatchByIndex(std::uint32_t a_index, HF_PatchRecordV1& a_outRecord) const noexcept;
        std::uint32_t AuditPatches() noexcept;
        std::uint32_t RestoreOwnedBy(std::string_view a_moduleName) noexcept;
        [[nodiscard]] std::uint32_t GetUnrestoredCountOwnedBy(std::string_view a_moduleName) const noexcept;
        void PruneRestored() noexcept;
        [[nodiscard]] bool FindActiveOverlap(
            HF_Address a_address,
            std::uint32_t a_size,
            std::string& a_outOwner,
            std::string& a_outLabel) const noexcept;

    private:
        struct PatchRecord
        {
            HF_PatchHandle handle{ HF_INVALID_PATCH_HANDLE };
            HF_Address address{ 0 };
            std::uint32_t size{ 0 };
            HF_PatchStatus status{ HF_PATCH_STATUS_UNKNOWN };
            std::uint32_t checkpoint{ 0 };
            std::string owner;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
            std::string label;
            std::vector<std::uint8_t> original;
            std::vector<std::uint8_t> replacement;
            bool conflictReported{ false };
        };

        MemoryManager() = default;

        [[nodiscard]] static bool IsReadableProtection(std::uint32_t a_protect) noexcept;
        [[nodiscard]] static std::uint32_t ToAccessFlags(std::uint32_t a_protect) noexcept;
        [[nodiscard]] static bool ValidateReadableRange(HF_Address a_address, std::uint32_t a_size) noexcept;
        [[nodiscard]] static bool ReadBytes(HF_Address a_address, void* a_outData, std::uint32_t a_size) noexcept;
        [[nodiscard]] static bool RangesOverlap(HF_Address a_leftAddress, std::uint32_t a_leftSize, HF_Address a_rightAddress, std::uint32_t a_rightSize) noexcept;
        static void FillPatchRecord(const PatchRecord& a_patch, HF_PatchRecordV1& a_out) noexcept;
        static void ReportMemoryFailure(std::string_view a_moduleName, HF_LogHandle a_logger, HF_ErrorCode a_code) noexcept;

        mutable std::mutex _lock;
        std::vector<PatchRecord> _patches;
        std::atomic<HF_PatchHandle> _nextPatchHandle{ 1 };
        std::atomic_bool _monitorStarted{ false };
    };
}
