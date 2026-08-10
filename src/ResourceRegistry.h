#pragma once

namespace HolyFramework
{
    class ResourceRegistry final
    {
    public:
        static ResourceRegistry& GetSingleton() noexcept;

        bool PublishCapability(std::string_view a_name, std::uint32_t a_version) noexcept;
        std::uint32_t GetCapabilityCount() const noexcept;
        bool GetCapabilityByIndex(std::uint32_t a_index, HF_CapabilityRecordV1& a_out) const noexcept;
        bool FindCapability(std::string_view a_name, HF_CapabilityRecordV1& a_out) const noexcept;

        HF_ResourceHandle ClaimResource(
            std::string_view a_name,
            HF_ResourceAccess a_access,
            std::uint32_t a_flags,
            std::string_view a_label) noexcept;
        bool ReleaseResource(HF_ResourceHandle a_handle) noexcept;
        bool IsResourceAvailable(std::string_view a_name, HF_ResourceAccess a_access) const noexcept;
        std::uint32_t GetResourceCount() const noexcept;
        bool GetResourceByIndex(std::uint32_t a_index, HF_ResourceRecordV1& a_out) const noexcept;

        std::uint32_t RemoveOwnedBy(std::string_view a_moduleName) noexcept;
        std::uint32_t ReleaseSessionScoped(std::uint64_t a_previousGeneration) noexcept;

    private:
        struct CapabilityRecord
        {
            std::string owner;
            std::string name;
            std::uint32_t version{ 0 };
        };

        struct ResourceRecord
        {
            HF_ResourceHandle handle{ HF_INVALID_RESOURCE_HANDLE };
            std::string owner;
            std::string name;
            std::string label;
            HF_ResourceAccess access{ HF_RESOURCE_ACCESS_SHARED };
            std::uint32_t flags{ HF_RESOURCE_FLAG_NONE };
            std::uint64_t sessionGeneration{ 0 };
            HF_LogHandle logger{ HF_INVALID_LOG_HANDLE };
        };

        ResourceRegistry() = default;

        static std::string NormalizeName(std::string_view a_name) noexcept;
        static bool IsValidName(std::string_view a_name) noexcept;
        static bool NamesEqual(std::string_view a_left, std::string_view a_right) noexcept;
        static void FillCapability(const CapabilityRecord& a_record, HF_CapabilityRecordV1& a_out) noexcept;
        static void FillResource(const ResourceRecord& a_record, HF_ResourceRecordV1& a_out) noexcept;

        mutable std::mutex _lock;
        std::vector<CapabilityRecord> _capabilities;
        std::vector<ResourceRecord> _resources;
        std::atomic<HF_ResourceHandle> _nextResourceHandle{ 1 };
    };
}
