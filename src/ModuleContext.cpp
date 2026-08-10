#include "pch.h"
#include "ModuleContext.h"

namespace
{
    thread_local HolyFramework::ModuleExecutionContext g_context{};
}

namespace HolyFramework
{
    ModuleContext::Scope::Scope(
        const char* const a_name,
        const HF_LogHandle a_logger,
        const std::uint32_t a_checkpoint) noexcept :
        _previous(ModuleContext::Current())
    {
        ModuleContext::Set(ModuleExecutionContext{ a_name, a_logger, a_checkpoint });
    }

    ModuleContext::Scope::~Scope() noexcept
    {
        ModuleContext::Set(_previous);
    }

    ModuleExecutionContext ModuleContext::Current() noexcept
    {
        return g_context;
    }

    void ModuleContext::SetCheckpoint(const std::uint32_t a_checkpoint) noexcept
    {
        g_context.checkpoint = a_checkpoint;
    }

    void ModuleContext::ClearCheckpoint() noexcept
    {
        g_context.checkpoint = 0;
    }

    void ModuleContext::Set(const ModuleExecutionContext a_context) noexcept
    {
        g_context = a_context;
    }
}
