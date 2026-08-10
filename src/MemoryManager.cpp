#include "pch.h"

#include <Windows.h>
#ifdef ERROR
#  undef ERROR
#endif

#include "MemoryManager.h"

#include "Diagnostics.h"
#include "HookManager.h"
#include "MutationLock.h"
#include "ModuleContext.h"
#include "RenderPipelineService.h"
#include "PresentationService.h"

namespace HolyFramework
{
    namespace
    {
        constexpr std::uint32_t kMaximumPatchSize = 4096;
        constexpr auto kAuditInterval = std::chrono::seconds{ 5 };

        [[nodiscard]] std::string SafeLabel(const char* const a_label)
        {
            if (!a_label || !*a_label) {
                return "unnamed";
            }
            std::string result{ a_label };
            if (result.size() > 95) {
                result.resize(95);
            }
            return result;
        }
    }

    MemoryManager& MemoryManager::GetSingleton() noexcept
    {
        // Process-lifetime object. Its integrity worker intentionally survives for
        // the lifetime of Fallout 4 and is not joined during DLL teardown.
        static MemoryManager* instance = new MemoryManager();
        return *instance;
    }

    void MemoryManager::StartIntegrityMonitor() noexcept
    {
        bool expected = false;
        if (!_monitorStarted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return;
        }

        std::thread([this]() {
            for (;;) {
                std::this_thread::sleep_for(kAuditInterval);
                AuditPatches();
                HookManager::GetSingleton().Audit();
                (void)RenderPipelineService::GetSingleton().AuditHook();
                (void)PresentationService::GetSingleton().AuditHooks();
            }
        }).detach();
    }

    bool MemoryManager::IsReadableProtection(const std::uint32_t a_protect) noexcept
    {
        if ((a_protect & PAGE_GUARD) != 0 || (a_protect & PAGE_NOACCESS) != 0) {
            return false;
        }

        const auto base = a_protect & 0xFFu;
        switch (base) {
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

    std::uint32_t MemoryManager::ToAccessFlags(const std::uint32_t a_protect) noexcept
    {
        std::uint32_t flags = HF_MEMORY_ACCESS_NONE;
        if ((a_protect & PAGE_GUARD) != 0) {
            flags |= HF_MEMORY_ACCESS_GUARD;
        }
        if (IsReadableProtection(a_protect)) {
            flags |= HF_MEMORY_ACCESS_READ;
        }

        const auto base = a_protect & 0xFFu;
        if (base == PAGE_READWRITE || base == PAGE_WRITECOPY ||
            base == PAGE_EXECUTE_READWRITE || base == PAGE_EXECUTE_WRITECOPY) {
            flags |= HF_MEMORY_ACCESS_WRITE;
        }
        if (base == PAGE_EXECUTE || base == PAGE_EXECUTE_READ ||
            base == PAGE_EXECUTE_READWRITE || base == PAGE_EXECUTE_WRITECOPY) {
            flags |= HF_MEMORY_ACCESS_EXECUTE;
        }
        return flags;
    }

    bool MemoryManager::ValidateReadableRange(const HF_Address a_address, const std::uint32_t a_size) noexcept
    {
        if (a_address == 0 || a_size == 0) {
            return false;
        }

        const auto start = static_cast<std::uintptr_t>(a_address);
        if (start > (std::numeric_limits<std::uintptr_t>::max)() - a_size) {
            return false;
        }
        const auto end = start + a_size;
        auto cursor = start;

        while (cursor < end) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (::VirtualQuery(reinterpret_cast<const void*>(cursor), &mbi, sizeof(mbi)) != sizeof(mbi)) {
                return false;
            }
            if (mbi.State != MEM_COMMIT || !IsReadableProtection(mbi.Protect)) {
                return false;
            }

            const auto regionBase = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
            const auto regionEnd = regionBase + mbi.RegionSize;
            if (regionEnd <= cursor) {
                return false;
            }
            cursor = (std::min)(end, regionEnd);
        }

        return true;
    }

    bool MemoryManager::ReadBytes(
        const HF_Address a_address,
        void* const a_outData,
        const std::uint32_t a_size) noexcept
    {
        if (!a_outData || !ValidateReadableRange(a_address, a_size)) {
            return false;
        }

        SIZE_T bytesRead = 0;
        const auto success = ::ReadProcessMemory(
            ::GetCurrentProcess(),
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(a_address)),
            a_outData,
            a_size,
            &bytesRead);
        return success != FALSE && bytesRead == a_size;
    }

