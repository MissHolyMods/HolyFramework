#include "pch.h"
#include "ModuleSignature.h"

namespace HolyFramework
{
    namespace
    {
        constexpr std::string_view kExportName = "HF_HolyFrameworkSignature";
        constexpr std::string_view kSignatureText = HF_MODULE_SIGNATURE_TEXT;
        constexpr std::size_t kScanChunkBytes = 64 * 1024;

        [[nodiscard]] bool ContainsText(
            const std::string_view a_bytes,
            const std::string_view a_needle) noexcept
        {
            return !a_needle.empty() && a_bytes.find(a_needle) != std::string_view::npos;
        }
    }

    ModuleSignatureStatus ModuleSignature::CheckFileMarker(
        const std::filesystem::path& a_dllPath) noexcept
    {
        try {
            std::error_code ec;
            const auto size = std::filesystem::file_size(a_dllPath, ec);
            if (ec || size == 0 || size > kMaxDllBytes) {
                return ModuleSignatureStatus::IoFailure;
            }

            std::ifstream input{ a_dllPath, std::ios::binary };
            if (!input) {
                return ModuleSignatureStatus::IoFailure;
            }

            // This is a module marker, not cryptography. Stream the file
            // in bounded chunks instead of allocating a full-DLL buffer.
            constexpr auto overlap =
                (kExportName.size() > kSignatureText.size() ?
                    kExportName.size() : kSignatureText.size()) - 1;

            std::array<char, kScanChunkBytes> chunk{};
            std::string tail;
            tail.reserve(overlap);
            std::string window;
            window.reserve(kScanChunkBytes + overlap);
            bool foundExport = false;
            bool foundSignature = false;

            while (input) {
                input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
                const auto read = input.gcount();
                if (read <= 0) {
                    break;
                }

                window.clear();
                window.append(tail);
                window.append(chunk.data(), static_cast<std::size_t>(read));

                const std::string_view bytes{ window };
                foundExport = foundExport || ContainsText(bytes, kExportName);
                foundSignature = foundSignature || ContainsText(bytes, kSignatureText);
                if (foundExport && foundSignature) {
                    return ModuleSignatureStatus::Present;
                }

                const auto keep = std::min(overlap, window.size());
                tail.assign(window.end() - static_cast<std::ptrdiff_t>(keep), window.end());
            }

            if (input.bad()) {
                return ModuleSignatureStatus::IoFailure;
            }

            return ModuleSignatureStatus::Missing;
        } catch (const std::exception&) {
            return ModuleSignatureStatus::IoFailure;
        } catch (...) {
            return ModuleSignatureStatus::IoFailure;
        }
    }

    const char* ModuleSignature::SchemeName() noexcept
    {
        return "export-marker-v1";
    }
}
