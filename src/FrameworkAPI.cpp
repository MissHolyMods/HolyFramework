#include "pch.h"
#include "EventBus.h"
#include "Diagnostics.h"
#include "ConfigManager.h"
#include "ErrorCatalog.h"
#include "FrameworkAPI.h"
#include "FrameworkUI.h"
#include "GameStateService.h"
#include "GameSettingsManager.h"
#include "GraphicsService.h"
#include "GameTimeService.h"
#include "EnvironmentService.h"
#include "LightingService.h"
#include "RenderPipelineService.h"
#include "PresentationService.h"
#include "SerializationService.h"
#include "FrameTimingService.h"
#include "FramePacingService.h"
#include "WindowService.h"
#include "DisplayService.h"
#include "PresentationPolicyService.h"
#include "StateFPSService.h"
#include "CPUSchedulingService.h"
#include "RuntimeTuningService.h"
#include "FormService.h"
#include "ReferenceActorService.h"
#include "PlayerMovementService.h"
#include "PostProcessService.h"
#include "HookManager.h"
#include "ModuleContext.h"
#include "ModuleLoader.h"
#include "ModuleLogManager.h"
#include "MemoryManager.h"
#include "PerformanceMonitor.h"
#include "ResourceRegistry.h"
#include "RuntimeState.h"
#include "TaskScheduler.h"
#include "UIStateService.h"
#if defined(_MSC_VER)
#    include <intrin.h>
#    define HF_CAPTURE_CALLER_ADDRESS() _ReturnAddress()
#else
#    define HF_CAPTURE_CALLER_ADDRESS() nullptr
#endif

namespace
{
    std::atomic_bool g_ready{ false };
    REL::Version g_runtimeVersion{};

    [[nodiscard]] HF_Version ToHFVersion(const REL::Version& a_version) noexcept
    {
        return HF_Version{
            .major = a_version.major(),
            .minor = a_version.minor(),
            .patch = a_version.patch(),
            .build = a_version.build()
        };
    }


    bool EnsureCallerModuleScope(
        const void* const a_callerAddress,
        std::string& a_nameStorage,
        std::unique_ptr<HolyFramework::ModuleContext::Scope>& a_scope)
    {
        const auto current = HolyFramework::ModuleContext::Current();
        if (current.name && *current.name) {
            return true;
        }

        HF_LogHandle logger = HF_INVALID_LOG_HANDLE;
        if (!a_callerAddress ||
            !HolyFramework::ModuleLoader::GetSingleton().FindExecutionIdentityByCodeAddress(
                a_callerAddress,
                a_nameStorage,
                logger)) {
            return false;
        }

        // Preserve a checkpoint set by the module on its own worker thread even
        // when the thread had no HolyFramework name/logger context yet.
        a_scope = std::make_unique<HolyFramework::ModuleContext::Scope>(
            a_nameStorage.c_str(),
            logger,
            current.checkpoint);
        return true;
    }