    void MemoryManager::ReportMemoryFailure(
        const std::string_view a_moduleName,
        const HF_LogHandle a_logger,
        const HF_ErrorCode a_code) noexcept
    {
        Diagnostics::ReportFrameworkFailureForModule(
            a_moduleName.empty() ? std::string_view{ "<unknown>" } : a_moduleName,
            a_logger,
            a_code);
    }

    bool MemoryManager::ResolveID(
        const std::uint64_t a_id,
        const std::int64_t a_offset,
        HF_Address& a_outAddress) noexcept
    {
        a_outAddress = 0;
        const auto context = ModuleContext::Current();
        const std::string_view owner = context.name ? std::string_view{ context.name } : std::string_view{ "<unknown>" };

        try {
            const auto base = REL::ID{ a_id }.address();
            if (base == 0) {
                ReportMemoryFailure(owner, context.logger, HF_ERROR_MEMORY_RELOCATION_FAILED);
                return false;
            }

            const auto signedBase = static_cast<std::int64_t>(base);
            if ((a_offset > 0 && signedBase > (std::numeric_limits<std::int64_t>::max)() - a_offset) ||
                (a_offset < 0 && signedBase < (std::numeric_limits<std::int64_t>::min)() - a_offset)) {
                ReportMemoryFailure(owner, context.logger, HF_ERROR_MEMORY_RELOCATION_FAILED);
                return false;
            }

            const auto address = static_cast<std::uintptr_t>(signedBase + a_offset);
            if (address == 0) {
                ReportMemoryFailure(owner, context.logger, HF_ERROR_MEMORY_RELOCATION_FAILED);
                return false;
            }

            a_outAddress = static_cast<HF_Address>(address);
            return true;
        } catch (const std::exception&) {
            ReportMemoryFailure(owner, context.logger, HF_ERROR_MEMORY_RELOCATION_FAILED);
        } catch (...) {
            ReportMemoryFailure(owner, context.logger, HF_ERROR_MEMORY_RELOCATION_FAILED);
        }
        return false;
    }

    bool MemoryManager::QueryRegion(const HF_Address a_address, HF_MemoryRegionV1& a_outRegion) noexcept
    {
        a_outRegion = {};
        a_outRegion.structSize = sizeof(HF_MemoryRegionV1);
        if (a_address == 0) {
            return false;
        }

        MEMORY_BASIC_INFORMATION mbi{};
        if (::VirtualQuery(
                reinterpret_cast<const void*>(static_cast<std::uintptr_t>(a_address)),
                &mbi,
                sizeof(mbi)) != sizeof(mbi)) {
            return false;
        }

        a_outRegion.baseAddress = static_cast<HF_Address>(reinterpret_cast<std::uintptr_t>(mbi.BaseAddress));
        a_outRegion.regionSize = static_cast<std::uint64_t>(mbi.RegionSize);
        a_outRegion.accessFlags = ToAccessFlags(mbi.Protect);
        a_outRegion.committed = mbi.State == MEM_COMMIT ? HF_TRUE : HF_FALSE;
        return true;
    }

    bool MemoryManager::Read(
        const HF_Address a_address,
        void* const a_outData,
        const std::uint32_t a_size) noexcept
    {
        if (a_size == 0 || !a_outData) {
            return false;
        }

        if (!ReadBytes(a_address, a_outData, a_size)) {
            const auto context = ModuleContext::Current();
            ReportMemoryFailure(
                context.name ? std::string_view{ context.name } : std::string_view{ "<unknown>" },
                context.logger,
                HF_ERROR_MEMORY_READ_FAILED);
            return false;
        }
        return true;
    }

    bool MemoryManager::Compare(
        const HF_Address a_address,
        const void* const a_expected,
        const std::uint32_t a_size) noexcept
    {
        if (!a_expected || a_size == 0 || a_size > kMaximumPatchSize) {
            return false;
        }
        std::vector<std::uint8_t> current(a_size);
        if (!ReadBytes(a_address, current.data(), a_size)) {
            return false;
        }
        return std::memcmp(current.data(), a_expected, a_size) == 0;
    }

