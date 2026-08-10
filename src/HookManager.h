#pragma once

namespace HolyFramework
{
    class HookManager final
    {
    public:
        static HookManager& GetSingleton() noexcept;

        std::uint32_t GetTrampolineCapacity() const noexcept;
        std::uint32_t GetTrampolineFreeSize() const noexcept;

        HF_CodeBlockHandle AllocateCode(std::uint32_t a_size, const char* a_label, HF_Address& a_outAddress) noexcept;
        bool WriteCode(HF_CodeBlockHandle a_handle, std::uint32_t a_offset, const void* a_data, std::uint32_t a_size) noexcept;

        HF_HookHandle Install(
            HF_HookKind a_kind,
            HF_Address a_site,
            const void* a_expected,
            std::uint32_t a_expectedSize,
            HF_Address a_destination,
            const char* a_label,
            HF_Address& a_outOriginalTarget) noexcept;

        bool Restore(HF_HookHandle a_handle) noexcept;
        bool Verify(HF_HookHandle a_handle, HF_HookStatus& a_outStatus) noexcept;
        std::uint32_t GetCount() const noexcept;
        bool GetByIndex(std::uint32_t a_index, HF_HookRecordV1& a_outRecord) const noexcept;
        std::uint32_t Audit() noexcept;

        std::uint32_t RestoreOwnedBy(std::string_view a_moduleName) noexcept;
        [[nodiscard]] std::uint32_t GetUnrestoredCountOwnedBy(std::string_view a_moduleName) const noexcept;
        void PruneRestored() noexcept;

        [[nodiscard]] bool FindActiveOverlap(
            HF_Address a_address,
            std::uint32_t a_size,
            std::string& a_outOwner,
            std::string& a_outLabel) const noexcept;

    private:
        struct HookRecord
        {
            HF_HookHandle handle{ HF_INVALID_HOOK_HANDLE };
            HF_Address site{ 0 };
            HF_Address destination{ 0 };
            HF_Address originalTarget{ 0 };
            HF_HookKind kind{ HF_HOOK_CALL5 };
            HF_HookStatus status{ HF_HOOK_STATUS_UNKNOWN };
            std::uint32_t checkpoint{ 0 };
            std::string owner;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
            std::string label;
            std::vector<std::uint8_t> original;
            std::vector<std::uint8_t> installed;
            bool conflictReported{ false };
        };

        struct CodeBlockRecord
        {
            HF_CodeBlockHandle handle{ HF_INVALID_CODE_BLOCK_HANDLE };
            HF_Address address{ 0 };
            std::uint32_t size{ 0 };
            std::uint32_t checkpoint{ 0 };
            std::string owner;
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
            std::string label;
        };

        HookManager() = default;

        [[nodiscard]] static std::uint32_t HookSize(HF_HookKind a_kind) noexcept;
        [[nodiscard]] static std::uint32_t BranchThunkReserve(HF_HookKind a_kind) noexcept;
        [[nodiscard]] static bool OpcodeMatches(HF_HookKind a_kind, const std::vector<std::uint8_t>& a_bytes) noexcept;
        [[nodiscard]] static bool ReadSmall(HF_Address a_address, void* a_out, std::uint32_t a_size) noexcept;
        [[nodiscard]] static bool IsExecutableAddress(HF_Address a_address) noexcept;
        [[nodiscard]] static bool RangesOverlap(HF_Address a_left, std::uint32_t a_leftSize, HF_Address a_right, std::uint32_t a_rightSize) noexcept;
        [[nodiscard]] static std::string SafeLabel(const char* a_label);
        static void FillRecord(const HookRecord& a_hook, HF_HookRecordV1& a_out) noexcept;
        static void ReportHookFailure(std::string_view a_owner, HF_LogHandle a_logger, HF_ErrorCode a_code) noexcept;

        mutable std::mutex _lock;
        std::vector<HookRecord> _hooks;
        std::vector<CodeBlockRecord> _codeBlocks;
        std::atomic<HF_HookHandle> _nextHook{ 1 };
        std::atomic<HF_CodeBlockHandle> _nextCodeBlock{ 1 };
    };
}
