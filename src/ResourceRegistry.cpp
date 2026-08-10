#include "pch.h"
#include "ResourceRegistry.h"

#include "Diagnostics.h"
#include "ModuleContext.h"
#include "RuntimeState.h"

namespace HolyFramework
{
    namespace
    {
        constexpr std::size_t kMaxLogicalNameLength = 95;
        constexpr std::size_t kMaxLabelLength = 95;

        std::string SafeLabel(const std::string_view a_label)
        {
            if (a_label.empty()) {
                return {};
            }
            return std::string{ a_label.substr(0, kMaxLabelLength) };
        }
    }

    ResourceRegistry& ResourceRegistry::GetSingleton() noexcept
    {
        static ResourceRegistry* instance = new ResourceRegistry();
        return *instance;
    }

    bool ResourceRegistry::IsValidName(const std::string_view a_name) noexcept
    {
        if (a_name.empty() || a_name.size() > kMaxLogicalNameLength) {
            return false;
        }
        for (const unsigned char ch : a_name) {
            if (std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-' || ch == ':' || ch == '/') {
                continue;
            }
            return false;
        }
        return true;
    }

    std::string ResourceRegistry::NormalizeName(const std::string_view a_name) noexcept
    {
        std::string result;
        result.reserve(a_name.size());
        for (const unsigned char ch : a_name) {
            result.push_back(static_cast<char>(std::tolower(ch)));
        }
        return result;
    }

    bool ResourceRegistry::NamesEqual(const std::string_view a_left, const std::string_view a_right) noexcept
    {
        return NormalizeName(a_left) == NormalizeName(a_right);
    }

    void ResourceRegistry::FillCapability(const CapabilityRecord& a_record, HF_CapabilityRecordV1& a_out) noexcept
    {
        a_out = {};
        a_out.structSize = sizeof(HF_CapabilityRecordV1);
        a_out.version = a_record.version;
        std::snprintf(a_out.owner, sizeof(a_out.owner), "%s", a_record.owner.c_str());
        std::snprintf(a_out.name, sizeof(a_out.name), "%s", a_record.name.c_str());
    }

    void ResourceRegistry::FillResource(const ResourceRecord& a_record, HF_ResourceRecordV1& a_out) noexcept
    {
        a_out = {};
        a_out.structSize = sizeof(HF_ResourceRecordV1);
        a_out.handle = a_record.handle;
        a_out.access = a_record.access;
        a_out.flags = a_record.flags;
        a_out.sessionGeneration = a_record.sessionGeneration;
        std::snprintf(a_out.owner, sizeof(a_out.owner), "%s", a_record.owner.c_str());
        std::snprintf(a_out.name, sizeof(a_out.name), "%s", a_record.name.c_str());
        std::snprintf(a_out.label, sizeof(a_out.label), "%s", a_record.label.c_str());
    }

    bool ResourceRegistry::PublishCapability(const std::string_view a_name, const std::uint32_t a_version) noexcept
    {
        const auto context = ModuleContext::Current();
        if (!context.name || !*context.name) {
            Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_CAPABILITY_OWNER_UNKNOWN);
            return false;
        }
        if (!IsValidName(a_name) || a_version == 0) {
            Diagnostics::ReportFrameworkFailureForModule(
                context.name,
                context.logger,
                HF_ERROR_CAPABILITY_INVALID_REQUEST);
            return false;
        }