    bool MemoryManager::RangesOverlap(
        const HF_Address a_leftAddress,
        const std::uint32_t a_leftSize,
        const HF_Address a_rightAddress,
        const std::uint32_t a_rightSize) noexcept
    {
        const auto leftEnd = a_leftAddress + a_leftSize;
        const auto rightEnd = a_rightAddress + a_rightSize;
        return a_leftAddress < rightEnd && a_rightAddress < leftEnd;
    }

    HF_PatchHandle MemoryManager::ApplyPatch(
        const HF_Address a_address,
        const void* const a_expected,
        const void* const a_replacement,
        const std::uint32_t a_size,
        const char* const a_label) noexcept
    {
        const auto context = ModuleContext::Current();
        if (!context.name || !*context.name) {
            ReportMemoryFailure("<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_MEMORY_OWNER_UNKNOWN);
            return HF_INVALID_PATCH_HANDLE;
        }

        const std::string owner{ context.name };
        if (!a_expected || !a_replacement || a_address == 0 || a_size == 0 || a_size > kMaximumPatchSize) {
            ReportMemoryFailure(owner, context.logger, HF_ERROR_MEMORY_INVALID_REQUEST);
            return HF_INVALID_PATCH_HANDLE;
        }

        std::scoped_lock mutation{ MutationMutex() };

        std::string hookOwner;
        std::string hookLabel;
        if (HookManager::GetSingleton().FindActiveOverlap(a_address, a_size, hookOwner, hookLabel)) {
            ReportMemoryFailure(owner, context.logger, HF_ERROR_MEMORY_PATCH_OVERLAP);
            return HF_INVALID_PATCH_HANDLE;
        }

        std::vector<std::uint8_t> expected(a_size);
        std::vector<std::uint8_t> replacement(a_size);
        std::memcpy(expected.data(), a_expected, a_size);
        std::memcpy(replacement.data(), a_replacement, a_size);

        if (!ValidateReadableRange(a_address, a_size)) {
            ReportMemoryFailure(owner, context.logger, HF_ERROR_MEMORY_REGION_INVALID);
            return HF_INVALID_PATCH_HANDLE;
        }

        std::vector<std::uint8_t> current(a_size);
        if (!ReadBytes(a_address, current.data(), a_size)) {
            ReportMemoryFailure(owner, context.logger, HF_ERROR_MEMORY_READ_FAILED);
            return HF_INVALID_PATCH_HANDLE;
        }

        if (current != expected) {
            ReportMemoryFailure(owner, context.logger, HF_ERROR_MEMORY_EXPECTED_MISMATCH);
            return HF_INVALID_PATCH_HANDLE;
        }

        std::scoped_lock lock{ _lock };
        for (const auto& patch : _patches) {
            if (patch.status != HF_PATCH_STATUS_APPLIED && patch.status != HF_PATCH_STATUS_MODIFIED) {
                continue;
            }
            if (RangesOverlap(a_address, a_size, patch.address, patch.size)) {
                ReportMemoryFailure(owner, context.logger, HF_ERROR_MEMORY_PATCH_OVERLAP);
                return HF_INVALID_PATCH_HANDLE;
            }
        }

        // Re-read after acquiring the registry lock. This closes the window for
        // another HolyFramework patch and also catches an external write that
        // happened between initial validation and the actual patch operation.
        if (!ReadBytes(a_address, current.data(), a_size) || current != expected) {
            ReportMemoryFailure(owner, context.logger, HF_ERROR_MEMORY_EXPECTED_MISMATCH);
            return HF_INVALID_PATCH_HANDLE;
        }

        if (!REL::WriteSafe(static_cast<std::uintptr_t>(a_address), replacement.data(), replacement.size())) {
            ReportMemoryFailure(owner, context.logger, HF_ERROR_MEMORY_WRITE_FAILED);
            return HF_INVALID_PATCH_HANDLE;
        }

        std::vector<std::uint8_t> verified(a_size);
        if (!ReadBytes(a_address, verified.data(), a_size) || verified != replacement) {
            // Best effort rollback because the write did not verify.
            REL::WriteSafe(static_cast<std::uintptr_t>(a_address), expected.data(), expected.size());
            ReportMemoryFailure(owner, context.logger, HF_ERROR_MEMORY_WRITE_VERIFY_FAILED);
            return HF_INVALID_PATCH_HANDLE;
        }

        auto handle = _nextPatchHandle.fetch_add(1, std::memory_order_relaxed);
        if (handle == HF_INVALID_PATCH_HANDLE) {
            handle = _nextPatchHandle.fetch_add(1, std::memory_order_relaxed);
        }

        PatchRecord record{};
        record.handle = handle;
        record.address = a_address;
        record.size = a_size;
        record.status = HF_PATCH_STATUS_APPLIED;
        record.checkpoint = context.checkpoint;
        record.owner = owner;
        record.logger = context.logger;
        record.label = SafeLabel(a_label);
        record.original = std::move(expected);
        record.replacement = std::move(replacement);
        _patches.emplace_back(std::move(record));
        return handle;
    }

    bool MemoryManager::RestorePatch(const HF_PatchHandle a_handle) noexcept
    {
        const auto context = ModuleContext::Current();
        if (!context.name || !*context.name) {
            ReportMemoryFailure("<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_MEMORY_OWNER_UNKNOWN);
            return false;
        }

        std::scoped_lock mutation{ MutationMutex() };
        std::scoped_lock lock{ _lock };
        const auto it = std::ranges::find_if(_patches, [a_handle](const PatchRecord& patch) {
            return patch.handle == a_handle;
        });
        if (it == _patches.end()) {
            return false;
        }

        if (it->owner != context.name) {
            ReportMemoryFailure(context.name, context.logger, HF_ERROR_MEMORY_PATCH_OWNER_MISMATCH);
            return false;
        }

        if (it->status == HF_PATCH_STATUS_RESTORED) {
            return true;
        }

        std::vector<std::uint8_t> current(it->size);
        if (!ReadBytes(it->address, current.data(), it->size)) {
            ReportMemoryFailure(it->owner, it->logger, HF_ERROR_MEMORY_READ_FAILED);
            return false;
        }

        if (current != it->replacement) {
            it->status = HF_PATCH_STATUS_MODIFIED;
            ReportMemoryFailure(it->owner, it->logger, HF_ERROR_MEMORY_RESTORE_CONFLICT);
            return false;
        }

        if (!REL::WriteSafe(static_cast<std::uintptr_t>(it->address), it->original.data(), it->original.size())) {
            ReportMemoryFailure(it->owner, it->logger, HF_ERROR_MEMORY_WRITE_FAILED);
            return false;
        }

        std::vector<std::uint8_t> verified(it->size);
        if (!ReadBytes(it->address, verified.data(), it->size) || verified != it->original) {
            ReportMemoryFailure(it->owner, it->logger, HF_ERROR_MEMORY_WRITE_VERIFY_FAILED);
            return false;
        }

        it->status = HF_PATCH_STATUS_RESTORED;
        return true;
    }

    bool MemoryManager::VerifyPatch(
        const HF_PatchHandle a_handle,
        HF_PatchStatus& a_outStatus) noexcept
    {
        a_outStatus = HF_PATCH_STATUS_UNKNOWN;
        std::string reportOwner;
        HF_LogHandle reportLogger = HF_INVALID_LOG_HANDLE;
        bool shouldReport = false;

        {
            std::scoped_lock lock{ _lock };
            const auto it = std::ranges::find_if(_patches, [a_handle](const PatchRecord& patch) {
                return patch.handle == a_handle;
            });
            if (it == _patches.end()) {
                return false;
            }

            if (it->status == HF_PATCH_STATUS_RESTORED) {
                a_outStatus = it->status;
                return true;
            }

            std::vector<std::uint8_t> current(it->size);
            if (!ReadBytes(it->address, current.data(), it->size)) {
                a_outStatus = it->status;
                return false;
            }

            if (current == it->replacement) {
                it->status = HF_PATCH_STATUS_APPLIED;
                a_outStatus = it->status;
                return true;
            }

            it->status = HF_PATCH_STATUS_MODIFIED;
            a_outStatus = it->status;
            if (!it->conflictReported) {
                it->conflictReported = true;
                reportOwner = it->owner;
                reportLogger = it->logger;
                shouldReport = true;
            }
        }

        if (shouldReport) {
            ReportMemoryFailure(reportOwner, reportLogger, HF_ERROR_MEMORY_PATCH_MODIFIED);
        }
        return true;
    }

    std::uint32_t MemoryManager::GetPatchCount() const noexcept
    {
        std::scoped_lock lock{ _lock };
        return static_cast<std::uint32_t>(std::ranges::count_if(_patches, [](const PatchRecord& patch) {
            return patch.status == HF_PATCH_STATUS_APPLIED || patch.status == HF_PATCH_STATUS_MODIFIED;
        }));
    }

    void MemoryManager::FillPatchRecord(const PatchRecord& a_patch, HF_PatchRecordV1& a_out) noexcept
    {
        a_out = {};
        a_out.structSize = sizeof(HF_PatchRecordV1);
        a_out.handle = a_patch.handle;
        a_out.address = a_patch.address;
        a_out.size = a_patch.size;
        a_out.status = a_patch.status;
        a_out.checkpoint = a_patch.checkpoint;
        std::snprintf(a_out.owner, sizeof(a_out.owner), "%s", a_patch.owner.c_str());
        std::snprintf(a_out.label, sizeof(a_out.label), "%s", a_patch.label.c_str());
    }

    bool MemoryManager::GetPatchByIndex(
        const std::uint32_t a_index,
        HF_PatchRecordV1& a_outRecord) const noexcept
    {
        std::scoped_lock lock{ _lock };
        std::uint32_t currentIndex = 0;
        for (const auto& patch : _patches) {
            if (patch.status != HF_PATCH_STATUS_APPLIED && patch.status != HF_PATCH_STATUS_MODIFIED) {
                continue;
            }
            if (currentIndex++ == a_index) {
                FillPatchRecord(patch, a_outRecord);
                return true;
            }
        }
        return false;
    }

    std::uint32_t MemoryManager::GetUnrestoredCountOwnedBy(const std::string_view a_moduleName) const noexcept
    {
        if (a_moduleName.empty()) {
            return 0;
        }
        std::scoped_lock lock{ _lock };
        return static_cast<std::uint32_t>(std::ranges::count_if(_patches, [a_moduleName](const PatchRecord& patch) {
            return patch.owner == a_moduleName && patch.status != HF_PATCH_STATUS_RESTORED;
        }));
    }

    std::uint32_t MemoryManager::RestoreOwnedBy(const std::string_view a_moduleName) noexcept
    {
        if (a_moduleName.empty()) {
            return 0;
        }

        std::vector<HF_PatchHandle> handles;
        HF_LogHandle logger = HF_INVALID_LOG_HANDLE;
        {
            std::scoped_lock lock{ _lock };
            for (const auto& patch : _patches) {
                if (patch.owner == a_moduleName && patch.status != HF_PATCH_STATUS_RESTORED) {
                    handles.push_back(patch.handle);
                    if (logger == HF_INVALID_LOG_HANDLE) {
                        logger = patch.logger;
                    }
                }
            }
        }

        std::uint32_t restored = 0;
        ModuleContext::Scope scope{ a_moduleName.data(), logger };
        for (const auto handle : handles) {
            if (RestorePatch(handle)) {
                ++restored;
            }
        }
        return restored;
    }

    void MemoryManager::PruneRestored() noexcept
    {
        std::scoped_lock lock{ _lock };
        std::erase_if(_patches, [](const PatchRecord& patch) {
            return patch.status == HF_PATCH_STATUS_RESTORED;
        });
    }

    bool MemoryManager::FindActiveOverlap(
        const HF_Address a_address,
        const std::uint32_t a_size,
        std::string& a_outOwner,
        std::string& a_outLabel) const noexcept
    {
        a_outOwner.clear();
        a_outLabel.clear();
        std::scoped_lock lock{ _lock };
        for (const auto& patch : _patches) {
            if (patch.status != HF_PATCH_STATUS_APPLIED && patch.status != HF_PATCH_STATUS_MODIFIED) {
                continue;
            }
            if (RangesOverlap(a_address, a_size, patch.address, patch.size)) {
                a_outOwner = patch.owner;
                a_outLabel = patch.label;
                return true;
            }
        }
        return false;
    }

    std::uint32_t MemoryManager::AuditPatches() noexcept
    {
        std::vector<HF_PatchHandle> activeHandles;
        {
            std::scoped_lock lock{ _lock };
            activeHandles.reserve(_patches.size());
            for (const auto& patch : _patches) {
                if (patch.status == HF_PATCH_STATUS_APPLIED || patch.status == HF_PATCH_STATUS_MODIFIED) {
                    activeHandles.push_back(patch.handle);
                }
            }
        }

        std::uint32_t modified = 0;
        for (const auto handle : activeHandles) {
            HF_PatchStatus status{};
            if (VerifyPatch(handle, status) && status == HF_PATCH_STATUS_MODIFIED) {
                ++modified;
            }
        }
        return modified;
    }
}
