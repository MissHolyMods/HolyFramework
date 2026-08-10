#pragma once

namespace HolyFramework
{
    enum class ModuleSignatureStatus
    {
        Present,
        Missing,
        IoFailure
    };

    class ModuleSignature final
    {
    public:
        static constexpr std::uint64_t kMaxDllBytes = 128ull * 1024ull * 1024ull;

        // Lightweight pre-load check. We only look for the public marker/export
        // strings in the DLL file so an unrelated DLL is normally rejected before
        // LoadLibraryW. The exported structure is validated again after loading.
        [[nodiscard]] static ModuleSignatureStatus CheckFileMarker(
            const std::filesystem::path& a_dllPath) noexcept;

        [[nodiscard]] static const char* SchemeName() noexcept;
    };
}