        const auto normalized = NormalizeName(a_name);
        std::scoped_lock lock{ _lock };
        for (auto& record : _capabilities) {
            if (NamesEqual(record.owner, context.name) && record.name == normalized) {
                record.version = a_version;
                return true;
            }
        }
        _capabilities.push_back(CapabilityRecord{
            .owner = context.name,
            .name = normalized,
            .version = a_version
        });
        return true;
    }

    std::uint32_t ResourceRegistry::GetCapabilityCount() const noexcept
    {
        std::scoped_lock lock{ _lock };
        return static_cast<std::uint32_t>(_capabilities.size());
    }

    bool ResourceRegistry::GetCapabilityByIndex(const std::uint32_t a_index, HF_CapabilityRecordV1& a_out) const noexcept
    {
        std::scoped_lock lock{ _lock };
        if (a_index >= _capabilities.size()) {
            return false;
        }
        FillCapability(_capabilities[a_index], a_out);
        return true;
    }

    bool ResourceRegistry::FindCapability(const std::string_view a_name, HF_CapabilityRecordV1& a_out) const noexcept
    {
        if (!IsValidName(a_name)) {
            return false;
        }
        const auto normalized = NormalizeName(a_name);
        std::scoped_lock lock{ _lock };
        const auto it = std::ranges::find_if(_capabilities, [&](const CapabilityRecord& record) {
            return record.name == normalized;
        });
        if (it == _capabilities.end()) {
            return false;
        }
        FillCapability(*it, a_out);
        return true;
    }

    bool ResourceRegistry::IsResourceAvailable(const std::string_view a_name, const HF_ResourceAccess a_access) const noexcept
    {
        if (!IsValidName(a_name) || (a_access != HF_RESOURCE_ACCESS_SHARED && a_access != HF_RESOURCE_ACCESS_EXCLUSIVE)) {
            return false;
        }
        const auto normalized = NormalizeName(a_name);
        std::scoped_lock lock{ _lock };
        for (const auto& record : _resources) {
            if (record.name != normalized) {
                continue;
            }
            if (a_access == HF_RESOURCE_ACCESS_EXCLUSIVE || record.access == HF_RESOURCE_ACCESS_EXCLUSIVE) {
                return false;
            }
        }
        return true;
    }

    HF_ResourceHandle ResourceRegistry::ClaimResource(
        const std::string_view a_name,
        const HF_ResourceAccess a_access,
        const std::uint32_t a_flags,
        const std::string_view a_label) noexcept
    {
        const auto context = ModuleContext::Current();
        if (!context.name || !*context.name) {
            Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_RESOURCE_OWNER_UNKNOWN);
            return HF_INVALID_RESOURCE_HANDLE;
        }
        if (!IsValidName(a_name) ||
            (a_access != HF_RESOURCE_ACCESS_SHARED && a_access != HF_RESOURCE_ACCESS_EXCLUSIVE) ||
            (a_flags & ~static_cast<std::uint32_t>(HF_RESOURCE_FLAG_SESSION_SCOPED)) != 0) {
            Diagnostics::ReportFrameworkFailureForModule(
                context.name,
                context.logger,
                HF_ERROR_RESOURCE_INVALID_REQUEST);
            return HF_INVALID_RESOURCE_HANDLE;
        }

        const auto normalized = NormalizeName(a_name);
        const auto generation = RuntimeState::GetSingleton().GetSessionGeneration();

        std::scoped_lock lock{ _lock };
        for (auto& record : _resources) {
            if (record.name != normalized) {
                continue;
            }
            if (NamesEqual(record.owner, context.name) && record.access == a_access && record.flags == a_flags) {
                if ((record.flags & HF_RESOURCE_FLAG_SESSION_SCOPED) != 0) {
                    record.sessionGeneration = generation;
                }
                return record.handle;
            }
            if (a_access == HF_RESOURCE_ACCESS_EXCLUSIVE || record.access == HF_RESOURCE_ACCESS_EXCLUSIVE) {
                Diagnostics::ReportFrameworkWarningForModule(
                    context.name,
                    context.logger,
                    HF_ERROR_RESOURCE_CONFLICT);
                return HF_INVALID_RESOURCE_HANDLE;
            }
        }

        auto handle = _nextResourceHandle.fetch_add(1, std::memory_order_relaxed);
        if (handle == HF_INVALID_RESOURCE_HANDLE) {
            handle = _nextResourceHandle.fetch_add(1, std::memory_order_relaxed);
        }
        _resources.push_back(ResourceRecord{
            .handle = handle,
            .owner = context.name,
            .name = normalized,
            .label = SafeLabel(a_label),
            .access = a_access,
            .flags = a_flags,
            .sessionGeneration = generation,
            .logger = context.logger
        });
        return handle;
    }

    bool ResourceRegistry::ReleaseResource(const HF_ResourceHandle a_handle) noexcept
    {
        if (a_handle == HF_INVALID_RESOURCE_HANDLE) {
            return false;
        }
        const auto context = ModuleContext::Current();
        std::scoped_lock lock{ _lock };
        const auto it = std::ranges::find_if(_resources, [a_handle](const ResourceRecord& record) {
            return record.handle == a_handle;
        });
        if (it == _resources.end()) {
            return false;
        }
        if (!context.name || !NamesEqual(it->owner, context.name)) {
            Diagnostics::ReportFrameworkFailureForModule(
                context.name ? context.name : "<unknown>",
                context.logger,
                HF_ERROR_RESOURCE_OWNER_MISMATCH);
            return false;
        }
        _resources.erase(it);
        return true;
    }

    std::uint32_t ResourceRegistry::GetResourceCount() const noexcept
    {
        std::scoped_lock lock{ _lock };
        return static_cast<std::uint32_t>(_resources.size());
    }

    bool ResourceRegistry::GetResourceByIndex(const std::uint32_t a_index, HF_ResourceRecordV1& a_out) const noexcept
    {
        std::scoped_lock lock{ _lock };
        if (a_index >= _resources.size()) {
            return false;
        }
        FillResource(_resources[a_index], a_out);
        return true;
    }

    std::uint32_t ResourceRegistry::RemoveOwnedBy(const std::string_view a_moduleName) noexcept
    {
        if (a_moduleName.empty()) {
            return 0;
        }
        std::scoped_lock lock{ _lock };
        const auto oldCapabilities = _capabilities.size();
        const auto oldResources = _resources.size();
        std::erase_if(_capabilities, [&](const CapabilityRecord& record) {
            return NamesEqual(record.owner, a_moduleName);
        });
        std::erase_if(_resources, [&](const ResourceRecord& record) {
            return NamesEqual(record.owner, a_moduleName);
        });
        return static_cast<std::uint32_t>((oldCapabilities - _capabilities.size()) + (oldResources - _resources.size()));
    }

    std::uint32_t ResourceRegistry::ReleaseSessionScoped(const std::uint64_t a_previousGeneration) noexcept
    {
        std::scoped_lock lock{ _lock };
        const auto oldSize = _resources.size();
        std::erase_if(_resources, [&](const ResourceRecord& record) {
            return (record.flags & HF_RESOURCE_FLAG_SESSION_SCOPED) != 0 &&
                   record.sessionGeneration <= a_previousGeneration;
        });
        return static_cast<std::uint32_t>(oldSize - _resources.size());
    }
}
