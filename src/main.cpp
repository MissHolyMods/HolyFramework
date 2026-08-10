#include "pch.h"
#include "Diagnostics.h"
#include "ErrorCatalog.h"
#include "EventBus.h"
#include "FrameworkAPI.h"
#include "FrameworkUI.h"
#include "HookManager.h"
#include "ModuleLoader.h"
#include "ModuleSignature.h"
#include "MemoryManager.h"
#include "PresentationService.h"
#include "RuntimeState.h"
#include "SerializationService.h"
#include "ResourceRegistry.h"
#include "TaskScheduler.h"
#include "UIStateService.h"

namespace HolyFramework
{
    namespace
    {
        std::atomic<HF_TaskHandle> g_startupTask{ HF_INVALID_TASK_HANDLE };

        // Framework policy, intentionally not user-configurable. The startup
        // notification is delayed so Fallout's own loading/HUD messages can
        // finish before HolyFramework announces session readiness.
        inline constexpr std::uint32_t kStartupNotificationDelayMs = 20'000;

        void CancelPendingStartupNotification() noexcept
        {
            const auto handle = g_startupTask.exchange(HF_INVALID_TASK_HANDLE, std::memory_order_acq_rel);
            if (handle != HF_INVALID_TASK_HANDLE) {
                TaskScheduler::GetSingleton().Cancel(handle);
            }
        }

        void HF_CALL FinalizeSessionReady(void*)
        {
            g_startupTask.store(HF_INVALID_TASK_HANDLE, std::memory_order_release);

            RuntimeState::GetSingleton().MarkSessionReady();
            EventBus::GetSingleton().Dispatch(HF_EVENT_SESSION_READY);

            const auto count = ModuleLoader::GetSingleton().GetLoadedCount();
            const auto message = std::format(
                "HolyFramework started: {} module{} in operation",
                count,
                count == 1 ? "" : "s");

            if (!QueueNotification(message)) {
                Diagnostics::ReportFrameworkFailureForModule(
                    "HolyFramework",
                    HF_INVALID_LOG_HANDLE,
                    HF_ERROR_UI_NOTIFICATION_FAILED);
                REX::WARN("HolyFramework startup HUD notification was not queued");
            }
        }

        void ScheduleStartupNotification()
        {
            CancelPendingStartupNotification();

            const auto handle = TaskScheduler::GetSingleton().QueueFrameworkDelayed(
                HF_TASK_QUEUE_GAME,
                kStartupNotificationDelayMs,
                FinalizeSessionReady,
                nullptr,
                HF_TASK_FLAG_CANCEL_ON_SESSION_CHANGE |
                    HF_TASK_FLAG_REQUIRE_SESSION_ACTIVE);

            g_startupTask.store(handle, std::memory_order_release);
        }
    }

