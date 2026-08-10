#include "pch.h"

#include <Windows.h>
#ifdef ERROR
#  undef ERROR
#endif

#include "HookManager.h"

#include "Diagnostics.h"
#include "MemoryManager.h"
#include "ModuleContext.h"
#include "MutationLock.h"

namespace HolyFramework
{
    namespace
    {
        constexpr std::uint32_t kMaximumCodeBlockSize = 16 * 1024;

        [[nodiscard]] bool IsReadableProtection(const DWORD a_protect) noexcept
        {
            if ((a_protect & PAGE_GUARD) != 0 || (a_protect & PAGE_NOACCESS) != 0) {
                return false;
            }
            switch (a_protect & 0xFFu) {
            case PAGE_READONLY:
            case PAGE_READWRITE:
            case PAGE_WRITECOPY:
            case PAGE_EXECUTE_READ:
            case PAGE_EXECUTE_READWRITE:
            case PAGE_EXECUTE_WRITECOPY:
                return true;
            default:
                return false;
            }
        }
    }

    HookManager& HookManager::GetSingleton() noexcept
    {
        static HookManager* instance = new HookManager();
        return *instance;
    }

    std::uint32_t HookManager::GetTrampolineCapacity() const noexcept
    {
        const auto capacity = REL::GetTrampoline().capacity();
        return static_cast<std::uint32_t>((std::min)(capacity, static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())));
    }

    std::uint32_t HookManager::GetTrampolineFreeSize() const noexcept
    {
        const auto free = REL::GetTrampoline().free_size();
        return static_cast<std::uint32_t>((std::min)(free, static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())));
    }

    std::uint32_t HookManager::HookSize(const HF_HookKind a_kind) noexcept
    {
        switch (a_kind) {
        case HF_HOOK_CALL5:
        case HF_HOOK_JMP5:
            return 5;
        case HF_HOOK_CALL6:
        case HF_HOOK_JMP6:
            return 6;
        default:
            return 0;
        }
    }

    std::uint32_t HookManager::BranchThunkReserve(const HF_HookKind a_kind) noexcept
    {
        switch (a_kind) {
        case HF_HOOK_CALL5:
        case HF_HOOK_JMP5:
            return 14;
        case HF_HOOK_CALL6:
        case HF_HOOK_JMP6:
            return static_cast<std::uint32_t>(sizeof(std::uintptr_t));
        default:
            return 0;
        }
    }

    bool HookManager::OpcodeMatches(const HF_HookKind a_kind, const std::vector<std::uint8_t>& a_bytes) noexcept
    {
        if (a_kind == HF_HOOK_CALL5) {
            return a_bytes.size() == 5 && a_bytes[0] == 0xE8;
        }
        if (a_kind == HF_HOOK_JMP5) {
            return a_bytes.size() == 5 && a_bytes[0] == 0xE9;
        }
        if (a_kind == HF_HOOK_CALL6) {
            return a_bytes.size() == 6 && a_bytes[0] == 0xFF && a_bytes[1] == 0x15;
        }
        if (a_kind == HF_HOOK_JMP6) {
            return a_bytes.size() == 6 && a_bytes[0] == 0xFF && a_bytes[1] == 0x25;
        }
        return false;
    }

    bool HookManager::ReadSmall(const HF_Address a_address, void* const a_out, const std::uint32_t a_size) noexcept
    {
        if (!a_address || !a_out || a_size == 0) {
            return false;
        }

        MEMORY_BASIC_INFORMATION mbi{};
        if (::VirtualQuery(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(a_address)), &mbi, sizeof(mbi)) != sizeof(mbi) ||
            mbi.State != MEM_COMMIT || !IsReadableProtection(mbi.Protect)) {
            return false;
        }

        const auto start = static_cast<std::uintptr_t>(a_address);
        const auto base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const auto end = base + mbi.RegionSize;
        if (start < base || start > end || a_size > end - start) {
            return false;
        }

        std::memcpy(a_out, reinterpret_cast<const void*>(start), a_size);
        return true;
    }

    bool HookManager::IsExecutableAddress(const HF_Address a_address) noexcept
    {
        if (!a_address) {
            return false;
        }
        MEMORY_BASIC_INFORMATION mbi{};
        if (::VirtualQuery(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(a_address)), &mbi, sizeof(mbi)) != sizeof(mbi) ||
            mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) != 0 || (mbi.Protect & PAGE_NOACCESS) != 0) {
            return false;
        }
        switch (mbi.Protect & 0xFFu) {
        case PAGE_EXECUTE:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return true;
        default:
            return false;
        }
    }

    bool HookManager::RangesOverlap(
        const HF_Address a_left,
        const std::uint32_t a_leftSize,
        const HF_Address a_right,
        const std::uint32_t a_rightSize) noexcept
    {
        if (!a_leftSize || !a_rightSize) {
            return false;
        }
        const auto leftEnd = a_left + a_leftSize;
        const auto rightEnd = a_right + a_rightSize;
        return a_left < rightEnd && a_right < leftEnd;
    }

    std::string HookManager::SafeLabel(const char* const a_label)
    {
        std::string value = a_label && *a_label ? a_label : "unnamed";
        if (value.size() > 95) {
            value.resize(95);
        }
        return value;
    }

    void HookManager::ReportHookFailure(
        const std::string_view a_owner,
        const HF_LogHandle a_logger,
        const HF_ErrorCode a_code) noexcept
    {
        Diagnostics::ReportFrameworkFailureForModule(
            a_owner.empty() ? std::string_view{ "<unknown>" } : a_owner,
            a_logger,
            a_code);
    }

    HF_CodeBlockHandle HookManager::AllocateCode(
        const std::uint32_t a_size,
        const char* const a_label,
        HF_Address& a_outAddress) noexcept
    {
        a_outAddress = 0;
        const auto context = ModuleContext::Current();
        if (!context.name || !*context.name) {
            ReportHookFailure("<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_HOOK_OWNER_UNKNOWN);
            return HF_INVALID_CODE_BLOCK_HANDLE;
        }
        if (a_size == 0 || a_size > kMaximumCodeBlockSize) {
            ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_CODE_BLOCK_INVALID);
            return HF_INVALID_CODE_BLOCK_HANDLE;
        }

        std::scoped_lock mutation{ MutationMutex() };
        auto& trampoline = REL::GetTrampoline();
        if (trampoline.free_size() < a_size) {
            ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_TRAMPOLINE_EXHAUSTED);
            return HF_INVALID_CODE_BLOCK_HANDLE;
        }

        try {
            const auto address = reinterpret_cast<std::uintptr_t>(trampoline.allocate(a_size));
            if (!address) {
                ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_CODE_BLOCK_INVALID);
                return HF_INVALID_CODE_BLOCK_HANDLE;
            }

            auto handle = _nextCodeBlock.fetch_add(1, std::memory_order_relaxed);
            if (handle == HF_INVALID_CODE_BLOCK_HANDLE) {
                handle = _nextCodeBlock.fetch_add(1, std::memory_order_relaxed);
            }

            CodeBlockRecord record{};
            record.handle = handle;
            record.address = static_cast<HF_Address>(address);
            record.size = a_size;
            record.checkpoint = context.checkpoint;
            record.owner = context.name;
            record.logger = context.logger;
            record.label = SafeLabel(a_label);
            {
                std::scoped_lock lock{ _lock };
                _codeBlocks.emplace_back(std::move(record));
            }
            a_outAddress = static_cast<HF_Address>(address);
            return handle;
        } catch (const std::exception&) {
            ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_CODE_BLOCK_INVALID);
        } catch (...) {
            ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_CODE_BLOCK_INVALID);
        }
        return HF_INVALID_CODE_BLOCK_HANDLE;
    }

    bool HookManager::WriteCode(
        const HF_CodeBlockHandle a_handle,
        const std::uint32_t a_offset,
        const void* const a_data,
        const std::uint32_t a_size) noexcept
    {
        const auto context = ModuleContext::Current();
        if (!context.name || !*context.name) {
            ReportHookFailure("<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_HOOK_OWNER_UNKNOWN);
            return false;
        }
        if (a_handle == HF_INVALID_CODE_BLOCK_HANDLE || !a_data || a_size == 0) {
            ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_CODE_BLOCK_INVALID);
            return false;
        }

        HF_Address address = 0;
        {
            std::scoped_lock lock{ _lock };
            const auto it = std::ranges::find_if(_codeBlocks, [a_handle](const CodeBlockRecord& block) { return block.handle == a_handle; });
            if (it == _codeBlocks.end() || it->owner != context.name || a_offset > it->size || a_size > it->size - a_offset) {
                ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_CODE_BLOCK_INVALID);
                return false;
            }
            address = it->address + a_offset;
        }

        std::scoped_lock mutation{ MutationMutex() };
        if (!REL::WriteSafe(static_cast<std::uintptr_t>(address), a_data, a_size)) {
            ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_CODE_WRITE_FAILED);
            return false;
        }

        std::vector<std::uint8_t> verify(a_size);
        if (!ReadSmall(address, verify.data(), a_size) || std::memcmp(verify.data(), a_data, a_size) != 0) {
            ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_CODE_WRITE_FAILED);
            return false;
        }
        return true;
    }

    HF_HookHandle HookManager::Install(
        const HF_HookKind a_kind,
        const HF_Address a_site,
        const void* const a_expected,
        const std::uint32_t a_expectedSize,
        const HF_Address a_destination,
        const char* const a_label,
        HF_Address& a_outOriginalTarget) noexcept
    {
        a_outOriginalTarget = 0;
        const auto context = ModuleContext::Current();
        if (!context.name || !*context.name) {
            ReportHookFailure("<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_HOOK_OWNER_UNKNOWN);
            return HF_INVALID_HOOK_HANDLE;
        }

        const auto hookSize = HookSize(a_kind);
        if (!hookSize || !a_site || !a_destination || !a_expected || a_expectedSize != hookSize) {
            ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_INVALID_REQUEST);
            return HF_INVALID_HOOK_HANDLE;
        }

        if (!IsExecutableAddress(a_destination)) {
            ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_DESTINATION_INVALID);
            return HF_INVALID_HOOK_HANDLE;
        }

        std::vector<std::uint8_t> expected(hookSize);
        std::memcpy(expected.data(), a_expected, hookSize);
        if (!OpcodeMatches(a_kind, expected)) {
            ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_INVALID_REQUEST);
            return HF_INVALID_HOOK_HANDLE;
        }

        std::scoped_lock mutation{ MutationMutex() };

        std::vector<std::uint8_t> current(hookSize);
        if (!ReadSmall(a_site, current.data(), hookSize) || current != expected) {
            ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_EXPECTED_MISMATCH);
            return HF_INVALID_HOOK_HANDLE;
        }

        std::string overlapOwner;
        std::string overlapLabel;
        if (MemoryManager::GetSingleton().FindActiveOverlap(a_site, hookSize, overlapOwner, overlapLabel)) {
            ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_OVERLAP);
            return HF_INVALID_HOOK_HANDLE;
        }
        if (FindActiveOverlap(a_site, hookSize, overlapOwner, overlapLabel)) {
            ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_OVERLAP);
            return HF_INVALID_HOOK_HANDLE;
        }

        auto& trampoline = REL::GetTrampoline();
        const auto reserve = BranchThunkReserve(a_kind);
        if (trampoline.free_size() < reserve) {
            ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_TRAMPOLINE_EXHAUSTED);
            return HF_INVALID_HOOK_HANDLE;
        }

        HF_Address originalTarget = 0;
        try {
            const auto site = static_cast<std::uintptr_t>(a_site);
            const auto destination = static_cast<std::uintptr_t>(a_destination);
            switch (a_kind) {
            case HF_HOOK_CALL5:
                originalTarget = static_cast<HF_Address>(REL::ASM::CALL5::TARGET(site));
                trampoline.write_call<5>(site, destination);
                break;
            case HF_HOOK_JMP5:
                originalTarget = static_cast<HF_Address>(REL::ASM::JMP5::TARGET(site));
                trampoline.write_jmp<5>(site, destination);
                break;
            case HF_HOOK_CALL6: {
                const auto slot = REL::ASM::CALL6::TARGET(site);
                std::uintptr_t target{};
                if (!ReadSmall(static_cast<HF_Address>(slot), &target, sizeof(target))) {
                    ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_INSTALL_FAILED);
                    return HF_INVALID_HOOK_HANDLE;
                }
                originalTarget = static_cast<HF_Address>(target);
                trampoline.write_call<6>(site, destination);
                break;
            }
            case HF_HOOK_JMP6: {
                const auto slot = REL::ASM::JMP6::TARGET(site);
                std::uintptr_t target{};
                if (!ReadSmall(static_cast<HF_Address>(slot), &target, sizeof(target))) {
                    ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_INSTALL_FAILED);
                    return HF_INVALID_HOOK_HANDLE;
                }
                originalTarget = static_cast<HF_Address>(target);
                trampoline.write_jmp<6>(site, destination);
                break;
            }
            default:
                return HF_INVALID_HOOK_HANDLE;
            }
        } catch (const std::exception&) {
            ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_INSTALL_FAILED);
            return HF_INVALID_HOOK_HANDLE;
        } catch (...) {
            ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_INSTALL_FAILED);
            return HF_INVALID_HOOK_HANDLE;
        }

        std::vector<std::uint8_t> installed(hookSize);
        if (!ReadSmall(a_site, installed.data(), hookSize) || installed == expected) {
            // Best effort rollback to exactly what the module expected before the hook.
            REL::WriteSafe(static_cast<std::uintptr_t>(a_site), expected.data(), expected.size());
            ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_VERIFY_FAILED);
            return HF_INVALID_HOOK_HANDLE;
        }

        auto handle = _nextHook.fetch_add(1, std::memory_order_relaxed);
        if (handle == HF_INVALID_HOOK_HANDLE) {
            handle = _nextHook.fetch_add(1, std::memory_order_relaxed);
        }

        HookRecord record{};
        record.handle = handle;
        record.site = a_site;
        record.destination = a_destination;
        record.originalTarget = originalTarget;
        record.kind = a_kind;
        record.status = HF_HOOK_STATUS_APPLIED;
        record.checkpoint = context.checkpoint;
        record.owner = context.name;
        record.logger = context.logger;
        record.label = SafeLabel(a_label);
        record.original = std::move(expected);
        record.installed = std::move(installed);
        {
            std::scoped_lock lock{ _lock };
            _hooks.emplace_back(std::move(record));
        }
        a_outOriginalTarget = originalTarget;
        return handle;
    }

    bool HookManager::Restore(const HF_HookHandle a_handle) noexcept
    {
        const auto context = ModuleContext::Current();
        if (!context.name || !*context.name) {
            ReportHookFailure("<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_HOOK_OWNER_UNKNOWN);
            return false;
        }

        std::scoped_lock mutation{ MutationMutex() };
        std::scoped_lock lock{ _lock };
        const auto it = std::ranges::find_if(_hooks, [a_handle](const HookRecord& hook) { return hook.handle == a_handle; });
        if (it == _hooks.end()) {
            return false;
        }
        if (it->owner != context.name) {
            ReportHookFailure(context.name, context.logger, HF_ERROR_HOOK_OWNER_MISMATCH);
            return false;
        }
        if (it->status == HF_HOOK_STATUS_RESTORED) {
            return true;
        }

        std::vector<std::uint8_t> current(it->installed.size());
        if (!ReadSmall(it->site, current.data(), static_cast<std::uint32_t>(current.size())) || current != it->installed) {
            it->status = HF_HOOK_STATUS_MODIFIED;
            ReportHookFailure(it->owner, it->logger, HF_ERROR_HOOK_RESTORE_CONFLICT);
            return false;
        }

        if (!REL::WriteSafe(static_cast<std::uintptr_t>(it->site), it->original.data(), it->original.size())) {
            ReportHookFailure(it->owner, it->logger, HF_ERROR_HOOK_INSTALL_FAILED);
            return false;
        }
        std::vector<std::uint8_t> verify(it->original.size());
        if (!ReadSmall(it->site, verify.data(), static_cast<std::uint32_t>(verify.size())) || verify != it->original) {
            ReportHookFailure(it->owner, it->logger, HF_ERROR_HOOK_VERIFY_FAILED);
            return false;
        }
        it->status = HF_HOOK_STATUS_RESTORED;
        return true;
    }

    bool HookManager::Verify(const HF_HookHandle a_handle, HF_HookStatus& a_outStatus) noexcept
    {
        a_outStatus = HF_HOOK_STATUS_UNKNOWN;
        std::string owner;
        HF_LogHandle logger = HF_INVALID_LOG_HANDLE;
        bool report = false;
        {
            std::scoped_lock lock{ _lock };
            const auto it = std::ranges::find_if(_hooks, [a_handle](const HookRecord& hook) { return hook.handle == a_handle; });
            if (it == _hooks.end()) {
                return false;
            }
            if (it->status == HF_HOOK_STATUS_RESTORED) {
                a_outStatus = it->status;
                return true;
            }
            std::vector<std::uint8_t> current(it->installed.size());
            if (!ReadSmall(it->site, current.data(), static_cast<std::uint32_t>(current.size()))) {
                a_outStatus = it->status;
                return false;
            }
            if (current == it->installed) {
                it->status = HF_HOOK_STATUS_APPLIED;
                a_outStatus = it->status;
                return true;
            }
            it->status = HF_HOOK_STATUS_MODIFIED;
            a_outStatus = it->status;
            if (!it->conflictReported) {
                it->conflictReported = true;
                owner = it->owner;
                logger = it->logger;
                report = true;
            }
        }
        if (report) {
            ReportHookFailure(owner, logger, HF_ERROR_HOOK_MODIFIED);
        }
        return true;
    }

    std::uint32_t HookManager::GetCount() const noexcept
    {
        std::scoped_lock lock{ _lock };
        return static_cast<std::uint32_t>(std::ranges::count_if(_hooks, [](const HookRecord& hook) {
            return hook.status == HF_HOOK_STATUS_APPLIED || hook.status == HF_HOOK_STATUS_MODIFIED;
        }));
    }

    void HookManager::FillRecord(const HookRecord& a_hook, HF_HookRecordV1& a_out) noexcept
    {
        a_out = {};
        a_out.structSize = sizeof(HF_HookRecordV1);
        a_out.handle = a_hook.handle;
        a_out.site = a_hook.site;
        a_out.destination = a_hook.destination;
        a_out.originalTarget = a_hook.originalTarget;
        a_out.kind = a_hook.kind;
        a_out.status = a_hook.status;
        a_out.checkpoint = a_hook.checkpoint;
        std::snprintf(a_out.owner, sizeof(a_out.owner), "%s", a_hook.owner.c_str());
        std::snprintf(a_out.label, sizeof(a_out.label), "%s", a_hook.label.c_str());
    }

    bool HookManager::GetByIndex(const std::uint32_t a_index, HF_HookRecordV1& a_outRecord) const noexcept
    {
        std::scoped_lock lock{ _lock };
        std::uint32_t currentIndex = 0;
        for (const auto& hook : _hooks) {
            if (hook.status != HF_HOOK_STATUS_APPLIED && hook.status != HF_HOOK_STATUS_MODIFIED) {
                continue;
            }
            if (currentIndex++ == a_index) {
                FillRecord(hook, a_outRecord);
                return true;
            }
        }
        return false;
    }

    std::uint32_t HookManager::Audit() noexcept
    {
        std::vector<HF_HookHandle> active;
        {
            std::scoped_lock lock{ _lock };
            for (const auto& hook : _hooks) {
                if (hook.status == HF_HOOK_STATUS_APPLIED || hook.status == HF_HOOK_STATUS_MODIFIED) {
                    active.push_back(hook.handle);
                }
            }
        }
        std::uint32_t modified = 0;
        for (const auto handle : active) {
            HF_HookStatus status{};
            if (Verify(handle, status) && status == HF_HOOK_STATUS_MODIFIED) {
                ++modified;
            }
        }
        return modified;
    }

    std::uint32_t HookManager::RestoreOwnedBy(const std::string_view a_moduleName) noexcept
    {
        if (a_moduleName.empty()) {
            return 0;
        }
        std::vector<HF_HookHandle> handles;
        HF_LogHandle logger = HF_INVALID_LOG_HANDLE;
        {
            std::scoped_lock lock{ _lock };
            for (const auto& hook : _hooks) {
                if (hook.owner == a_moduleName && hook.status != HF_HOOK_STATUS_RESTORED) {
                    handles.push_back(hook.handle);
                    if (logger == HF_INVALID_LOG_HANDLE) {
                        logger = hook.logger;
                    }
                }
            }
        }
        std::uint32_t restored = 0;
        ModuleContext::Scope scope{ a_moduleName.data(), logger };
        for (const auto handle : handles) {
            if (Restore(handle)) {
                ++restored;
            }
        }
        return restored;
    }

    std::uint32_t HookManager::GetUnrestoredCountOwnedBy(const std::string_view a_moduleName) const noexcept
    {
        if (a_moduleName.empty()) {
            return 0;
        }
        std::scoped_lock lock{ _lock };
        return static_cast<std::uint32_t>(std::ranges::count_if(_hooks, [a_moduleName](const HookRecord& hook) {
            return hook.owner == a_moduleName && hook.status != HF_HOOK_STATUS_RESTORED;
        }));
    }

    void HookManager::PruneRestored() noexcept
    {
        std::scoped_lock lock{ _lock };
        std::erase_if(_hooks, [](const HookRecord& hook) { return hook.status == HF_HOOK_STATUS_RESTORED; });
    }

    bool HookManager::FindActiveOverlap(
        const HF_Address a_address,
        const std::uint32_t a_size,
        std::string& a_outOwner,
        std::string& a_outLabel) const noexcept
    {
        a_outOwner.clear();
        a_outLabel.clear();
        std::scoped_lock lock{ _lock };
        for (const auto& hook : _hooks) {
            if (hook.status != HF_HOOK_STATUS_APPLIED && hook.status != HF_HOOK_STATUS_MODIFIED) {
                continue;
            }
            if (RangesOverlap(a_address, a_size, hook.site, HookSize(hook.kind))) {
                a_outOwner = hook.owner;
                a_outLabel = hook.label;
                return true;
            }
        }
        return false;
    }
}
