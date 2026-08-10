#pragma once

namespace HolyFramework
{
    void SetRuntimeVersion(REL::Version a_version) noexcept;
    void SetReady(bool a_ready) noexcept;
    [[nodiscard]] const HF_API* GetAPI(std::uint32_t a_requestedABIVersion) noexcept;
}