    static void MessageHandler(F4SE::MessagingInterface::Message* const a_message)
    {
        if (!a_message) {
            return;
        }

        HF_Event event{};
        switch (a_message->type) {
        case F4SE::MessagingInterface::kPostLoad:
            event = HF_EVENT_POST_LOAD;
            break;
        case F4SE::MessagingInterface::kPostPostLoad:
            event = HF_EVENT_POST_POST_LOAD;
            break;
        case F4SE::MessagingInterface::kPreLoadGame:
            event = HF_EVENT_PRE_LOAD_GAME;
            break;
        case F4SE::MessagingInterface::kPostLoadGame:
            event = HF_EVENT_POST_LOAD_GAME;
            break;
        case F4SE::MessagingInterface::kPreSaveGame:
            event = HF_EVENT_PRE_SAVE_GAME;
            break;
        case F4SE::MessagingInterface::kPostSaveGame:
            event = HF_EVENT_POST_SAVE_GAME;
            break;
        case F4SE::MessagingInterface::kDeleteGame:
            event = HF_EVENT_DELETE_GAME;
            break;
        case F4SE::MessagingInterface::kInputLoaded:
            event = HF_EVENT_INPUT_LOADED;
            break;
        case F4SE::MessagingInterface::kNewGame:
            event = HF_EVENT_NEW_GAME;
            break;
        case F4SE::MessagingInterface::kGameLoaded:
            event = HF_EVENT_GAME_LOADED;
            break;
        case F4SE::MessagingInterface::kGameDataReady:
            event = HF_EVENT_GAME_DATA_READY;
            break;
        default:
            return;
        }

        const auto previousGeneration = RuntimeState::GetSingleton().GetSessionGeneration();
        RuntimeState::GetSingleton().OnEvent(event);

        // RE::UI is not guaranteed to exist during the earliest F4SE phases.
        // Repeated installation attempts are harmless and stop after the menu
        // observer is registered successfully.
        UIStateService::GetSingleton().TryInstall();
        // Presentation subscriptions may be registered before DXGI exists.
        // Retry opportunistically on lifecycle events; the 5-second integrity
        // monitor remains a fallback if the swap chain appears between events.
        (void)PresentationService::GetSingleton().AuditHooks();

        EventBus::GetSingleton().Dispatch(event);

        // Session-scoped logical resource claims are released only after module
        // callbacks for the boundary event have run. NEW_GAME increments the
        // generation first, so claims created by its callbacks belong to the new
        // generation and are preserved.
        if (event == HF_EVENT_PRE_LOAD_GAME || event == HF_EVENT_NEW_GAME) {
            ResourceRegistry::GetSingleton().ReleaseSessionScoped(previousGeneration);
        }

        // Restored registry entries are session-local history. Prune them only
        // after session-boundary callbacks have run so modules can still inspect
        // their just-restored handles during PRE_LOAD_GAME/NEW_GAME cleanup.
        if (event == HF_EVENT_PRE_LOAD_GAME || event == HF_EVENT_NEW_GAME) {
            HookManager::GetSingleton().PruneRestored();
            MemoryManager::GetSingleton().PruneRestored();
        }

        // Show the framework status only after an actual game session becomes active.
        // The fixed framework post-load delay avoids competing with loading-screen/HUD messages.
        if (event == HF_EVENT_POST_LOAD_GAME || event == HF_EVENT_NEW_GAME) {
            ScheduleStartupNotification();
        } else if (event == HF_EVENT_PRE_LOAD_GAME) {
            CancelPendingStartupNotification();
        }
    }
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
    F4SE::InitInfo initInfo{};
    initInfo.logName = "HolyFramework";
    initInfo.logLevel = REX::ELogLevel::Info;
    initInfo.logPattern = "[%T.%e] [%L] %v";
    // HolyFramework owns one process-lifetime trampoline. We create it
    // ourselves below instead of asking F4SE's branch pool for 64 KiB first;
    // some F4SE builds reject that branch-pool request and CommonLib logs a
    // harmless error before falling back to the same local allocation path.
    initInfo.trampoline = false;
    initInfo.trampolineSize = 0;
    initInfo.hook = false;
    F4SE::Init(a_f4se, initInfo);

    HolyFramework::SetRuntimeVersion(a_f4se->RuntimeVersion());

    HolyFramework::InitializeErrorCatalog();

    REX::INFO("HolyFramework module signature check: {}", HolyFramework::ModuleSignature::SchemeName());

    REX::INFO("HolyFramework 0.35.0 loading for Fallout 4 {}", a_f4se->RuntimeVersion().string());

    HolyFramework::Diagnostics::Install();

    try {
        REL::GetTrampoline().create(64 * 1024);
    } catch (const std::exception&) {
        HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
            "HolyFramework",
            HF_INVALID_LOG_HANDLE,
            HF_ERROR_HOOK_TRAMPOLINE_EXHAUSTED);
        REX::CRITICAL("HolyFramework startup aborted: trampoline creation failed");
        return false;
    } catch (...) {
        HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
            "HolyFramework",
            HF_INVALID_LOG_HANDLE,
            HF_ERROR_HOOK_TRAMPOLINE_EXHAUSTED);
        REX::CRITICAL("HolyFramework startup aborted: trampoline creation failed");
        return false;
    }

    // HolyFramework owns the single F4SE serialization channel and multiplexes
    // module-local records inside it. Serialization is optional for framework
    // startup; modules can query HF_SerializationV1::IsAvailable().
    HolyFramework::SerializationService::GetSingleton().Initialize();

    const auto messaging = F4SE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(HolyFramework::MessageHandler)) {
        HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
            "HolyFramework",
            HF_INVALID_LOG_HANDLE,
            HF_ERROR_FRAMEWORK_MESSAGING_LISTENER);
        REX::CRITICAL("HolyFramework startup aborted: F4SE messaging listener unavailable");
        return false;
    }

    HolyFramework::SetReady(true);

    const auto api = HolyFramework::GetAPI(HF_ABI_VERSION);
    const auto loadedModules = HolyFramework::ModuleLoader::GetSingleton().LoadAll(api);

    // Audit only patches/hooks registered through HolyFramework. The monitor never
    // scans or alters unrelated memory; it verifies ownership records every 5 s.
    HolyFramework::MemoryManager::GetSingleton().StartIntegrityMonitor();

    REX::INFO("HolyFramework loaded successfully; {} native module(s) active", loadedModules);
    return true;
}