    [[nodiscard]] bool NamesEqualInsensitive(const std::string_view a_left, const std::string_view a_right) noexcept
    {
        if (a_left.size() != a_right.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a_left.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a_left[i])) !=
                std::tolower(static_cast<unsigned char>(a_right[i]))) {
                return false;
            }
        }
        return true;
    }

    const char* HF_CALL CoreGetFrameworkName()
    {
        return "HolyFramework";
    }

    HF_Version HF_CALL CoreGetFrameworkVersion()
    {
        return HF_Version{ 0, 35, 0, 0 };
    }

    HF_Version HF_CALL CoreGetRuntimeVersion()
    {
        return ToHFVersion(g_runtimeVersion);
    }

    HF_Version HF_CALL CoreGetF4SEVersion()
    {
        return ToHFVersion(F4SE::GetF4SEVersion());
    }

    HF_Bool HF_CALL CoreIsReady()
    {
        return g_ready.load(std::memory_order_acquire) ? HF_TRUE : HF_FALSE;
    }

    std::uint32_t HF_CALL CoreGetLoadedModuleCount()
    {
        return HolyFramework::ModuleLoader::GetSingleton().GetLoadedCount();
    }

    void WriteCentralLog(const HF_LogLevel a_level, const std::string_view a_message)
    {
        switch (a_level) {
        case HF_LOG_TRACE:
            REX::TRACE("[Module] {}", a_message);
            break;
        case HF_LOG_DEBUG:
            REX::DEBUG("[Module] {}", a_message);
            break;
        case HF_LOG_WARNING:
            REX::WARN("[Module] {}", a_message);
            break;
        case HF_LOG_ERROR:
            REX::ERROR("[Module] {}", a_message);
            break;
        case HF_LOG_CRITICAL:
            REX::CRITICAL("[Module] {}", a_message);
            break;
        case HF_LOG_INFO:
        default:
            REX::INFO("[Module] {}", a_message);
            break;
        }
    }

    HF_LogHandle HF_CALL LogGetCurrentLogger()
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope);
        return HolyFramework::ModuleContext::Current().logger;
    }

    HF_LogHandle HF_CALL LogOpen(const char* const a_moduleName)
    {
        if (!a_moduleName || !*a_moduleName) {
            return HF_INVALID_LOG_HANDLE;
        }

        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            return HF_INVALID_LOG_HANDLE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        if (!context.name || !NamesEqualInsensitive(context.name, a_moduleName)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name ? context.name : "<unknown>",
                context.logger,
                HF_ERROR_LOG_OWNER_MISMATCH);
            return HF_INVALID_LOG_HANDLE;
        }
        return HolyFramework::ModuleLogManager::GetSingleton().Open(context.name);
    }

    void HF_CALL LogWrite(
        const HF_LogHandle a_logger,
        const HF_LogLevel a_level,
        const char* const a_message)
    {
        const std::string_view message = a_message ? std::string_view{ a_message } : std::string_view{};
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope);
        const auto context = HolyFramework::ModuleContext::Current();

        if (a_logger == HF_INVALID_LOG_HANDLE) {
            if (context.name) {
                HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                    context.name,
                    context.logger,
                    HF_ERROR_LOG_OWNER_MISMATCH);
                return;
            }
            WriteCentralLog(a_level, message);
            return;
        }
        if (!context.name) {
            return;
        }
        if (!HolyFramework::ModuleLogManager::GetSingleton().IsOwnedBy(a_logger, context.name)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name,
                context.logger,
                HF_ERROR_LOG_OWNER_MISMATCH);
            return;
        }
        HolyFramework::ModuleLogManager::GetSingleton().Write(a_logger, a_level, message);
    }

    void HF_CALL LogFlush(const HF_LogHandle a_logger)
    {
        if (a_logger == HF_INVALID_LOG_HANDLE) {
            return;
        }
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope);
        const auto context = HolyFramework::ModuleContext::Current();
        if (!context.name || !HolyFramework::ModuleLogManager::GetSingleton().IsOwnedBy(a_logger, context.name)) {
            return;
        }
        HolyFramework::ModuleLogManager::GetSingleton().Flush(a_logger);
    }

    HF_SubscriptionHandle HF_CALL EventsSubscribe(
        const HF_Event a_event,
        const HF_EventCallback a_callback,
        void* const a_userData)
    {
        if (!a_callback) {
            return 0;
        }
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            // Fallback to the callback image for worker-thread subscriptions.
            HF_LogHandle logger = HF_INVALID_LOG_HANDLE;
            if (!HolyFramework::ModuleLoader::GetSingleton().FindExecutionIdentityByCodeAddress(
                    reinterpret_cast<const void*>(a_callback), owner, logger)) {
                HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                    "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_EVENT_OWNER_UNKNOWN);
                return 0;
            }
            const auto current = HolyFramework::ModuleContext::Current();
            scope = std::make_unique<HolyFramework::ModuleContext::Scope>(owner.c_str(), logger, current.checkpoint);
        }
        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::EventBus::GetSingleton().SubscribeOwned(
            a_event, a_callback, a_userData, context.name, context.logger);
    }

    HF_Bool HF_CALL EventsUnsubscribe(const HF_SubscriptionHandle a_handle)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_EVENT_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        std::string actualOwner;
        if (HolyFramework::EventBus::GetSingleton().UnsubscribeOwned(a_handle, context.name, &actualOwner)) {
            return HF_TRUE;
        }
        if (!actualOwner.empty()) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name, context.logger, HF_ERROR_EVENT_OWNER_MISMATCH);
        }
        return HF_FALSE;
    }


    void HF_CALL DiagnosticsSetCheckpoint(const std::uint32_t a_checkpoint)
    {
        HolyFramework::ModuleContext::SetCheckpoint(a_checkpoint);
    }

    void HF_CALL DiagnosticsClearCheckpoint()
    {
        HolyFramework::ModuleContext::ClearCheckpoint();
    }

    void HF_CALL DiagnosticsReportFailure(const HF_ErrorCode a_code)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope);
        HolyFramework::Diagnostics::ReportFailure(a_code);
    }

    const char* HF_CALL DiagnosticsGetErrorName(const HF_ErrorCode a_code)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope);
        const auto context = HolyFramework::ModuleContext::Current();
        const auto prefix = context.name ?
            HolyFramework::ResolveModuleErrorPrefix(context.name) :
            std::string{ "HFW" };
        return HolyFramework::GetErrorName(prefix, a_code);
    }

    const char* HF_CALL DiagnosticsGetErrorDescription(const HF_ErrorCode a_code)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope);
        const auto context = HolyFramework::ModuleContext::Current();
        const auto prefix = context.name ?
            HolyFramework::ResolveModuleErrorPrefix(context.name) :
            std::string{ "HFW" };
        return HolyFramework::GetErrorDescription(prefix, a_code);
    }

    HF_Bool HF_CALL UIShowNotification(const char* const a_message, const HF_Bool a_warning)
    {
        if (!a_message || !*a_message) {
            return HF_FALSE;
        }
        return HolyFramework::QueueNotification(a_message, a_warning == HF_TRUE) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL UIIsMenuOpen(const char* const a_menuName)
    {
        return HolyFramework::UIStateService::GetSingleton().IsMenuOpen(a_menuName) ? HF_TRUE : HF_FALSE;
    }

    std::uint32_t HF_CALL UIGetStateFlags()
    {
        return HolyFramework::UIStateService::GetSingleton().GetStateFlags();
    }

    HF_Bool HF_CALL UIHasState(const std::uint32_t a_requiredFlags)
    {
        const auto flags = HolyFramework::UIStateService::GetSingleton().GetStateFlags();
        return (flags & a_requiredFlags) == a_requiredFlags ? HF_TRUE : HF_FALSE;
    }

    HF_UIMenuSubscriptionHandle HF_CALL UISubscribeMenuEvents(
        const char* const a_menuNameFilter,
        const HF_UIMenuEventCallback a_callback,
        void* const a_userData)
    {
        if (!a_callback) {
            return HF_INVALID_UI_MENU_SUBSCRIPTION_HANDLE;
        }

        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_UI_MENU_OWNER_UNKNOWN);
            return HF_INVALID_UI_MENU_SUBSCRIPTION_HANDLE;
        }

        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::UIStateService::GetSingleton().SubscribeMenuEventsOwned(
            a_menuNameFilter ? std::string_view{ a_menuNameFilter } : std::string_view{},
            a_callback,
            a_userData,
            context.name ? std::string_view{ context.name } : std::string_view{},
            context.logger);
    }

    HF_Bool HF_CALL UIUnsubscribeMenuEvents(const HF_UIMenuSubscriptionHandle a_handle)
    {
        if (a_handle == HF_INVALID_UI_MENU_SUBSCRIPTION_HANDLE) {
            return HF_FALSE;
        }

        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_UI_MENU_OWNER_UNKNOWN);
            return HF_FALSE;
        }

        const auto context = HolyFramework::ModuleContext::Current();
        std::string actualOwner;
        if (HolyFramework::UIStateService::GetSingleton().UnsubscribeMenuEventsOwned(
                a_handle,
                context.name ? std::string_view{ context.name } : std::string_view{},
                &actualOwner)) {
            return HF_TRUE;
        }

        if (!actualOwner.empty()) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name ? context.name : "<unknown>",
                context.logger,
                HF_ERROR_UI_MENU_OWNER_MISMATCH);
        }
        return HF_FALSE;
    }

    HF_Bool HF_CALL UIGetLoadingMenuState(HF_LoadingMenuStateV1* const a_outState)
    {
        if (!a_outState) {
            return HF_FALSE;
        }
        return HolyFramework::UIStateService::GetSingleton().GetLoadingMenuState(*a_outState) ? HF_TRUE : HF_FALSE;
    }

    HF_UIPolicyHandle HF_CALL UIAcquireLoadingMenuPolicy(const std::uint32_t a_policyFlags)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_UI_LOADING_POLICY_OWNER_UNKNOWN);
            return HF_INVALID_UI_POLICY_HANDLE;
        }

        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::UIStateService::GetSingleton().AcquireLoadingMenuPolicyOwned(
            a_policyFlags,
            context.name ? std::string_view{ context.name } : std::string_view{},
            context.logger);
    }

    HF_Bool HF_CALL UIReleaseLoadingMenuPolicy(const HF_UIPolicyHandle a_handle)
    {
        if (a_handle == HF_INVALID_UI_POLICY_HANDLE) {
            return HF_FALSE;
        }

        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_UI_LOADING_POLICY_OWNER_UNKNOWN);
            return HF_FALSE;
        }

        const auto context = HolyFramework::ModuleContext::Current();
        std::string actualOwner;
        if (HolyFramework::UIStateService::GetSingleton().ReleaseLoadingMenuPolicyOwned(
                a_handle,
                context.name ? std::string_view{ context.name } : std::string_view{},
                &actualOwner)) {
            return HF_TRUE;
        }

        if (!actualOwner.empty()) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name ? context.name : "<unknown>",
                context.logger,
                HF_ERROR_UI_LOADING_POLICY_OWNER_MISMATCH);
        }
        return HF_FALSE;
    }

    std::uint32_t HF_CALL GameGetStateFlags()
    {
        return HolyFramework::GameStateService::GetSingleton().GetStateFlags();
    }

    HF_Bool HF_CALL GameHasState(const std::uint32_t a_requiredFlags)
    {
        return HolyFramework::GameStateService::GetSingleton().HasState(a_requiredFlags) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL GameIsPaused()
    {
        return HolyFramework::GameStateService::GetSingleton().IsPaused() ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL GameIsLoading()
    {
        return HolyFramework::GameStateService::GetSingleton().IsLoading() ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL GameIsInGame()
    {
        return HolyFramework::GameStateService::GetSingleton().IsInGame() ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL GameSettingsExists(const HF_GameSettingSource a_source, const char* const a_name)
    {
        if (!a_name || !*a_name) {
            return HF_FALSE;
        }
        return HolyFramework::GameSettingsManager::GetSingleton().Exists(a_source, a_name) ? HF_TRUE : HF_FALSE;
    }

    HF_GameSettingType HF_CALL GameSettingsGetType(const HF_GameSettingSource a_source, const char* const a_name)
    {
        if (!a_name || !*a_name) {
            return HF_GAME_SETTING_TYPE_UNKNOWN;
        }
        return HolyFramework::GameSettingsManager::GetSingleton().GetType(a_source, a_name);
    }

    HF_Bool HF_CALL GameSettingsGetBool(const HF_GameSettingSource a_source, const char* const a_name, HF_Bool* const a_outValue)
    {
        if (!a_name || !*a_name || !a_outValue) {
            return HF_FALSE;
        }
        bool value = false;
        if (!HolyFramework::GameSettingsManager::GetSingleton().GetBool(a_source, a_name, value)) {
            return HF_FALSE;
        }
        *a_outValue = value ? HF_TRUE : HF_FALSE;
        return HF_TRUE;
    }

    HF_Bool HF_CALL GameSettingsGetInt32(const HF_GameSettingSource a_source, const char* const a_name, std::int32_t* const a_outValue)
    {
        if (!a_name || !*a_name || !a_outValue) {
            return HF_FALSE;
        }
        return HolyFramework::GameSettingsManager::GetSingleton().GetInt32(a_source, a_name, *a_outValue) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL GameSettingsGetUInt32(const HF_GameSettingSource a_source, const char* const a_name, std::uint32_t* const a_outValue)
    {
        if (!a_name || !*a_name || !a_outValue) {
            return HF_FALSE;
        }
        return HolyFramework::GameSettingsManager::GetSingleton().GetUInt32(a_source, a_name, *a_outValue) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL GameSettingsGetFloat(const HF_GameSettingSource a_source, const char* const a_name, float* const a_outValue)
    {
        if (!a_name || !*a_name || !a_outValue) {
            return HF_FALSE;
        }
        return HolyFramework::GameSettingsManager::GetSingleton().GetFloat(a_source, a_name, *a_outValue) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL GameSettingsGetString(
        const HF_GameSettingSource a_source,
        const char* const a_name,
        char* const a_buffer,
        const std::uint32_t a_bufferSize)
    {
        if (!a_name || !*a_name || !a_buffer || a_bufferSize == 0) {
            return HF_FALSE;
        }
        std::string value;
        if (!HolyFramework::GameSettingsManager::GetSingleton().GetString(a_source, a_name, value)) {
            a_buffer[0] = '\0';
            return HF_FALSE;
        }
        std::snprintf(a_buffer, a_bufferSize, "%s", value.c_str());
        return HF_TRUE;
    }

    template <class Fn>
    HF_Bool GameSettingsWriteOwned(const void* const a_callerAddress, Fn&& a_fn)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(a_callerAddress, owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_GAME_SETTING_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        return a_fn() ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL GameSettingsSetBool(const HF_GameSettingSource a_source, const char* const a_name, const HF_Bool a_value)
    {
        if (!a_name || !*a_name) {
            return HF_FALSE;
        }
        const void* const caller = HF_CAPTURE_CALLER_ADDRESS();
        return GameSettingsWriteOwned(caller, [&] {
            return HolyFramework::GameSettingsManager::GetSingleton().SetBool(a_source, a_name, a_value == HF_TRUE);
        });
    }

    HF_Bool HF_CALL GameSettingsSetInt32(const HF_GameSettingSource a_source, const char* const a_name, const std::int32_t a_value)
    {
        if (!a_name || !*a_name) {
            return HF_FALSE;
        }
        const void* const caller = HF_CAPTURE_CALLER_ADDRESS();
        return GameSettingsWriteOwned(caller, [&] {
            return HolyFramework::GameSettingsManager::GetSingleton().SetInt32(a_source, a_name, a_value);
        });
    }

    HF_Bool HF_CALL GameSettingsSetUInt32(const HF_GameSettingSource a_source, const char* const a_name, const std::uint32_t a_value)
    {
        if (!a_name || !*a_name) {
            return HF_FALSE;
        }
        const void* const caller = HF_CAPTURE_CALLER_ADDRESS();
        return GameSettingsWriteOwned(caller, [&] {
            return HolyFramework::GameSettingsManager::GetSingleton().SetUInt32(a_source, a_name, a_value);
        });
    }

    HF_Bool HF_CALL GameSettingsSetFloat(const HF_GameSettingSource a_source, const char* const a_name, const float a_value)
    {
        if (!a_name || !*a_name) {
            return HF_FALSE;
        }
        const void* const caller = HF_CAPTURE_CALLER_ADDRESS();
        return GameSettingsWriteOwned(caller, [&] {
            return HolyFramework::GameSettingsManager::GetSingleton().SetFloat(a_source, a_name, a_value);
        });
    }

    HF_Bool HF_CALL GameSettingsRelease(
        const HF_GameSettingSource a_source,
        const char* const a_name,
        const HF_Bool a_restoreOriginal)
    {
        if (!a_name || !*a_name) {
            return HF_FALSE;
        }
        const void* const caller = HF_CAPTURE_CALLER_ADDRESS();
        return GameSettingsWriteOwned(caller, [&] {
            return HolyFramework::GameSettingsManager::GetSingleton().Release(a_source, a_name, a_restoreOriginal == HF_TRUE);
        });
    }

    HF_FormHandle HF_CALL FormsLookupByID(const std::uint32_t a_formID)
    {
        return HolyFramework::FormService::GetSingleton().LookupByID(a_formID);
    }

    HF_FormHandle HF_CALL FormsLookupByEditorID(const char* const a_editorID)
    {
        if (!a_editorID || !*a_editorID) {
            return HF_INVALID_FORM_HANDLE;
        }
        return HolyFramework::FormService::GetSingleton().LookupByEditorID(a_editorID);
    }

    HF_Bool HF_CALL FormsIsValid(const HF_FormHandle a_handle)
    {
        return HolyFramework::FormService::GetSingleton().IsValid(a_handle) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL FormsGetInfo(const HF_FormHandle a_handle, HF_FormInfoV1* const a_outInfo)
    {
        if (!a_outInfo) {
            return HF_FALSE;
        }
        return HolyFramework::FormService::GetSingleton().GetInfo(a_handle, *a_outInfo) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL FormsIsType(const HF_FormHandle a_handle, const char* const a_typeCode)
    {
        if (!a_typeCode || !*a_typeCode) {
            return HF_FALSE;
        }
        return HolyFramework::FormService::GetSingleton().IsType(a_handle, a_typeCode) ? HF_TRUE : HF_FALSE;
    }


    HF_Bool HF_CALL ReferencesIsReference(const HF_FormHandle a_handle)
    {
        return HolyFramework::ReferenceActorService::GetSingleton().IsReference(a_handle) ? HF_TRUE : HF_FALSE;
    }

    std::uint32_t HF_CALL ReferencesGetStateFlags(const HF_FormHandle a_handle)
    {
        return HolyFramework::ReferenceActorService::GetSingleton().GetReferenceStateFlags(a_handle);
    }

    HF_Bool HF_CALL ReferencesGetPosition(const HF_FormHandle a_handle, HF_Vector3* const a_outPosition)
    {
        return a_outPosition && HolyFramework::ReferenceActorService::GetSingleton().GetPosition(a_handle, *a_outPosition) ? HF_TRUE : HF_FALSE;
    }

    HF_FormHandle HF_CALL ReferencesGetBaseForm(const HF_FormHandle a_handle)
    {
        return HolyFramework::ReferenceActorService::GetSingleton().GetBaseForm(a_handle);
    }

    HF_FormHandle HF_CALL ReferencesGetParentCell(const HF_FormHandle a_handle)
    {
        return HolyFramework::ReferenceActorService::GetSingleton().GetParentCell(a_handle);
    }

    HF_Bool HF_CALL ActorsIsActor(const HF_FormHandle a_handle)
    {
        return HolyFramework::ReferenceActorService::GetSingleton().IsActor(a_handle) ? HF_TRUE : HF_FALSE;
    }

    std::uint32_t HF_CALL ActorsGetStateFlags(const HF_FormHandle a_handle)
    {
        return HolyFramework::ReferenceActorService::GetSingleton().GetActorStateFlags(a_handle);
    }

    HF_FormHandle HF_CALL ActorsGetBaseActor(const HF_FormHandle a_handle)
    {
        return HolyFramework::ReferenceActorService::GetSingleton().GetBaseActor(a_handle);
    }

    HF_Bool HF_CALL ActorsGetActorValue(const HF_FormHandle a_handle, const HF_ActorValue a_value, float* const a_outValue)
    {
        return a_outValue && HolyFramework::ReferenceActorService::GetSingleton().GetActorValue(a_handle, a_value, *a_outValue) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL ActorsGetBaseActorValue(const HF_FormHandle a_handle, const HF_ActorValue a_value, float* const a_outValue)
    {
        return a_outValue && HolyFramework::ReferenceActorService::GetSingleton().GetBaseActorValue(a_handle, a_value, *a_outValue) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL ActorsGetPermanentActorValue(const HF_FormHandle a_handle, const HF_ActorValue a_value, float* const a_outValue)
    {
        return a_outValue && HolyFramework::ReferenceActorService::GetSingleton().GetPermanentActorValue(a_handle, a_value, *a_outValue) ? HF_TRUE : HF_FALSE;
    }

    HF_FormHandle HF_CALL ActorsGetActorValueForm(const HF_ActorValue a_value)
    {
        return HolyFramework::ReferenceActorService::GetSingleton().GetActorValueForm(a_value);
    }

    HF_Bool HF_CALL PlayerIsAvailable()
    {
        return HolyFramework::ReferenceActorService::GetSingleton().IsPlayerAvailable() ? HF_TRUE : HF_FALSE;
    }

    HF_FormHandle HF_CALL PlayerGetHandle()
    {
        return HolyFramework::ReferenceActorService::GetSingleton().GetPlayerHandle();
    }

    HF_Bool HF_CALL PlayerIsGodMode()
    {
        return HolyFramework::ReferenceActorService::GetSingleton().IsPlayerGodMode() ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL PlayerIsImmortal()
    {
        return HolyFramework::ReferenceActorService::GetSingleton().IsPlayerImmortal() ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL PlayerIsPipboyLightOn()
    {
        return HolyFramework::ReferenceActorService::GetSingleton().IsPlayerPipboyLightOn() ? HF_TRUE : HF_FALSE;
    }


    HF_Bool HF_CALL ReferencesGetDisplayName(
        const HF_FormHandle a_handle,
        char* const a_buffer,
        const std::uint32_t a_bufferSize)
    {
        if (!a_buffer || a_bufferSize == 0) {
            return HF_FALSE;
        }
        a_buffer[0] = '\0';
        std::string value;
        if (!HolyFramework::ReferenceActorService::GetSingleton().GetDisplayName(a_handle, value)) {
            return HF_FALSE;
        }
        std::snprintf(a_buffer, a_bufferSize, "%s", value.c_str());
        return HF_TRUE;
    }

    HF_Bool HF_CALL ActorsIsActorValueForm(const HF_FormHandle a_actorValue)
    {
        return HolyFramework::ReferenceActorService::GetSingleton().IsActorValueForm(a_actorValue) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL ActorsGetActorValueByForm(
        const HF_FormHandle a_actor,
        const HF_FormHandle a_actorValue,
        float* const a_outValue)
    {
        return a_outValue && HolyFramework::ReferenceActorService::GetSingleton().GetActorValueByForm(a_actor, a_actorValue, *a_outValue) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL ActorsGetBaseActorValueByForm(
        const HF_FormHandle a_actor,
        const HF_FormHandle a_actorValue,
        float* const a_outValue)
    {
        return a_outValue && HolyFramework::ReferenceActorService::GetSingleton().GetBaseActorValueByForm(a_actor, a_actorValue, *a_outValue) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL ActorsGetPermanentActorValueByForm(
        const HF_FormHandle a_actor,
        const HF_FormHandle a_actorValue,
        float* const a_outValue)
    {
        return a_outValue && HolyFramework::ReferenceActorService::GetSingleton().GetPermanentActorValueByForm(a_actor, a_actorValue, *a_outValue) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL ActorsGetModifierByForm(
        const HF_FormHandle a_actor,
        const HF_FormHandle a_actorValue,
        const HF_ActorValueModifier a_modifier,
        float* const a_outValue)
    {
        return a_outValue && HolyFramework::ReferenceActorService::GetSingleton().GetModifierByForm(a_actor, a_actorValue, a_modifier, *a_outValue) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL ActorsModifyActorValueByForm(
        const HF_FormHandle a_actor,
        const HF_FormHandle a_actorValue,
        const HF_ActorValueModifier a_modifier,
        const float a_delta)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_ACTOR_VALUE_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        return HolyFramework::ReferenceActorService::GetSingleton().ModifyActorValueByForm(
                   a_actor, a_actorValue, a_modifier, a_delta) ? HF_TRUE : HF_FALSE;
    }

    HF_ActorValueAdjustmentHandle HF_CALL ActorsAcquireAdjustment(
        const HF_FormHandle a_actor,
        const HF_FormHandle a_actorValue,
        const HF_ActorValueModifier a_modifier,
        const float a_amount)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_ACTOR_VALUE_OWNER_UNKNOWN);
            return HF_INVALID_ACTOR_VALUE_ADJUSTMENT_HANDLE;
        }
        return HolyFramework::ReferenceActorService::GetSingleton().AcquireAdjustment(
            a_actor, a_actorValue, a_modifier, a_amount);
    }

    HF_Bool HF_CALL ActorsUpdateAdjustment(
        const HF_ActorValueAdjustmentHandle a_handle,
        const float a_amount)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_ACTOR_VALUE_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        return HolyFramework::ReferenceActorService::GetSingleton().UpdateAdjustment(a_handle, a_amount) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL ActorsReleaseAdjustment(const HF_ActorValueAdjustmentHandle a_handle)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_ACTOR_VALUE_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        return HolyFramework::ReferenceActorService::GetSingleton().ReleaseAdjustment(a_handle) ? HF_TRUE : HF_FALSE;
    }

    HF_FormHandle HF_CALL PlayerGetDialogueTarget()
    {
        return HolyFramework::ReferenceActorService::GetSingleton().GetPlayerDialogueTarget();
    }


    std::uint32_t HF_CALL RuntimeGetStateFlags()
    {
        return HolyFramework::RuntimeState::GetSingleton().GetFlags();
    }

    std::uint64_t HF_CALL RuntimeGetSessionGeneration()
    {
        return HolyFramework::RuntimeState::GetSingleton().GetSessionGeneration();
    }

    HF_Bool HF_CALL RuntimeHasState(const std::uint32_t a_requiredFlags)
    {
        return HolyFramework::RuntimeState::GetSingleton().HasState(a_requiredFlags) ? HF_TRUE : HF_FALSE;
    }

    std::uint32_t HF_CALL ModulesGetCount()
    {
        return HolyFramework::ModuleLoader::GetSingleton().GetLoadedCount();
    }

    std::uint32_t HF_CALL ModulesGetHealthyCount()
    {
        return HolyFramework::ModuleLoader::GetSingleton().GetHealthyCount();
    }

    std::uint32_t HF_CALL ModulesGetDegradedCount()
    {
        return HolyFramework::ModuleLoader::GetSingleton().GetDegradedCount();
    }

    HF_Bool HF_CALL ModulesGetByIndex(const std::uint32_t a_index, HF_ModuleRecordV2* const a_outRecord)
    {
        if (!a_outRecord) {
            return HF_FALSE;
        }
        return HolyFramework::ModuleLoader::GetSingleton().GetRecordByIndex(a_index, *a_outRecord) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL ModulesFindByName(const char* const a_name, HF_ModuleRecordV2* const a_outRecord)
    {
        if (!a_name || !*a_name || !a_outRecord) {
            return HF_FALSE;
        }
        return HolyFramework::ModuleLoader::GetSingleton().FindRecordByName(a_name, *a_outRecord) ? HF_TRUE : HF_FALSE;
    }


    HF_TaskHandle HF_CALL TasksQueue(
        const HF_TaskQueue a_queue,
        const HF_TaskCallback a_callback,
        void* const a_userData,
        const std::uint32_t a_flags)
    {
        return HolyFramework::TaskScheduler::GetSingleton().Queue(
            a_queue,
            a_callback,
            a_userData,
            a_flags);
    }

    HF_TaskHandle HF_CALL TasksQueueDelayed(
        const HF_TaskQueue a_queue,
        const std::uint32_t a_delayMs,
        const HF_TaskCallback a_callback,
        void* const a_userData,
        const std::uint32_t a_flags)
    {
        return HolyFramework::TaskScheduler::GetSingleton().QueueDelayed(
            a_queue,
            a_delayMs,
            a_callback,
            a_userData,
            a_flags);
    }

    HF_Bool HF_CALL TasksCancel(const HF_TaskHandle a_handle)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_TASK_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        std::string actualOwner;
        if (HolyFramework::TaskScheduler::GetSingleton().CancelOwned(a_handle, context.name, &actualOwner)) {
            return HF_TRUE;
        }
        if (!actualOwner.empty()) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name, context.logger, HF_ERROR_TASK_OWNER_MISMATCH);
        }
        return HF_FALSE;
    }

    std::uint32_t HF_CALL TasksGetPendingCount()
    {
        return HolyFramework::TaskScheduler::GetSingleton().GetPendingCount();
    }



    HF_Bool HF_CALL MemoryResolveID(
        const std::uint64_t a_id,
        const std::int64_t a_offset,
        HF_Address* const a_outAddress)
    {
        if (!a_outAddress) {
            return HF_FALSE;
        }
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope);
        return HolyFramework::MemoryManager::GetSingleton().ResolveID(a_id, a_offset, *a_outAddress) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL MemoryQueryRegion(
        const HF_Address a_address,
        HF_MemoryRegionV1* const a_outRegion)
    {
        if (!a_outRegion) {
            return HF_FALSE;
        }
        return HolyFramework::MemoryManager::GetSingleton().QueryRegion(a_address, *a_outRegion) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL MemoryRead(
        const HF_Address a_address,
        void* const a_outData,
        const std::uint32_t a_size)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope);
        return HolyFramework::MemoryManager::GetSingleton().Read(a_address, a_outData, a_size) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL MemoryCompare(
        const HF_Address a_address,
        const void* const a_expected,
        const std::uint32_t a_size)
    {
        return HolyFramework::MemoryManager::GetSingleton().Compare(a_address, a_expected, a_size) ? HF_TRUE : HF_FALSE;
    }

    HF_PatchHandle HF_CALL MemoryApplyPatch(
        const HF_Address a_address,
        const void* const a_expected,
        const void* const a_replacement,
        const std::uint32_t a_size,
        const char* const a_label)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope);
        return HolyFramework::MemoryManager::GetSingleton().ApplyPatch(
            a_address, a_expected, a_replacement, a_size, a_label);
    }

    HF_Bool HF_CALL MemoryRestorePatch(const HF_PatchHandle a_handle)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope);
        return HolyFramework::MemoryManager::GetSingleton().RestorePatch(a_handle) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL MemoryVerifyPatch(
        const HF_PatchHandle a_handle,
        HF_PatchStatus* const a_outStatus)
    {
        if (!a_outStatus) {
            return HF_FALSE;
        }
        return HolyFramework::MemoryManager::GetSingleton().VerifyPatch(a_handle, *a_outStatus) ? HF_TRUE : HF_FALSE;
    }

    std::uint32_t HF_CALL MemoryGetPatchCount()
    {
        return HolyFramework::MemoryManager::GetSingleton().GetPatchCount();
    }

    HF_Bool HF_CALL MemoryGetPatchByIndex(
        const std::uint32_t a_index,
        HF_PatchRecordV1* const a_outRecord)
    {
        if (!a_outRecord) {
            return HF_FALSE;
        }
        return HolyFramework::MemoryManager::GetSingleton().GetPatchByIndex(a_index, *a_outRecord) ? HF_TRUE : HF_FALSE;
    }

    std::uint32_t HF_CALL MemoryAuditPatches()
    {
        return HolyFramework::MemoryManager::GetSingleton().AuditPatches();
    }



    std::uint32_t HF_CALL HooksGetTrampolineCapacity()
    {
        return HolyFramework::HookManager::GetSingleton().GetTrampolineCapacity();
    }

    std::uint32_t HF_CALL HooksGetTrampolineFreeSize()
    {
        return HolyFramework::HookManager::GetSingleton().GetTrampolineFreeSize();
    }

    HF_CodeBlockHandle HF_CALL HooksAllocateCode(
        const std::uint32_t a_size,
        const char* const a_label,
        HF_Address* const a_outAddress)
    {
        if (!a_outAddress) {
            return HF_INVALID_CODE_BLOCK_HANDLE;
        }
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope);
        return HolyFramework::HookManager::GetSingleton().AllocateCode(a_size, a_label, *a_outAddress);
    }

    HF_Bool HF_CALL HooksWriteCode(
        const HF_CodeBlockHandle a_handle,
        const std::uint32_t a_offset,
        const void* const a_data,
        const std::uint32_t a_size)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope);
        return HolyFramework::HookManager::GetSingleton().WriteCode(a_handle, a_offset, a_data, a_size) ? HF_TRUE : HF_FALSE;
    }

    HF_HookHandle HF_CALL HooksInstall(
        const HF_HookKind a_kind,
        const HF_Address a_site,
        const void* const a_expected,
        const std::uint32_t a_expectedSize,
        const HF_Address a_destination,
        const char* const a_label,
        HF_Address* const a_outOriginalTarget)
    {
        if (!a_outOriginalTarget) {
            return HF_INVALID_HOOK_HANDLE;
        }
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope);
        return HolyFramework::HookManager::GetSingleton().Install(
            a_kind, a_site, a_expected, a_expectedSize, a_destination, a_label, *a_outOriginalTarget);
    }

    HF_Bool HF_CALL HooksRestore(const HF_HookHandle a_handle)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope);
        return HolyFramework::HookManager::GetSingleton().Restore(a_handle) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL HooksVerify(const HF_HookHandle a_handle, HF_HookStatus* const a_outStatus)
    {
        if (!a_outStatus) {
            return HF_FALSE;
        }
        return HolyFramework::HookManager::GetSingleton().Verify(a_handle, *a_outStatus) ? HF_TRUE : HF_FALSE;
    }

    std::uint32_t HF_CALL HooksGetCount()
    {
        return HolyFramework::HookManager::GetSingleton().GetCount();
    }

    HF_Bool HF_CALL HooksGetByIndex(const std::uint32_t a_index, HF_HookRecordV1* const a_outRecord)
    {
        if (!a_outRecord) {
            return HF_FALSE;
        }
        return HolyFramework::HookManager::GetSingleton().GetByIndex(a_index, *a_outRecord) ? HF_TRUE : HF_FALSE;
    }

    std::uint32_t HF_CALL HooksAudit()
    {
        return HolyFramework::HookManager::GetSingleton().Audit();
    }


    HF_Bool HF_CALL CapabilitiesPublish(const char* const a_name, const std::uint32_t a_version)
    {
        if (!a_name || !*a_name) {
            return HF_FALSE;
        }
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope);
        return HolyFramework::ResourceRegistry::GetSingleton().PublishCapability(a_name, a_version) ? HF_TRUE : HF_FALSE;
    }

    std::uint32_t HF_CALL CapabilitiesGetCount()
    {
        return HolyFramework::ResourceRegistry::GetSingleton().GetCapabilityCount();
    }

    HF_Bool HF_CALL CapabilitiesGetByIndex(const std::uint32_t a_index, HF_CapabilityRecordV1* const a_outRecord)
    {
        if (!a_outRecord) {
            return HF_FALSE;
        }
        return HolyFramework::ResourceRegistry::GetSingleton().GetCapabilityByIndex(a_index, *a_outRecord) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL CapabilitiesFindFirst(const char* const a_name, HF_CapabilityRecordV1* const a_outRecord)
    {
        if (!a_name || !*a_name || !a_outRecord) {
            return HF_FALSE;
        }
        return HolyFramework::ResourceRegistry::GetSingleton().FindCapability(a_name, *a_outRecord) ? HF_TRUE : HF_FALSE;
    }

    HF_ResourceHandle HF_CALL ResourcesClaim(
        const char* const a_name,
        const HF_ResourceAccess a_access,
        const std::uint32_t a_flags,
        const char* const a_label)
    {
        if (!a_name || !*a_name) {
            return HF_INVALID_RESOURCE_HANDLE;
        }
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope);
        return HolyFramework::ResourceRegistry::GetSingleton().ClaimResource(
            a_name,
            a_access,
            a_flags,
            a_label ? std::string_view{ a_label } : std::string_view{});
    }

    HF_Bool HF_CALL ResourcesRelease(const HF_ResourceHandle a_handle)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope);
        return HolyFramework::ResourceRegistry::GetSingleton().ReleaseResource(a_handle) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL ResourcesIsAvailable(const char* const a_name, const HF_ResourceAccess a_access)
    {
        if (!a_name || !*a_name) {
            return HF_FALSE;
        }
        return HolyFramework::ResourceRegistry::GetSingleton().IsResourceAvailable(a_name, a_access) ? HF_TRUE : HF_FALSE;
    }

    std::uint32_t HF_CALL ResourcesGetCount()
    {
        return HolyFramework::ResourceRegistry::GetSingleton().GetResourceCount();
    }

    HF_Bool HF_CALL ResourcesGetByIndex(const std::uint32_t a_index, HF_ResourceRecordV1* const a_outRecord)
    {
        if (!a_outRecord) {
            return HF_FALSE;
        }
        return HolyFramework::ResourceRegistry::GetSingleton().GetResourceByIndex(a_index, *a_outRecord) ? HF_TRUE : HF_FALSE;
    }

    HF_PerfSampleHandle HF_CALL PerformanceBeginSample(
        const char* const a_label,
        const std::uint32_t a_warningThresholdMicroseconds)
    {
        if (!a_label || !*a_label) {
            return HF_INVALID_PERF_SAMPLE_HANDLE;
        }
        const void* caller = nullptr;
#if defined(_MSC_VER)
        caller = _ReturnAddress();
#endif
        return HolyFramework::PerformanceMonitor::GetSingleton().BeginSample(
            a_label,
            a_warningThresholdMicroseconds,
            caller);
    }

    HF_Bool HF_CALL PerformanceEndSample(const HF_PerfSampleHandle a_handle)
    {
        const void* caller = nullptr;
#if defined(_MSC_VER)
        caller = _ReturnAddress();
#endif
        return HolyFramework::PerformanceMonitor::GetSingleton().EndSample(a_handle, caller) ? HF_TRUE : HF_FALSE;
    }

    std::uint32_t HF_CALL PerformanceGetCount()
    {
        return HolyFramework::PerformanceMonitor::GetSingleton().GetCount();
    }

    HF_Bool HF_CALL PerformanceGetByIndex(const std::uint32_t a_index, HF_PerformanceRecordV1* const a_outRecord)
    {
        if (!a_outRecord) {
            return HF_FALSE;
        }
        return HolyFramework::PerformanceMonitor::GetSingleton().GetByIndex(a_index, *a_outRecord) ? HF_TRUE : HF_FALSE;
    }


    HF_Bool HF_CALL GraphicsIsAvailable()
    {
        return HolyFramework::GraphicsService::GetSingleton().IsAvailable() ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL GraphicsGetState(HF_GraphicsStateV1* const a_outState)
    {
        if (!a_outState) {
            return HF_FALSE;
        }
        return HolyFramework::GraphicsService::GetSingleton().GetState(*a_outState) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL GraphicsGetNativeHandles(HF_GraphicsNativeHandlesV1* const a_outHandles)
    {
        if (!a_outHandles) {
            return HF_FALSE;
        }
        return HolyFramework::GraphicsService::GetSingleton().GetNativeHandles(*a_outHandles) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL GameTimeIsAvailable()
    {
        return HolyFramework::GameTimeService::GetSingleton().IsAvailable() ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL GameTimeGetGameHour(float* const a_outHour)
    {
        if (!a_outHour) {
            return HF_FALSE;
        }
        return HolyFramework::GameTimeService::GetSingleton().GetGameHour(*a_outHour) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL GameTimeGetTimeScale(float* const a_outTimeScale)
    {
        if (!a_outTimeScale) {
            return HF_FALSE;
        }
        return HolyFramework::GameTimeService::GetSingleton().GetTimeScale(*a_outTimeScale) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL GameTimeGetState(HF_GameTimeStateV1* const a_outState)
    {
        if (!a_outState) {
            return HF_FALSE;
        }
        return HolyFramework::GameTimeService::GetSingleton().GetState(*a_outState) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL EnvironmentIsAvailable()
    {
        return HolyFramework::EnvironmentService::GetSingleton().IsAvailable() ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL EnvironmentGetState(HF_EnvironmentStateV1* const a_outState)
    {
        if (!a_outState) {
            return HF_FALSE;
        }
        return HolyFramework::EnvironmentService::GetSingleton().GetState(*a_outState) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL EnvironmentGetWeatherInfo(
        const HF_FormHandle a_weather,
        HF_WeatherInfoV1* const a_outInfo)
    {
        if (!a_outInfo) {
            return HF_FALSE;
        }
        return HolyFramework::EnvironmentService::GetSingleton().GetWeatherInfo(a_weather, *a_outInfo) ? HF_TRUE : HF_FALSE;
    }


    HF_Bool HF_CALL LightingIsAvailable()
    {
        return HolyFramework::LightingService::GetSingleton().IsAvailable() ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL LightingGetState(HF_LightingStateV1* const a_outState)
    {
        if (!a_outState) {
            return HF_FALSE;
        }
        return HolyFramework::LightingService::GetSingleton().GetState(*a_outState) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL LightingGetTemplateInfo(
        const HF_FormHandle a_template,
        HF_LightingTemplateInfoV1* const a_outInfo)
    {
        if (!a_outInfo) {
            return HF_FALSE;
        }
        return HolyFramework::LightingService::GetSingleton().GetLightingTemplateInfo(a_template, *a_outInfo) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL LightingGetImageSpaceInfo(
        const HF_FormHandle a_imageSpace,
        HF_ImageSpaceInfoV1* const a_outInfo)
    {
        if (!a_outInfo) {
            return HF_FALSE;
        }
        return HolyFramework::LightingService::GetSingleton().GetImageSpaceInfo(a_imageSpace, *a_outInfo) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL LightingGetWeatherImageSpace(
        const HF_FormHandle a_weather,
        const HF_WeatherTimeSlot a_timeSlot,
        HF_FormHandle* const a_outImageSpace)
    {
        if (!a_outImageSpace) {
            return HF_FALSE;
        }
        return HolyFramework::LightingService::GetSingleton().GetWeatherImageSpace(
                   a_weather, a_timeSlot, *a_outImageSpace) ?
                   HF_TRUE :
                   HF_FALSE;
    }


    HF_Bool HF_CALL RenderPipelineIsStageSupported(const HF_RenderStage a_stage)
    {
        return HolyFramework::RenderPipelineService::GetSingleton().IsStageSupported(a_stage) ? HF_TRUE : HF_FALSE;
    }

    HF_RenderSubscriptionHandle HF_CALL RenderPipelineSubscribe(
        const HF_RenderStage a_stage,
        const std::int32_t a_priority,
        const HF_RenderStageCallback a_callback,
        void* const a_userData)
    {
        if (!a_callback) {
            return HF_INVALID_RENDER_SUBSCRIPTION_HANDLE;
        }

        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HF_LogHandle logger = HF_INVALID_LOG_HANDLE;
            if (!HolyFramework::ModuleLoader::GetSingleton().FindExecutionIdentityByCodeAddress(
                    reinterpret_cast<const void*>(a_callback), owner, logger)) {
                HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                    "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_RENDER_STAGE_OWNER_UNKNOWN);
                return HF_INVALID_RENDER_SUBSCRIPTION_HANDLE;
            }
            const auto current = HolyFramework::ModuleContext::Current();
            scope = std::make_unique<HolyFramework::ModuleContext::Scope>(
                owner.c_str(), logger, current.checkpoint);
        }

        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::RenderPipelineService::GetSingleton().SubscribeOwned(
            a_stage, a_priority, a_callback, a_userData, context.name ? context.name : "", context.logger);
    }

    HF_Bool HF_CALL RenderPipelineUnsubscribe(const HF_RenderSubscriptionHandle a_handle)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_RENDER_STAGE_OWNER_UNKNOWN);
            return HF_FALSE;
        }

        const auto context = HolyFramework::ModuleContext::Current();
        std::string actualOwner;
        if (HolyFramework::RenderPipelineService::GetSingleton().UnsubscribeOwned(
                a_handle, context.name ? context.name : "", &actualOwner)) {
            return HF_TRUE;
        }
        if (!actualOwner.empty()) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name ? context.name : "<unknown>",
                context.logger,
                HF_ERROR_RENDER_STAGE_OWNER_MISMATCH);
        }
        return HF_FALSE;
    }


    HF_Bool HF_CALL PresentationIsAvailable()
    {
        return HolyFramework::PresentationService::GetSingleton().IsAvailable() ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL PresentationGetState(HF_PresentationStateV1* const a_outState)
    {
        if (!a_outState) {
            return HF_FALSE;
        }
        return HolyFramework::PresentationService::GetSingleton().GetState(*a_outState) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL PresentationGetCapabilities(HF_PresentationCapabilitiesV1* const a_outCapabilities)
    {
        if (!a_outCapabilities) {
            return HF_FALSE;
        }
        return HolyFramework::PresentationService::GetSingleton().GetCapabilities(*a_outCapabilities) ? HF_TRUE : HF_FALSE;
    }

    HF_PresentationSubscriptionHandle HF_CALL PresentationSubscribePresent(
        const std::int32_t a_priority,
        const HF_PresentCallback a_callback,
        void* const a_userData)
    {
        if (!a_callback) {
            return HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE;
        }

        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HF_LogHandle logger = HF_INVALID_LOG_HANDLE;
            if (!HolyFramework::ModuleLoader::GetSingleton().FindExecutionIdentityByCodeAddress(
                    reinterpret_cast<const void*>(a_callback), owner, logger)) {
                HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                    "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_PRESENTATION_OWNER_UNKNOWN);
                return HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE;
            }
            const auto current = HolyFramework::ModuleContext::Current();
            scope = std::make_unique<HolyFramework::ModuleContext::Scope>(
                owner.c_str(), logger, current.checkpoint);
        }

        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::PresentationService::GetSingleton().SubscribePresentOwned(
            a_priority,
            a_callback,
            a_userData,
            context.name ? context.name : "",
            context.logger);
    }

    HF_PresentationSubscriptionHandle HF_CALL PresentationSubscribeResizeBuffers(
        const std::int32_t a_priority,
        const HF_ResizeBuffersCallback a_callback,
        void* const a_userData)
    {
        if (!a_callback) {
            return HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE;
        }

        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HF_LogHandle logger = HF_INVALID_LOG_HANDLE;
            if (!HolyFramework::ModuleLoader::GetSingleton().FindExecutionIdentityByCodeAddress(
                    reinterpret_cast<const void*>(a_callback), owner, logger)) {
                HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                    "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_PRESENTATION_OWNER_UNKNOWN);
                return HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE;
            }
            const auto current = HolyFramework::ModuleContext::Current();
            scope = std::make_unique<HolyFramework::ModuleContext::Scope>(
                owner.c_str(), logger, current.checkpoint);
        }

        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::PresentationService::GetSingleton().SubscribeResizeBuffersOwned(
            a_priority,
            a_callback,
            a_userData,
            context.name ? context.name : "",
            context.logger);
    }

    HF_PresentationSubscriptionHandle HF_CALL PresentationSubscribeSwapChainCreate(
        const std::int32_t a_priority,
        const HF_SwapChainCreateCallback a_callback,
        void* const a_userData)
    {
        if (!a_callback) {
            return HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE;
        }

        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HF_LogHandle logger = HF_INVALID_LOG_HANDLE;
            if (!HolyFramework::ModuleLoader::GetSingleton().FindExecutionIdentityByCodeAddress(
                    reinterpret_cast<const void*>(a_callback), owner, logger)) {
                HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                    "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_PRESENTATION_OWNER_UNKNOWN);
                return HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE;
            }
            const auto current = HolyFramework::ModuleContext::Current();
            scope = std::make_unique<HolyFramework::ModuleContext::Scope>(
                owner.c_str(), logger, current.checkpoint);
        }

        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::PresentationService::GetSingleton().SubscribeSwapChainCreateOwned(
            a_priority,
            a_callback,
            a_userData,
            context.name ? context.name : "",
            context.logger);
    }

    HF_Bool HF_CALL PresentationUnsubscribe(const HF_PresentationSubscriptionHandle a_handle)
    {
        if (a_handle == HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE) {
            return HF_FALSE;
        }

        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_PRESENTATION_OWNER_UNKNOWN);
            return HF_FALSE;
        }

        const auto context = HolyFramework::ModuleContext::Current();
        std::string actualOwner;
        if (HolyFramework::PresentationService::GetSingleton().UnsubscribeOwned(
                a_handle,
                context.name ? context.name : "",
                &actualOwner)) {
            return HF_TRUE;
        }
        if (!actualOwner.empty()) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name ? context.name : "<unknown>",
                context.logger,
                HF_ERROR_PRESENTATION_OWNER_MISMATCH);
        }
        return HF_FALSE;
    }


    HF_Bool HF_CALL FrameTimingIsAvailable()
    {
        return HolyFramework::FrameTimingService::GetSingleton().IsAvailable() ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL FrameTimingGetState(HF_FrameTimingStateV1* const a_outState)
    {
        if (!a_outState) {
            return HF_FALSE;
        }
        return HolyFramework::FrameTimingService::GetSingleton().GetState(*a_outState) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL FrameTimingGetDeltaSeconds(float* const a_outDeltaSeconds)
    {
        if (!a_outDeltaSeconds) {
            return HF_FALSE;
        }
        return HolyFramework::FrameTimingService::GetSingleton().GetDeltaSeconds(*a_outDeltaSeconds) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL FrameTimingGetFramesPerSecond(float* const a_outFramesPerSecond)
    {
        if (!a_outFramesPerSecond) {
            return HF_FALSE;
        }
        return HolyFramework::FrameTimingService::GetSingleton().GetFramesPerSecond(*a_outFramesPerSecond) ? HF_TRUE : HF_FALSE;
    }


    HF_Bool HF_CALL FramePacingIsAvailable()
    {
        return HolyFramework::FramePacingService::GetSingleton().IsAvailable() ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL FramePacingGetState(HF_FramePacingStateV1* const a_outState)
    {
        if (!a_outState) {
            return HF_FALSE;
        }
        return HolyFramework::FramePacingService::GetSingleton().GetState(*a_outState) ? HF_TRUE : HF_FALSE;
    }

    HF_FrameLimitHandle HF_CALL FramePacingAcquireLimit(const std::uint32_t a_targetFPS)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_FRAME_PACING_OWNER_UNKNOWN);
            return HF_INVALID_FRAME_LIMIT_HANDLE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::FramePacingService::GetSingleton().AcquireLimitOwned(
            a_targetFPS, context.name, context.logger);
    }

    HF_Bool HF_CALL FramePacingUpdateLimit(
        const HF_FrameLimitHandle a_handle,
        const std::uint32_t a_targetFPS)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_FRAME_PACING_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        if (a_targetFPS != 0 &&
            (a_targetFPS < HF_FRAME_PACING_MIN_FPS || a_targetFPS > HF_FRAME_PACING_MAX_FPS)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name, context.logger, HF_ERROR_FRAME_PACING_INVALID_REQUEST);
            return HF_FALSE;
        }
        std::string actualOwner;
        if (HolyFramework::FramePacingService::GetSingleton().UpdateLimitOwned(
                a_handle, a_targetFPS, context.name, &actualOwner)) {
            return HF_TRUE;
        }
        if (!actualOwner.empty()) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name, context.logger, HF_ERROR_FRAME_PACING_OWNER_MISMATCH);
        } else {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name, context.logger, HF_ERROR_FRAME_PACING_INVALID_REQUEST);
        }
        return HF_FALSE;
    }

    HF_Bool HF_CALL FramePacingReleaseLimit(const HF_FrameLimitHandle a_handle)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_FRAME_PACING_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        std::string actualOwner;
        if (HolyFramework::FramePacingService::GetSingleton().ReleaseLimitOwned(
                a_handle, context.name, &actualOwner)) {
            return HF_TRUE;
        }
        if (!actualOwner.empty()) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name, context.logger, HF_ERROR_FRAME_PACING_OWNER_MISMATCH);
        } else {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name, context.logger, HF_ERROR_FRAME_PACING_INVALID_REQUEST);
        }
        return HF_FALSE;
    }


    HF_Bool HF_CALL WindowIsAvailable()
    {
        return HolyFramework::WindowService::GetSingleton().IsAvailable() ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL WindowGetState(HF_WindowStateV1* const a_outState)
    {
        if (!a_outState) {
            return HF_FALSE;
        }
        return HolyFramework::WindowService::GetSingleton().GetState(*a_outState) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL WindowIsForeground()
    {
        return HolyFramework::WindowService::GetSingleton().IsForeground() ? HF_TRUE : HF_FALSE;
    }

    HF_CursorClipHandle HF_CALL WindowAcquireCursorClip()
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_WINDOW_OWNER_UNKNOWN);
            return HF_INVALID_CURSOR_CLIP_HANDLE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        const auto handle = HolyFramework::WindowService::GetSingleton().AcquireCursorClipOwned(
            context.name, context.logger);
        if (handle == HF_INVALID_CURSOR_CLIP_HANDLE) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name, context.logger, HF_ERROR_WINDOW_INVALID_REQUEST);
        }
        return handle;
    }

    HF_Bool HF_CALL WindowReleaseCursorClip(const HF_CursorClipHandle a_handle)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_WINDOW_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        std::string actualOwner;
        if (HolyFramework::WindowService::GetSingleton().ReleaseCursorClipOwned(
                a_handle, context.name, &actualOwner)) {
            return HF_TRUE;
        }
        if (!actualOwner.empty()) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name, context.logger, HF_ERROR_WINDOW_OWNER_MISMATCH);
        } else {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name, context.logger, HF_ERROR_WINDOW_INVALID_REQUEST);
        }
        return HF_FALSE;
    }


    HF_Bool HF_CALL DisplayIsAvailable()
    {
        return HolyFramework::DisplayService::GetSingleton().IsAvailable() ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL DisplayGetState(HF_DisplayStateV1* const a_outState)
    {
        if (!a_outState) {
            return HF_FALSE;
        }
        return HolyFramework::DisplayService::GetSingleton().GetState(*a_outState) ? HF_TRUE : HF_FALSE;
    }


    HF_Bool HF_CALL PresentationPolicyIsAvailable()
    {
        return HolyFramework::PresentationPolicyService::GetSingleton().IsAvailable() ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL PresentationPolicyGetState(HF_PresentationPolicyStateV1* const a_outState)
    {
        if (!a_outState) {
            return HF_FALSE;
        }
        return HolyFramework::PresentationPolicyService::GetSingleton().GetState(*a_outState) ? HF_TRUE : HF_FALSE;
    }

    HF_PresentationPolicyHandle HF_CALL PresentationPolicyAcquire(
        const HF_PresentationPolicyRequestV1* const a_request)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_PRESENTATION_POLICY_OWNER_UNKNOWN);
            return HF_INVALID_PRESENTATION_POLICY_HANDLE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        if (!a_request) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name, context.logger, HF_ERROR_PRESENTATION_POLICY_INVALID_REQUEST);
            return HF_INVALID_PRESENTATION_POLICY_HANDLE;
        }
        const auto handle = HolyFramework::PresentationPolicyService::GetSingleton().AcquirePolicyOwned(
            *a_request, context.name, context.logger);
        if (handle == HF_INVALID_PRESENTATION_POLICY_HANDLE) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name, context.logger, HF_ERROR_PRESENTATION_POLICY_INVALID_REQUEST);
        }
        return handle;
    }

    HF_Bool HF_CALL PresentationPolicyUpdate(
        const HF_PresentationPolicyHandle a_handle,
        const HF_PresentationPolicyRequestV1* const a_request)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_PRESENTATION_POLICY_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        if (!a_request) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name, context.logger, HF_ERROR_PRESENTATION_POLICY_INVALID_REQUEST);
            return HF_FALSE;
        }
        std::string actualOwner;
        if (HolyFramework::PresentationPolicyService::GetSingleton().UpdatePolicyOwned(
                a_handle, *a_request, context.name, &actualOwner)) {
            return HF_TRUE;
        }
        if (!actualOwner.empty()) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name, context.logger, HF_ERROR_PRESENTATION_POLICY_OWNER_MISMATCH);
        } else {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name, context.logger, HF_ERROR_PRESENTATION_POLICY_INVALID_REQUEST);
        }
        return HF_FALSE;
    }

    HF_Bool HF_CALL PresentationPolicyRelease(const HF_PresentationPolicyHandle a_handle)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_PRESENTATION_POLICY_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        std::string actualOwner;
        if (HolyFramework::PresentationPolicyService::GetSingleton().ReleasePolicyOwned(
                a_handle, context.name, &actualOwner)) {
            return HF_TRUE;
        }
        if (!actualOwner.empty()) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name, context.logger, HF_ERROR_PRESENTATION_POLICY_OWNER_MISMATCH);
        } else {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name, context.logger, HF_ERROR_PRESENTATION_POLICY_INVALID_REQUEST);
        }
        return HF_FALSE;
    }


    HF_Bool HF_CALL StateFPSIsAvailable()
    {
        return HolyFramework::StateFPSService::GetSingleton().IsAvailable() ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL StateFPSGetState(HF_StateFPSStateV1* const a_outState)
    {
        if (!a_outState) return HF_FALSE;
        return HolyFramework::StateFPSService::GetSingleton().GetState(*a_outState) ? HF_TRUE : HF_FALSE;
    }

    HF_StateFPSPolicyHandle HF_CALL StateFPSAcquire(const HF_StateFPSPolicyV1* const a_policy)
    {
        std::string owner; std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule("<unknown>", HF_INVALID_LOG_HANDLE,
                HF_ERROR_STATE_FPS_OWNER_UNKNOWN);
            return HF_INVALID_STATE_FPS_POLICY_HANDLE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        if (!a_policy) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(context.name, context.logger,
                HF_ERROR_STATE_FPS_INVALID_REQUEST);
            return HF_INVALID_STATE_FPS_POLICY_HANDLE;
        }
        return HolyFramework::StateFPSService::GetSingleton().AcquirePolicyOwned(*a_policy, context.name, context.logger);
    }

    HF_Bool HF_CALL StateFPSUpdate(const HF_StateFPSPolicyHandle a_handle, const HF_StateFPSPolicyV1* const a_policy)
    {
        std::string owner; std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule("<unknown>", HF_INVALID_LOG_HANDLE,
                HF_ERROR_STATE_FPS_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        if (!a_policy) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(context.name, context.logger,
                HF_ERROR_STATE_FPS_INVALID_REQUEST);
            return HF_FALSE;
        }
        std::string actualOwner;
        if (HolyFramework::StateFPSService::GetSingleton().UpdatePolicyOwned(a_handle, *a_policy, context.name, &actualOwner)) return HF_TRUE;
        HolyFramework::Diagnostics::ReportFrameworkFailureForModule(context.name, context.logger,
            actualOwner.empty() ? HF_ERROR_STATE_FPS_INVALID_REQUEST : HF_ERROR_STATE_FPS_OWNER_MISMATCH);
        return HF_FALSE;
    }

    HF_Bool HF_CALL StateFPSRelease(const HF_StateFPSPolicyHandle a_handle)
    {
        std::string owner; std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule("<unknown>", HF_INVALID_LOG_HANDLE,
                HF_ERROR_STATE_FPS_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        std::string actualOwner;
        if (HolyFramework::StateFPSService::GetSingleton().ReleasePolicyOwned(a_handle, context.name, &actualOwner)) return HF_TRUE;
        HolyFramework::Diagnostics::ReportFrameworkFailureForModule(context.name, context.logger,
            actualOwner.empty() ? HF_ERROR_STATE_FPS_INVALID_REQUEST : HF_ERROR_STATE_FPS_OWNER_MISMATCH);
        return HF_FALSE;
    }

    HF_Bool HF_CALL CPUSchedulingIsAvailable()
    {
        return HolyFramework::CPUSchedulingService::GetSingleton().IsAvailable() ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL CPUSchedulingGetState(HF_CPUSchedulingStateV1* const a_outState)
    {
        if (!a_outState) return HF_FALSE;
        return HolyFramework::CPUSchedulingService::GetSingleton().GetState(*a_outState) ? HF_TRUE : HF_FALSE;
    }

    HF_CPUSchedulingHandle HF_CALL CPUSchedulingAcquire(const std::uint32_t a_maxLogicalProcessors, const std::uint32_t a_timeoutMs)
    {
        std::string owner; std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule("<unknown>", HF_INVALID_LOG_HANDLE,
                HF_ERROR_CPU_SCHEDULING_OWNER_UNKNOWN);
            return HF_INVALID_CPU_SCHEDULING_HANDLE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::CPUSchedulingService::GetSingleton().AcquireLimitOwned(
            a_maxLogicalProcessors, a_timeoutMs, context.name, context.logger);
    }

    HF_Bool HF_CALL CPUSchedulingUpdate(const HF_CPUSchedulingHandle a_handle, const std::uint32_t a_maxLogicalProcessors, const std::uint32_t a_timeoutMs)
    {
        std::string owner; std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule("<unknown>", HF_INVALID_LOG_HANDLE,
                HF_ERROR_CPU_SCHEDULING_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        std::string actualOwner;
        if (HolyFramework::CPUSchedulingService::GetSingleton().UpdateLimitOwned(
            a_handle, a_maxLogicalProcessors, a_timeoutMs, context.name, &actualOwner)) return HF_TRUE;
        HolyFramework::Diagnostics::ReportFrameworkFailureForModule(context.name, context.logger,
            actualOwner.empty() ? HF_ERROR_CPU_SCHEDULING_INVALID_REQUEST : HF_ERROR_CPU_SCHEDULING_OWNER_MISMATCH);
        return HF_FALSE;
    }

    HF_Bool HF_CALL CPUSchedulingRelease(const HF_CPUSchedulingHandle a_handle)
    {
        std::string owner; std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule("<unknown>", HF_INVALID_LOG_HANDLE,
                HF_ERROR_CPU_SCHEDULING_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        std::string actualOwner;
        if (HolyFramework::CPUSchedulingService::GetSingleton().ReleaseLimitOwned(a_handle, context.name, &actualOwner)) return HF_TRUE;
        HolyFramework::Diagnostics::ReportFrameworkFailureForModule(context.name, context.logger,
            actualOwner.empty() ? HF_ERROR_CPU_SCHEDULING_INVALID_REQUEST : HF_ERROR_CPU_SCHEDULING_OWNER_MISMATCH);
        return HF_FALSE;
    }

    HF_Bool HF_CALL RuntimeTuningIsAvailable()
    {
        return HolyFramework::RuntimeTuningService::GetSingleton().IsAvailable() ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL RuntimeTuningGetState(HF_RuntimeTuningStateV1* const a_outState)
    {
        if (!a_outState) return HF_FALSE;
        return HolyFramework::RuntimeTuningService::GetSingleton().GetState(*a_outState) ? HF_TRUE : HF_FALSE;
    }

    HF_RuntimeTuningHandle HF_CALL RuntimeTuningAcquire(const HF_RuntimeTuningPolicyV1* const a_policy)
    {
        std::string owner; std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule("<unknown>", HF_INVALID_LOG_HANDLE,
                HF_ERROR_RUNTIME_TUNING_OWNER_UNKNOWN);
            return HF_INVALID_RUNTIME_TUNING_HANDLE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        if (!a_policy) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(context.name, context.logger,
                HF_ERROR_RUNTIME_TUNING_INVALID_REQUEST);
            return HF_INVALID_RUNTIME_TUNING_HANDLE;
        }
        return HolyFramework::RuntimeTuningService::GetSingleton().AcquirePolicyOwned(*a_policy, context.name, context.logger);
    }

    HF_Bool HF_CALL RuntimeTuningUpdate(const HF_RuntimeTuningHandle a_handle, const HF_RuntimeTuningPolicyV1* const a_policy)
    {
        std::string owner; std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule("<unknown>", HF_INVALID_LOG_HANDLE,
                HF_ERROR_RUNTIME_TUNING_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        if (!a_policy) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(context.name, context.logger,
                HF_ERROR_RUNTIME_TUNING_INVALID_REQUEST);
            return HF_FALSE;
        }
        std::string actualOwner;
        if (HolyFramework::RuntimeTuningService::GetSingleton().UpdatePolicyOwned(a_handle, *a_policy, context.name, &actualOwner)) return HF_TRUE;
        HolyFramework::Diagnostics::ReportFrameworkFailureForModule(context.name, context.logger,
            actualOwner.empty() ? HF_ERROR_RUNTIME_TUNING_INVALID_REQUEST : HF_ERROR_RUNTIME_TUNING_OWNER_MISMATCH);
        return HF_FALSE;
    }

    HF_Bool HF_CALL RuntimeTuningRelease(const HF_RuntimeTuningHandle a_handle)
    {
        std::string owner; std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule("<unknown>", HF_INVALID_LOG_HANDLE,
                HF_ERROR_RUNTIME_TUNING_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        std::string actualOwner;
        if (HolyFramework::RuntimeTuningService::GetSingleton().ReleasePolicyOwned(a_handle, context.name, &actualOwner)) return HF_TRUE;
        HolyFramework::Diagnostics::ReportFrameworkFailureForModule(context.name, context.logger,
            actualOwner.empty() ? HF_ERROR_RUNTIME_TUNING_INVALID_REQUEST : HF_ERROR_RUNTIME_TUNING_OWNER_MISMATCH);
        return HF_FALSE;
    }


    HF_Bool HF_CALL SerializationIsAvailable()
    {
        return HolyFramework::SerializationService::GetSingleton().IsAvailable() ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL SerializationSetRecord(
        const std::uint32_t a_key,
        const std::uint32_t a_version,
        const void* const a_data,
        const std::uint32_t a_dataSize)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_SERIALIZATION_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::SerializationService::GetSingleton().SetRecordOwned(
                   context.name ? context.name : "",
                   context.logger,
                   a_key,
                   a_version,
                   a_data,
                   a_dataSize) ?
                   HF_TRUE :
                   HF_FALSE;
    }

    HF_Bool HF_CALL SerializationGetRecordInfo(
        const std::uint32_t a_key,
        HF_SerializationRecordInfoV1* const a_outInfo)
    {
        if (!a_outInfo) {
            return HF_FALSE;
        }
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_SERIALIZATION_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::SerializationService::GetSingleton().GetRecordInfoOwned(
                   context.name ? context.name : "", a_key, *a_outInfo) ?
                   HF_TRUE :
                   HF_FALSE;
    }

    HF_Bool HF_CALL SerializationReadRecord(
        const std::uint32_t a_key,
        void* const a_buffer,
        const std::uint32_t a_bufferSize,
        std::uint32_t* const a_outBytes)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            if (a_outBytes) {
                *a_outBytes = 0;
            }
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_SERIALIZATION_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::SerializationService::GetSingleton().ReadRecordOwned(
                   context.name ? context.name : "", a_key, a_buffer, a_bufferSize, a_outBytes) ?
                   HF_TRUE :
                   HF_FALSE;
    }

    HF_Bool HF_CALL SerializationRemoveRecord(const std::uint32_t a_key)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_SERIALIZATION_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::SerializationService::GetSingleton().RemoveRecordOwned(
                   context.name ? context.name : "", a_key) ?
                   HF_TRUE :
                   HF_FALSE;
    }

    std::uint32_t HF_CALL SerializationClearRecords()
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_SERIALIZATION_OWNER_UNKNOWN);
            return 0;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::SerializationService::GetSingleton().ClearRecordsOwned(
            context.name ? context.name : "");
    }

    std::uint32_t HF_CALL SerializationGetRecordCount()
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            return 0;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::SerializationService::GetSingleton().GetRecordCountOwned(
            context.name ? context.name : "");
    }

    std::uint64_t HF_CALL SerializationGetGeneration()
    {
        return HolyFramework::SerializationService::GetSingleton().GetGeneration();
    }


    HF_Bool HF_CALL ConfigHasKey(const char* const a_key)
    {
        if (!a_key || !*a_key) {
            return HF_FALSE;
        }
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_CONFIG_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::ConfigManager::GetSingleton().HasKey(context.name, a_key) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL ConfigGetBool(
        const char* const a_key,
        const HF_Bool a_defaultValue,
        HF_Bool* const a_outValue)
    {
        if (!a_key || !*a_key || !a_outValue) {
            return HF_FALSE;
        }
        *a_outValue = a_defaultValue;
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            return HF_FALSE;
        }
        bool value = a_defaultValue == HF_TRUE;
        const auto context = HolyFramework::ModuleContext::Current();
        const auto found = HolyFramework::ConfigManager::GetSingleton().GetBool(context.name, a_key, value, value);
        *a_outValue = value ? HF_TRUE : HF_FALSE;
        return found ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL ConfigGetInt64(
        const char* const a_key,
        const std::int64_t a_defaultValue,
        std::int64_t* const a_outValue)
    {
        if (!a_key || !*a_key || !a_outValue) {
            return HF_FALSE;
        }
        *a_outValue = a_defaultValue;
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::ConfigManager::GetSingleton().GetInt64(
            context.name, a_key, a_defaultValue, *a_outValue) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL ConfigGetDouble(
        const char* const a_key,
        const double a_defaultValue,
        double* const a_outValue)
    {
        if (!a_key || !*a_key || !a_outValue) {
            return HF_FALSE;
        }
        *a_outValue = a_defaultValue;
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::ConfigManager::GetSingleton().GetDouble(
            context.name, a_key, a_defaultValue, *a_outValue) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL ConfigGetString(
        const char* const a_key,
        const char* const a_defaultValue,
        char* const a_buffer,
        const std::uint32_t a_bufferSize)
    {
        if (!a_key || !*a_key || !a_buffer || a_bufferSize == 0) {
            return HF_FALSE;
        }
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        std::string value = a_defaultValue ? a_defaultValue : "";
        bool found = false;
        if (EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            const auto context = HolyFramework::ModuleContext::Current();
            found = HolyFramework::ConfigManager::GetSingleton().GetString(
                context.name, a_key, value, value);
        }
        std::snprintf(a_buffer, a_bufferSize, "%s", value.c_str());
        return found ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL ConfigReload()
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::ConfigManager::GetSingleton().Reload(context.name) ? HF_TRUE : HF_FALSE;
    }

    std::uint64_t HF_CALL ConfigGetGeneration()
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            return 0;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::ConfigManager::GetSingleton().GetGeneration(context.name);
    }


    HF_ConfigDocumentHandle HF_CALL ConfigDocumentsOpenIni(
        const HF_ConfigDocumentRoot a_root,
        const char* const a_relativePath)
    {
        if (!a_relativePath || !*a_relativePath) {
            return HF_INVALID_CONFIG_DOCUMENT_HANDLE;
        }
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_CONFIG_DOCUMENT_OWNER_UNKNOWN);
            return HF_INVALID_CONFIG_DOCUMENT_HANDLE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::ConfigManager::GetSingleton().OpenDocument(
            a_root, a_relativePath, context.name ? context.name : "", context.logger);
    }

    HF_Bool HF_CALL ConfigDocumentsClose(const HF_ConfigDocumentHandle a_handle)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_CONFIG_DOCUMENT_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        std::string actualOwner;
        if (HolyFramework::ConfigManager::GetSingleton().CloseDocument(
                a_handle, context.name ? context.name : "", &actualOwner)) {
            return HF_TRUE;
        }
        if (!actualOwner.empty()) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name, context.logger, HF_ERROR_CONFIG_DOCUMENT_OWNER_MISMATCH);
        }
        return HF_FALSE;
    }

    HF_Bool HF_CALL ConfigDocumentsGetState(
        const HF_ConfigDocumentHandle a_handle,
        HF_ConfigDocumentStateV1* const a_outState)
    {
        if (!a_outState) return HF_FALSE;
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) return HF_FALSE;
        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::ConfigManager::GetSingleton().GetDocumentState(
                   a_handle, context.name ? context.name : "", *a_outState) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL ConfigDocumentsRefreshIfChanged(
        const HF_ConfigDocumentHandle a_handle,
        const std::uint32_t a_minCheckIntervalMs,
        HF_Bool* const a_outChanged)
    {
        if (!a_outChanged) return HF_FALSE;
        *a_outChanged = HF_FALSE;
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) return HF_FALSE;
        const auto context = HolyFramework::ModuleContext::Current();
        bool changed = false;
        const bool ok = HolyFramework::ConfigManager::GetSingleton().RefreshDocument(
            a_handle, context.name ? context.name : "", a_minCheckIntervalMs, changed);
        *a_outChanged = changed ? HF_TRUE : HF_FALSE;
        return ok ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL ConfigDocumentsHasKey(
        const HF_ConfigDocumentHandle a_handle,
        const char* const a_key)
    {
        if (!a_key || !*a_key) return HF_FALSE;
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) return HF_FALSE;
        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::ConfigManager::GetSingleton().DocumentHasKey(
                   a_handle, context.name ? context.name : "", a_key) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL ConfigDocumentsGetBool(
        const HF_ConfigDocumentHandle a_handle,
        const char* const a_key,
        const HF_Bool a_defaultValue,
        HF_Bool* const a_outValue)
    {
        if (!a_key || !*a_key || !a_outValue) return HF_FALSE;
        *a_outValue = a_defaultValue;
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) return HF_FALSE;
        const auto context = HolyFramework::ModuleContext::Current();
        bool value = a_defaultValue == HF_TRUE;
        const bool found = HolyFramework::ConfigManager::GetSingleton().DocumentGetBool(
            a_handle, context.name ? context.name : "", a_key, value, value);
        *a_outValue = value ? HF_TRUE : HF_FALSE;
        return found ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL ConfigDocumentsGetInt64(
        const HF_ConfigDocumentHandle a_handle,
        const char* const a_key,
        const std::int64_t a_defaultValue,
        std::int64_t* const a_outValue)
    {
        if (!a_key || !*a_key || !a_outValue) return HF_FALSE;
        *a_outValue = a_defaultValue;
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) return HF_FALSE;
        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::ConfigManager::GetSingleton().DocumentGetInt64(
                   a_handle, context.name ? context.name : "", a_key, a_defaultValue, *a_outValue) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL ConfigDocumentsGetDouble(
        const HF_ConfigDocumentHandle a_handle,
        const char* const a_key,
        const double a_defaultValue,
        double* const a_outValue)
    {
        if (!a_key || !*a_key || !a_outValue) return HF_FALSE;
        *a_outValue = a_defaultValue;
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) return HF_FALSE;
        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::ConfigManager::GetSingleton().DocumentGetDouble(
                   a_handle, context.name ? context.name : "", a_key, a_defaultValue, *a_outValue) ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL ConfigDocumentsGetString(
        const HF_ConfigDocumentHandle a_handle,
        const char* const a_key,
        const char* const a_defaultValue,
        char* const a_buffer,
        const std::uint32_t a_bufferSize)
    {
        if (!a_key || !*a_key || !a_buffer || a_bufferSize == 0) return HF_FALSE;
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        std::string value = a_defaultValue ? a_defaultValue : "";
        bool found = false;
        if (EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            const auto context = HolyFramework::ModuleContext::Current();
            found = HolyFramework::ConfigManager::GetSingleton().DocumentGetString(
                a_handle, context.name ? context.name : "", a_key, value, value);
        }
        std::snprintf(a_buffer, a_bufferSize, "%s", value.c_str());
        return found ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL PlayerMovementIsAvailable()
    {
        return HolyFramework::PlayerMovementService::GetSingleton().IsAvailable() ? HF_TRUE : HF_FALSE;
    }

    HF_Bool HF_CALL PlayerMovementGetLatest(HF_PlayerMovementSampleV1* const a_outSample)
    {
        return a_outSample && HolyFramework::PlayerMovementService::GetSingleton().GetLatest(*a_outSample) ? HF_TRUE : HF_FALSE;
    }

    HF_PlayerMovementSubscriptionHandle HF_CALL PlayerMovementSubscribe(
        const HF_PlayerMovementCallback a_callback,
        void* const a_userData)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_PLAYER_MOVEMENT_OWNER_UNKNOWN);
            return HF_INVALID_PLAYER_MOVEMENT_SUBSCRIPTION_HANDLE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        return HolyFramework::PlayerMovementService::GetSingleton().Subscribe(
            a_callback, a_userData, context.name ? context.name : "", context.logger);
    }

    HF_Bool HF_CALL PlayerMovementUnsubscribe(
        const HF_PlayerMovementSubscriptionHandle a_handle)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_PLAYER_MOVEMENT_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        std::string actualOwner;
        if (HolyFramework::PlayerMovementService::GetSingleton().Unsubscribe(
                a_handle, context.name ? context.name : "", &actualOwner)) {
            return HF_TRUE;
        }
        HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
            context.name, context.logger,
            actualOwner.empty() ? HF_ERROR_PLAYER_MOVEMENT_INVALID_REQUEST : HF_ERROR_PLAYER_MOVEMENT_OWNER_MISMATCH);
        return HF_FALSE;
    }

    HF_PostProcessEffectHandle HF_CALL PostProcessCreateEffect(
        const HF_PostProcessEffectDescV1* const a_desc)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_POST_PROCESS_OWNER_UNKNOWN);
            return HF_INVALID_POST_PROCESS_EFFECT_HANDLE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        if (!a_desc) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                context.name, context.logger, HF_ERROR_POST_PROCESS_INVALID_REQUEST);
            return HF_INVALID_POST_PROCESS_EFFECT_HANDLE;
        }
        return HolyFramework::PostProcessService::GetSingleton().CreateEffect(
            *a_desc, context.name ? context.name : "", context.logger);
    }

    HF_Bool HF_CALL PostProcessDestroyEffect(const HF_PostProcessEffectHandle a_handle)
    {
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_POST_PROCESS_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto context = HolyFramework::ModuleContext::Current();
        std::string actualOwner;
        if (HolyFramework::PostProcessService::GetSingleton().DestroyEffect(
                a_handle, context.name ? context.name : "", &actualOwner)) {
            return HF_TRUE;
        }
        HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
            context.name, context.logger,
            actualOwner.empty() ? HF_ERROR_POST_PROCESS_INVALID_REQUEST : HF_ERROR_POST_PROCESS_OWNER_MISMATCH);
        return HF_FALSE;
    }

    HF_Bool HF_CALL PostProcessDraw(
        const HF_PostProcessEffectHandle a_handle,
        const HF_RenderStageContextV1* const a_context,
        const void* const a_constants,
        const std::uint32_t a_constantBytes)
    {
        if (!a_context) return HF_FALSE;
        std::string owner;
        std::unique_ptr<HolyFramework::ModuleContext::Scope> scope;
        if (!EnsureCallerModuleScope(HF_CAPTURE_CALLER_ADDRESS(), owner, scope)) {
            HolyFramework::Diagnostics::ReportFrameworkFailureForModule(
                "<unknown>", HF_INVALID_LOG_HANDLE, HF_ERROR_POST_PROCESS_OWNER_UNKNOWN);
            return HF_FALSE;
        }
        const auto moduleContext = HolyFramework::ModuleContext::Current();
        return HolyFramework::PostProcessService::GetSingleton().Draw(
                   a_handle, *a_context, a_constants, a_constantBytes,
                   moduleContext.name ? moduleContext.name : "") ? HF_TRUE : HF_FALSE;
    }

    const HF_CoreV1 g_coreV1{
        .structSize = sizeof(HF_CoreV1),
        .interfaceVersion = HF_CORE_INTERFACE_VERSION,
        .GetFrameworkName = CoreGetFrameworkName,
        .GetFrameworkVersion = CoreGetFrameworkVersion,
        .GetRuntimeVersion = CoreGetRuntimeVersion,
        .GetF4SEVersion = CoreGetF4SEVersion,
        .IsReady = CoreIsReady,
        .GetLoadedModuleCount = CoreGetLoadedModuleCount
    };

    const HF_Log g_log{
        .structSize = sizeof(HF_Log),
        .interfaceVersion = HF_LOG_INTERFACE_VERSION,
        .GetCurrentLogger = LogGetCurrentLogger,
        .Open = LogOpen,
        .Write = LogWrite,
        .Flush = LogFlush
    };

    const HF_EventsV1 g_eventsV1{
        .structSize = sizeof(HF_EventsV1),
        .interfaceVersion = HF_EVENTS_INTERFACE_VERSION,
        .Subscribe = EventsSubscribe,
        .Unsubscribe = EventsUnsubscribe
    };


    const HF_DiagnosticsV2 g_diagnosticsV2{
        .structSize = sizeof(HF_DiagnosticsV2),
        .interfaceVersion = HF_DIAGNOSTICS_INTERFACE_VERSION,
        .SetCheckpoint = DiagnosticsSetCheckpoint,
        .ClearCheckpoint = DiagnosticsClearCheckpoint,
        .ReportFailure = DiagnosticsReportFailure,
        .GetErrorName = DiagnosticsGetErrorName,
        .GetErrorDescription = DiagnosticsGetErrorDescription
    };

    const HF_UI g_ui{
        .structSize = sizeof(HF_UI),
        .interfaceVersion = HF_UI_INTERFACE_VERSION,
        .ShowNotification = UIShowNotification,
        .IsMenuOpen = UIIsMenuOpen,
        .GetStateFlags = UIGetStateFlags,
        .HasState = UIHasState,
        .SubscribeMenuEvents = UISubscribeMenuEvents,
        .UnsubscribeMenuEvents = UIUnsubscribeMenuEvents,
        .GetLoadingMenuState = UIGetLoadingMenuState,
        .AcquireLoadingMenuPolicy = UIAcquireLoadingMenuPolicy,
        .ReleaseLoadingMenuPolicy = UIReleaseLoadingMenuPolicy
    };

    const HF_GameV1 g_gameV1{
        .structSize = sizeof(HF_GameV1),
        .interfaceVersion = HF_GAME_INTERFACE_VERSION,
        .GetStateFlags = GameGetStateFlags,
        .HasState = GameHasState,
        .IsPaused = GameIsPaused,
        .IsLoading = GameIsLoading,
        .IsInGame = GameIsInGame
    };

    const HF_GameSettingsV1 g_gameSettingsV1{
        .structSize = sizeof(HF_GameSettingsV1),
        .interfaceVersion = HF_GAME_SETTINGS_INTERFACE_VERSION,
        .Exists = GameSettingsExists,
        .GetType = GameSettingsGetType,
        .GetBool = GameSettingsGetBool,
        .GetInt32 = GameSettingsGetInt32,
        .GetUInt32 = GameSettingsGetUInt32,
        .GetFloat = GameSettingsGetFloat,
        .GetString = GameSettingsGetString,
        .SetBool = GameSettingsSetBool,
        .SetInt32 = GameSettingsSetInt32,
        .SetUInt32 = GameSettingsSetUInt32,
        .SetFloat = GameSettingsSetFloat,
        .Release = GameSettingsRelease
    };

    const HF_FormsV1 g_formsV1{
        .structSize = sizeof(HF_FormsV1),
        .interfaceVersion = HF_FORMS_INTERFACE_VERSION,
        .LookupByID = FormsLookupByID,
        .LookupByEditorID = FormsLookupByEditorID,
        .IsValid = FormsIsValid,
        .GetInfo = FormsGetInfo,
        .IsType = FormsIsType
    };


    const HF_ReferencesV2 g_referencesV2{
        .structSize = sizeof(HF_ReferencesV2),
        .interfaceVersion = HF_REFERENCES_INTERFACE_VERSION,
        .IsReference = ReferencesIsReference,
        .GetStateFlags = ReferencesGetStateFlags,
        .GetPosition = ReferencesGetPosition,
        .GetBaseForm = ReferencesGetBaseForm,
        .GetParentCell = ReferencesGetParentCell,
        .GetDisplayName = ReferencesGetDisplayName
    };

    const HF_ActorsV3 g_actorsV3{
        .structSize = sizeof(HF_ActorsV3),
        .interfaceVersion = HF_ACTORS_INTERFACE_VERSION,
        .IsActor = ActorsIsActor,
        .GetStateFlags = ActorsGetStateFlags,
        .GetBaseActor = ActorsGetBaseActor,
        .GetActorValue = ActorsGetActorValue,
        .GetBaseActorValue = ActorsGetBaseActorValue,
        .GetPermanentActorValue = ActorsGetPermanentActorValue,
        .GetActorValueForm = ActorsGetActorValueForm,
        .IsActorValueForm = ActorsIsActorValueForm,
        .GetActorValueByForm = ActorsGetActorValueByForm,
        .GetBaseActorValueByForm = ActorsGetBaseActorValueByForm,
        .GetPermanentActorValueByForm = ActorsGetPermanentActorValueByForm,
        .GetModifierByForm = ActorsGetModifierByForm,
        .ModifyActorValueByForm = ActorsModifyActorValueByForm,
        .AcquireAdjustment = ActorsAcquireAdjustment,
        .UpdateAdjustment = ActorsUpdateAdjustment,
        .ReleaseAdjustment = ActorsReleaseAdjustment
    };

    const HF_PlayerV2 g_playerV2{
        .structSize = sizeof(HF_PlayerV2),
        .interfaceVersion = HF_PLAYER_INTERFACE_VERSION,
        .IsAvailable = PlayerIsAvailable,
        .GetHandle = PlayerGetHandle,
        .IsGodMode = PlayerIsGodMode,
        .IsImmortal = PlayerIsImmortal,
        .IsPipboyLightOn = PlayerIsPipboyLightOn,
        .GetDialogueTarget = PlayerGetDialogueTarget
    };

    const HF_GraphicsV1 g_graphicsV1{
        .structSize = sizeof(HF_GraphicsV1),
        .interfaceVersion = HF_GRAPHICS_INTERFACE_VERSION,
        .IsAvailable = GraphicsIsAvailable,
        .GetState = GraphicsGetState,
        .GetNativeHandles = GraphicsGetNativeHandles
    };

    const HF_GameTimeV1 g_gameTimeV1{
        .structSize = sizeof(HF_GameTimeV1),
        .interfaceVersion = HF_GAME_TIME_INTERFACE_VERSION,
        .IsAvailable = GameTimeIsAvailable,
        .GetGameHour = GameTimeGetGameHour,
        .GetTimeScale = GameTimeGetTimeScale,
        .GetState = GameTimeGetState
    };

    const HF_EnvironmentV1 g_environmentV1{
        .structSize = sizeof(HF_EnvironmentV1),
        .interfaceVersion = HF_ENVIRONMENT_INTERFACE_VERSION,
        .IsAvailable = EnvironmentIsAvailable,
        .GetState = EnvironmentGetState,
        .GetWeatherInfo = EnvironmentGetWeatherInfo
    };


    const HF_LightingV1 g_lightingV1{
        .structSize = sizeof(HF_LightingV1),
        .interfaceVersion = HF_LIGHTING_INTERFACE_VERSION,
        .IsAvailable = LightingIsAvailable,
        .GetState = LightingGetState,
        .GetLightingTemplateInfo = LightingGetTemplateInfo,
        .GetImageSpaceInfo = LightingGetImageSpaceInfo,
        .GetWeatherImageSpace = LightingGetWeatherImageSpace
    };

    const HF_RenderPipelineV1 g_renderPipelineV1{
        .structSize = sizeof(HF_RenderPipelineV1),
        .interfaceVersion = HF_RENDER_PIPELINE_INTERFACE_VERSION,
        .IsStageSupported = RenderPipelineIsStageSupported,
        .Subscribe = RenderPipelineSubscribe,
        .Unsubscribe = RenderPipelineUnsubscribe
    };


    const HF_Presentation g_presentation{
        .structSize = sizeof(HF_Presentation),
        .interfaceVersion = HF_PRESENTATION_INTERFACE_VERSION,
        .IsAvailable = PresentationIsAvailable,
        .GetState = PresentationGetState,
        .SubscribePresent = PresentationSubscribePresent,
        .SubscribeResizeBuffers = PresentationSubscribeResizeBuffers,
        .Unsubscribe = PresentationUnsubscribe,
        .GetCapabilities = PresentationGetCapabilities,
        .SubscribeSwapChainCreate = PresentationSubscribeSwapChainCreate
    };

    const HF_FrameTimingV1 g_frameTimingV1{
        .structSize = sizeof(HF_FrameTimingV1),
        .interfaceVersion = HF_FRAME_TIMING_INTERFACE_VERSION,
        .IsAvailable = FrameTimingIsAvailable,
        .GetState = FrameTimingGetState,
        .GetDeltaSeconds = FrameTimingGetDeltaSeconds,
        .GetFramesPerSecond = FrameTimingGetFramesPerSecond
    };


    const HF_FramePacingV1 g_framePacingV1{
        .structSize = sizeof(HF_FramePacingV1),
        .interfaceVersion = HF_FRAME_PACING_INTERFACE_VERSION,
        .IsAvailable = FramePacingIsAvailable,
        .GetState = FramePacingGetState,
        .AcquireLimit = FramePacingAcquireLimit,
        .UpdateLimit = FramePacingUpdateLimit,
        .ReleaseLimit = FramePacingReleaseLimit
    };

    const HF_WindowV1 g_windowV1{
        .structSize = sizeof(HF_WindowV1),
        .interfaceVersion = HF_WINDOW_INTERFACE_VERSION,
        .IsAvailable = WindowIsAvailable,
        .GetState = WindowGetState,
        .IsForeground = WindowIsForeground,
        .AcquireCursorClip = WindowAcquireCursorClip,
        .ReleaseCursorClip = WindowReleaseCursorClip
    };

    const HF_DisplayV1 g_displayV1{
        .structSize = sizeof(HF_DisplayV1),
        .interfaceVersion = HF_DISPLAY_INTERFACE_VERSION,
        .IsAvailable = DisplayIsAvailable,
        .GetState = DisplayGetState
    };

    const HF_PresentationPolicyV1 g_presentationPolicyV1{
        .structSize = sizeof(HF_PresentationPolicyV1),
        .interfaceVersion = HF_PRESENTATION_POLICY_INTERFACE_VERSION,
        .IsAvailable = PresentationPolicyIsAvailable,
        .GetState = PresentationPolicyGetState,
        .AcquirePolicy = PresentationPolicyAcquire,
        .UpdatePolicy = PresentationPolicyUpdate,
        .ReleasePolicy = PresentationPolicyRelease
    };


    const HF_StateFPSV1 g_stateFPSV1{
        .structSize = sizeof(HF_StateFPSV1),
        .interfaceVersion = HF_STATE_FPS_INTERFACE_VERSION,
        .IsAvailable = StateFPSIsAvailable,
        .GetState = StateFPSGetState,
        .AcquirePolicy = StateFPSAcquire,
        .UpdatePolicy = StateFPSUpdate,
        .ReleasePolicy = StateFPSRelease
    };

    const HF_CPUSchedulingV1 g_cpuSchedulingV1{
        .structSize = sizeof(HF_CPUSchedulingV1),
        .interfaceVersion = HF_CPU_SCHEDULING_INTERFACE_VERSION,
        .IsAvailable = CPUSchedulingIsAvailable,
        .GetState = CPUSchedulingGetState,
        .AcquireProcessorLimit = CPUSchedulingAcquire,
        .UpdateProcessorLimit = CPUSchedulingUpdate,
        .ReleaseProcessorLimit = CPUSchedulingRelease
    };

    const HF_RuntimeTuningV1 g_runtimeTuningV1{
        .structSize = sizeof(HF_RuntimeTuningV1),
        .interfaceVersion = HF_RUNTIME_TUNING_INTERFACE_VERSION,
        .IsAvailable = RuntimeTuningIsAvailable,
        .GetState = RuntimeTuningGetState,
        .AcquirePolicy = RuntimeTuningAcquire,
        .UpdatePolicy = RuntimeTuningUpdate,
        .ReleasePolicy = RuntimeTuningRelease
    };

    const HF_SerializationV1 g_serializationV1{
        .structSize = sizeof(HF_SerializationV1),
        .interfaceVersion = HF_SERIALIZATION_INTERFACE_VERSION,
        .IsAvailable = SerializationIsAvailable,
        .SetRecord = SerializationSetRecord,
        .GetRecordInfo = SerializationGetRecordInfo,
        .ReadRecord = SerializationReadRecord,
        .RemoveRecord = SerializationRemoveRecord,
        .ClearRecords = SerializationClearRecords,
        .GetRecordCount = SerializationGetRecordCount,
        .GetGeneration = SerializationGetGeneration
    };


    const HF_RuntimeV1 g_runtimeV1{
        .structSize = sizeof(HF_RuntimeV1),
        .interfaceVersion = HF_RUNTIME_INTERFACE_VERSION,
        .GetStateFlags = RuntimeGetStateFlags,
        .GetSessionGeneration = RuntimeGetSessionGeneration,
        .HasState = RuntimeHasState
    };

    const HF_ModulesV2 g_modulesV2{
        .structSize = sizeof(HF_ModulesV2),
        .interfaceVersion = HF_MODULES_INTERFACE_VERSION,
        .GetCount = ModulesGetCount,
        .GetHealthyCount = ModulesGetHealthyCount,
        .GetDegradedCount = ModulesGetDegradedCount,
        .GetByIndex = ModulesGetByIndex,
        .FindByName = ModulesFindByName
    };


    const HF_TasksV1 g_tasksV1{
        .structSize = sizeof(HF_TasksV1),
        .interfaceVersion = HF_TASKS_INTERFACE_VERSION,
        .Queue = TasksQueue,
        .QueueDelayed = TasksQueueDelayed,
        .Cancel = TasksCancel,
        .GetPendingCount = TasksGetPendingCount
    };


    const HF_MemoryV1 g_memoryV1{
        .structSize = sizeof(HF_MemoryV1),
        .interfaceVersion = HF_MEMORY_INTERFACE_VERSION,
        .ResolveID = MemoryResolveID,
        .QueryRegion = MemoryQueryRegion,
        .Read = MemoryRead,
        .Compare = MemoryCompare,
        .ApplyPatch = MemoryApplyPatch,
        .RestorePatch = MemoryRestorePatch,
        .VerifyPatch = MemoryVerifyPatch,
        .GetPatchCount = MemoryGetPatchCount,
        .GetPatchByIndex = MemoryGetPatchByIndex,
        .AuditPatches = MemoryAuditPatches
    };

    const HF_HooksV1 g_hooksV1{
        .structSize = sizeof(HF_HooksV1),
        .interfaceVersion = HF_HOOKS_INTERFACE_VERSION,
        .GetTrampolineCapacity = HooksGetTrampolineCapacity,
        .GetTrampolineFreeSize = HooksGetTrampolineFreeSize,
        .AllocateCode = HooksAllocateCode,
        .WriteCode = HooksWriteCode,
        .Install = HooksInstall,
        .Restore = HooksRestore,
        .Verify = HooksVerify,
        .GetCount = HooksGetCount,
        .GetByIndex = HooksGetByIndex,
        .Audit = HooksAudit
    };


    const HF_CapabilitiesV1 g_capabilitiesV1{
        .structSize = sizeof(HF_CapabilitiesV1),
        .interfaceVersion = HF_CAPABILITIES_INTERFACE_VERSION,
        .Publish = CapabilitiesPublish,
        .GetCount = CapabilitiesGetCount,
        .GetByIndex = CapabilitiesGetByIndex,
        .FindFirst = CapabilitiesFindFirst
    };

    const HF_ResourcesV1 g_resourcesV1{
        .structSize = sizeof(HF_ResourcesV1),
        .interfaceVersion = HF_RESOURCES_INTERFACE_VERSION,
        .Claim = ResourcesClaim,
        .Release = ResourcesRelease,
        .IsAvailable = ResourcesIsAvailable,
        .GetCount = ResourcesGetCount,
        .GetByIndex = ResourcesGetByIndex
    };

    const HF_PerformanceV1 g_performanceV1{
        .structSize = sizeof(HF_PerformanceV1),
        .interfaceVersion = HF_PERFORMANCE_INTERFACE_VERSION,
        .BeginSample = PerformanceBeginSample,
        .EndSample = PerformanceEndSample,
        .GetCount = PerformanceGetCount,
        .GetByIndex = PerformanceGetByIndex
    };

    const HF_ConfigV1 g_configV1{
        .structSize = sizeof(HF_ConfigV1),
        .interfaceVersion = HF_CONFIG_INTERFACE_VERSION,
        .HasKey = ConfigHasKey,
        .GetBool = ConfigGetBool,
        .GetInt64 = ConfigGetInt64,
        .GetDouble = ConfigGetDouble,
        .GetString = ConfigGetString,
        .Reload = ConfigReload,
        .GetGeneration = ConfigGetGeneration
    };



    const HF_ConfigDocumentsV1 g_configDocumentsV1{
        .structSize = sizeof(HF_ConfigDocumentsV1),
        .interfaceVersion = HF_CONFIG_DOCUMENTS_INTERFACE_VERSION,
        .OpenIni = ConfigDocumentsOpenIni,
        .Close = ConfigDocumentsClose,
        .GetState = ConfigDocumentsGetState,
        .RefreshIfChanged = ConfigDocumentsRefreshIfChanged,
        .HasKey = ConfigDocumentsHasKey,
        .GetBool = ConfigDocumentsGetBool,
        .GetInt64 = ConfigDocumentsGetInt64,
        .GetDouble = ConfigDocumentsGetDouble,
        .GetString = ConfigDocumentsGetString
    };

    const HF_PlayerMovementV1 g_playerMovementV1{
        .structSize = sizeof(HF_PlayerMovementV1),
        .interfaceVersion = HF_PLAYER_MOVEMENT_INTERFACE_VERSION,
        .IsAvailable = PlayerMovementIsAvailable,
        .GetLatest = PlayerMovementGetLatest,
        .Subscribe = PlayerMovementSubscribe,
        .Unsubscribe = PlayerMovementUnsubscribe
    };

    const HF_PostProcessV1 g_postProcessV1{
        .structSize = sizeof(HF_PostProcessV1),
        .interfaceVersion = HF_POST_PROCESS_INTERFACE_VERSION,
        .CreateEffect = PostProcessCreateEffect,
        .DestroyEffect = PostProcessDestroyEffect,
        .Draw = PostProcessDraw
    };

    const void* HF_CALL QueryInterface(
        const std::uint32_t a_interfaceID,
        const std::uint32_t a_interfaceVersion)
    {
        switch (a_interfaceID) {
        case HF_INTERFACE_CORE:
            return a_interfaceVersion == HF_CORE_INTERFACE_VERSION ? &g_coreV1 : nullptr;
        case HF_INTERFACE_LOG:
            return a_interfaceVersion == HF_LOG_INTERFACE_VERSION ? &g_log : nullptr;
        case HF_INTERFACE_EVENTS:
            return a_interfaceVersion == HF_EVENTS_INTERFACE_VERSION ? &g_eventsV1 : nullptr;
        case HF_INTERFACE_UI:
            return a_interfaceVersion == HF_UI_INTERFACE_VERSION ? &g_ui : nullptr;
        case HF_INTERFACE_DIAGNOSTICS:
            return a_interfaceVersion == HF_DIAGNOSTICS_INTERFACE_VERSION ? &g_diagnosticsV2 : nullptr;
        case HF_INTERFACE_RUNTIME:
            return a_interfaceVersion == HF_RUNTIME_INTERFACE_VERSION ? &g_runtimeV1 : nullptr;
        case HF_INTERFACE_MODULES:
            return a_interfaceVersion == HF_MODULES_INTERFACE_VERSION ? &g_modulesV2 : nullptr;
        case HF_INTERFACE_TASKS:
            return a_interfaceVersion == HF_TASKS_INTERFACE_VERSION ? &g_tasksV1 : nullptr;
        case HF_INTERFACE_MEMORY:
            return a_interfaceVersion == HF_MEMORY_INTERFACE_VERSION ? &g_memoryV1 : nullptr;
        case HF_INTERFACE_HOOKS:
            return a_interfaceVersion == HF_HOOKS_INTERFACE_VERSION ? &g_hooksV1 : nullptr;
        case HF_INTERFACE_CAPABILITIES:
            return a_interfaceVersion == HF_CAPABILITIES_INTERFACE_VERSION ? &g_capabilitiesV1 : nullptr;
        case HF_INTERFACE_RESOURCES:
            return a_interfaceVersion == HF_RESOURCES_INTERFACE_VERSION ? &g_resourcesV1 : nullptr;
        case HF_INTERFACE_PERFORMANCE:
            return a_interfaceVersion == HF_PERFORMANCE_INTERFACE_VERSION ? &g_performanceV1 : nullptr;
        case HF_INTERFACE_CONFIG:
            return a_interfaceVersion == HF_CONFIG_INTERFACE_VERSION ? &g_configV1 : nullptr;
        case HF_INTERFACE_GAME:
            return a_interfaceVersion == HF_GAME_INTERFACE_VERSION ? &g_gameV1 : nullptr;
        case HF_INTERFACE_GAME_SETTINGS:
            return a_interfaceVersion == HF_GAME_SETTINGS_INTERFACE_VERSION ? &g_gameSettingsV1 : nullptr;
        case HF_INTERFACE_FORMS:
            return a_interfaceVersion == HF_FORMS_INTERFACE_VERSION ? &g_formsV1 : nullptr;
        case HF_INTERFACE_REFERENCES:
            return a_interfaceVersion == HF_REFERENCES_INTERFACE_VERSION ? &g_referencesV2 : nullptr;
        case HF_INTERFACE_ACTORS:
            return a_interfaceVersion == HF_ACTORS_INTERFACE_VERSION ? &g_actorsV3 : nullptr;
        case HF_INTERFACE_PLAYER:
            return a_interfaceVersion == HF_PLAYER_INTERFACE_VERSION ? &g_playerV2 : nullptr;
        case HF_INTERFACE_GRAPHICS:
            return a_interfaceVersion == HF_GRAPHICS_INTERFACE_VERSION ? &g_graphicsV1 : nullptr;
        case HF_INTERFACE_GAME_TIME:
            return a_interfaceVersion == HF_GAME_TIME_INTERFACE_VERSION ? &g_gameTimeV1 : nullptr;
        case HF_INTERFACE_ENVIRONMENT:
            return a_interfaceVersion == HF_ENVIRONMENT_INTERFACE_VERSION ? &g_environmentV1 : nullptr;
        case HF_INTERFACE_LIGHTING:
            return a_interfaceVersion == HF_LIGHTING_INTERFACE_VERSION ? &g_lightingV1 : nullptr;
        case HF_INTERFACE_RENDER_PIPELINE:
            return a_interfaceVersion == HF_RENDER_PIPELINE_INTERFACE_VERSION ? &g_renderPipelineV1 : nullptr;
        case HF_INTERFACE_SERIALIZATION:
            return a_interfaceVersion == HF_SERIALIZATION_INTERFACE_VERSION ? &g_serializationV1 : nullptr;
        case HF_INTERFACE_PRESENTATION:
            return a_interfaceVersion == HF_PRESENTATION_INTERFACE_VERSION ? &g_presentation : nullptr;
        case HF_INTERFACE_FRAME_TIMING:
            return a_interfaceVersion == HF_FRAME_TIMING_INTERFACE_VERSION ? &g_frameTimingV1 : nullptr;
        case HF_INTERFACE_FRAME_PACING:
            return a_interfaceVersion == HF_FRAME_PACING_INTERFACE_VERSION ? &g_framePacingV1 : nullptr;
        case HF_INTERFACE_WINDOW:
            return a_interfaceVersion == HF_WINDOW_INTERFACE_VERSION ? &g_windowV1 : nullptr;
        case HF_INTERFACE_DISPLAY:
            return a_interfaceVersion == HF_DISPLAY_INTERFACE_VERSION ? &g_displayV1 : nullptr;
        case HF_INTERFACE_PRESENTATION_POLICY:
            return a_interfaceVersion == HF_PRESENTATION_POLICY_INTERFACE_VERSION ? &g_presentationPolicyV1 : nullptr;
        case HF_INTERFACE_STATE_FPS:
            return a_interfaceVersion == HF_STATE_FPS_INTERFACE_VERSION ? &g_stateFPSV1 : nullptr;
        case HF_INTERFACE_CPU_SCHEDULING:
            return a_interfaceVersion == HF_CPU_SCHEDULING_INTERFACE_VERSION ? &g_cpuSchedulingV1 : nullptr;
        case HF_INTERFACE_RUNTIME_TUNING:
            return a_interfaceVersion == HF_RUNTIME_TUNING_INTERFACE_VERSION ? &g_runtimeTuningV1 : nullptr;
        case HF_INTERFACE_CONFIG_DOCUMENTS:
            return a_interfaceVersion == HF_CONFIG_DOCUMENTS_INTERFACE_VERSION ? &g_configDocumentsV1 : nullptr;
        case HF_INTERFACE_PLAYER_MOVEMENT:
            return a_interfaceVersion == HF_PLAYER_MOVEMENT_INTERFACE_VERSION ? &g_playerMovementV1 : nullptr;
        case HF_INTERFACE_POST_PROCESS:
            return a_interfaceVersion == HF_POST_PROCESS_INTERFACE_VERSION ? &g_postProcessV1 : nullptr;
        default:
            return nullptr;
        }
    }

    const HF_API g_api{
        .structSize = sizeof(HF_API),
        .abiVersion = HF_ABI_VERSION,
        .QueryInterface = QueryInterface
    };
}

namespace HolyFramework
{
    void SetRuntimeVersion(const REL::Version a_version) noexcept
    {
        g_runtimeVersion = a_version;
    }

    void SetReady(const bool a_ready) noexcept
    {
        g_ready.store(a_ready, std::memory_order_release);
        RuntimeState::GetSingleton().SetFrameworkReady(a_ready);
    }

    const HF_API* GetAPI(const std::uint32_t a_requestedABIVersion) noexcept
    {
        return a_requestedABIVersion == HF_ABI_VERSION ? &g_api : nullptr;
    }
}

HF_FRAMEWORK_EXPORT const HF_API* HF_CALL HF_GetAPI(const std::uint32_t a_requestedABIVersion)
{
    return HolyFramework::GetAPI(a_requestedABIVersion);
}
