#include "pch.h"

#include <Windows.h>
#include "ModuleLoader.h"
#include "EventBus.h"
#include "HookManager.h"
#include "TaskScheduler.h"
#include "UIStateService.h"
#include "Diagnostics.h"
#include "ConfigManager.h"
#include "GameSettingsManager.h"
#include "ErrorCatalog.h"
#include "ModuleContext.h"
#include "ModuleLogManager.h"
#include "MemoryManager.h"
#include "ModuleSignature.h"
#include "PerformanceMonitor.h"
#include "ResourceRegistry.h"
#include "RenderPipelineService.h"
#include "PresentationService.h"
#include "PresentationPolicyService.h"
#include "StateFPSService.h"
#include "CPUSchedulingService.h"
#include "RuntimeTuningService.h"
#include "FramePacingService.h"
#include "WindowService.h"
#include "SerializationService.h"
#include "ReferenceActorService.h"
#include "PlayerMovementService.h"
#include "PostProcessService.h"

namespace HolyFramework
{
    ModuleLoader& ModuleLoader::GetSingleton() noexcept
    {
        static ModuleLoader* instance = new ModuleLoader();
        return *instance;
    }

    std::filesystem::path ModuleLoader::GetModuleDirectory()
    {
        std::wstring buffer(32768, L'\0');
        const auto length = REX::W32::GetModuleFileNameW(
            REX::W32::GetCurrentModule(),
            buffer.data(),
            static_cast<std::uint32_t>(buffer.size()));

        if (length == 0 || length >= buffer.size()) {
            return {};
        }

        buffer.resize(length);
        auto frameworkPath = std::filesystem::path{ buffer };
        return frameworkPath.parent_path() / L"HolyFramework";
    }

    std::uint32_t ModuleLoader::LoadAll(const HF_API* const a_api)
    {
        const auto directory = GetModuleDirectory();
        if (directory.empty()) {
            Diagnostics::ReportFrameworkFailureForModule(
                "HolyFramework",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_FRAMEWORK_MODULE_DIRECTORY);
            REX::WARN("Native module loading skipped because the module directory could not be resolved");
            return 0;
        }

        std::error_code ec;
        if (!std::filesystem::exists(directory, ec)) {
            REX::INFO("Module directory does not exist yet; no native modules will be loaded");
            return 0;
        }

        std::vector<std::filesystem::path> candidates;
        for (std::filesystem::directory_iterator it{ directory, ec }, end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) {
                continue;
            }

            auto extension = it->path().extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](const wchar_t ch) {
                return static_cast<wchar_t>(std::towlower(ch));
            });

            if (extension == L".dll") {
                candidates.push_back(it->path());
            }
        }

        if (ec) {
            Diagnostics::ReportFrameworkFailureForModule(
                "HolyFramework",
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_FRAMEWORK_MODULE_ENUMERATION);
            REX::WARN("Native module enumeration ended early");
            return GetLoadedCount();
        }

        std::ranges::sort(candidates);
        for (const auto& candidate : candidates) {
            LoadOne(candidate, a_api);
        }

        return GetLoadedCount();
    }

    bool ModuleLoader::LoadOne(const std::filesystem::path& a_path, const HF_API* const a_api)
    {
        const std::string moduleFileName = a_path.stem().string();

        // Lightweight module marker check before LoadLibraryW. This is not
        // DRM; it only prevents unrelated DLLs from being loaded accidentally from
        // the HolyFramework module directory.
        const auto markerStatus = ModuleSignature::CheckFileMarker(a_path);
        if (markerStatus != ModuleSignatureStatus::Present) {
            const auto code = markerStatus == ModuleSignatureStatus::Missing ?
                HF_ERROR_MODULE_SIGNATURE_MISSING : HF_ERROR_MODULE_SIGNATURE_IO_FAILED;
            Diagnostics::ReportFrameworkFailureForModule(
                moduleFileName, HF_INVALID_LOG_HANDLE, code);
            REX::WARN("Ignoring native module '{}': HolyFramework signature marker not found", a_path.filename().string());
            return false;
        }

        auto moduleToml = a_path;
        moduleToml.replace_extension(L".toml");
        std::error_code tomlError;
        if (!std::filesystem::is_regular_file(moduleToml, tomlError)) {
            Diagnostics::ReportFrameworkFailureForModule(
                moduleFileName,
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_MODULE_TOML_MISSING);
            REX::WARN("Ignoring native module '{}': matching TOML is missing", a_path.filename().string());
            return false;
        }

        auto handle = REX::W32::LoadLibraryW(a_path.c_str());
        if (!handle) {
            Diagnostics::ReportFrameworkFailureForModule(
                moduleFileName,
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_MODULE_LIBRARY_LOAD_FAILED);
            REX::WARN("Could not load native module '{}'", a_path.filename().string());
            return false;
        }

        const auto* signature = reinterpret_cast<const HF_ModuleSignatureV1*>(
            REX::W32::GetProcAddress(handle, "HF_HolyFrameworkSignature"));
        if (!signature || signature->structSize < sizeof(HF_ModuleSignatureV1) ||
            std::memcmp(signature->signatureText, HF_MODULE_SIGNATURE_TEXT, sizeof(HF_MODULE_SIGNATURE_TEXT)) != 0) {
            Diagnostics::ReportFrameworkFailureForModule(
                moduleFileName,
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_MODULE_SIGNATURE_INVALID);
            REX::WARN("Ignoring native module '{}': invalid HolyFramework signature export", a_path.filename().string());
            REX::W32::FreeLibrary(handle);
            return false;
        }
        if (signature->signatureVersion != HF_MODULE_SIGNATURE_VERSION) {
            Diagnostics::ReportFrameworkFailureForModule(
                moduleFileName,
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_MODULE_SIGNATURE_VERSION_UNSUPPORTED);
            REX::WARN("Ignoring native module '{}': unsupported HolyFramework signature version", a_path.filename().string());
            REX::W32::FreeLibrary(handle);
            return false;
        }

        const bool prefixTerminated = signature->errorPrefix[3] == '\0';
        const std::string errorPrefix{ signature->errorPrefix, signature->errorPrefix + 3 };
        const auto validPrefix = [&]() noexcept {
            if (!prefixTerminated || errorPrefix.size() != 3 || errorPrefix == "HFW") return false;
            return std::ranges::all_of(errorPrefix, [](const unsigned char ch) {
                return ch >= static_cast<unsigned char>('A') &&
                    ch <= static_cast<unsigned char>('Z');
            });
        }();
        if (!validPrefix) {
            Diagnostics::ReportFrameworkFailureForModule(
                moduleFileName,
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_MODULE_SIGNATURE_METADATA_INVALID);
            REX::WARN("Ignoring native module '{}': invalid HolyFramework signature metadata", a_path.filename().string());
            REX::W32::FreeLibrary(handle);
            return false;
        }

        const auto getInfo = reinterpret_cast<HF_GetModuleInfoFn>(
            REX::W32::GetProcAddress(handle, "HF_GetModuleInfo"));
        const auto load = reinterpret_cast<HF_ModuleLoadFn>(
            REX::W32::GetProcAddress(handle, "HF_ModuleLoad"));
        const auto unload = reinterpret_cast<HF_ModuleUnloadFn>(
            REX::W32::GetProcAddress(handle, "HF_ModuleUnload"));

        if (!getInfo || !load) {
            Diagnostics::ReportFrameworkFailureForModule(
                moduleFileName,
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_MODULE_EXPORTS_MISSING);
            REX::WARN("Ignoring native module '{}'", a_path.filename().string());
            REX::W32::FreeLibrary(handle);
            return false;
        }

        const HF_ModuleInfoV1* info = nullptr;
        try {
            info = getInfo();
        } catch (...) {
            Diagnostics::ReportFrameworkFailureForModule(
                moduleFileName,
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_MODULE_INFO_EXCEPTION);
            REX::WARN("Ignoring native module '{}'", a_path.filename().string());
            REX::W32::FreeLibrary(handle);
            return false;
        }

        if (!info || info->structSize < sizeof(HF_ModuleInfoV1)) {
            Diagnostics::ReportFrameworkFailureForModule(
                moduleFileName,
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_MODULE_INFO_INVALID);
            REX::WARN("Ignoring native module '{}'", a_path.filename().string());
            REX::W32::FreeLibrary(handle);
            return false;
        }

        const std::string moduleName = info->name && *info->name ? info->name : moduleFileName;

        // Keep DLL filename, runtime identity, logger, INI and TOML one-to-one.
        if (!NamesEqual(moduleName, moduleFileName)) {
            Diagnostics::ReportFrameworkFailureForModule(
                moduleFileName,
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_MODULE_NAME_MISMATCH);
            REX::WARN("Ignoring native module '{}': metadata name '{}' does not match filename", a_path.filename().string(), moduleName);
            REX::W32::FreeLibrary(handle);
            return false;
        }

        // Display names are execution identities throughout HolyFramework. Two
        // loaded modules with the same name would make ownership and logs ambiguous.
        bool duplicateName = false;
        {
            std::scoped_lock lock{ _lock };
            duplicateName = std::ranges::any_of(_modules, [&](const LoadedModule& module) {
                return NamesEqual(module.name, moduleName);
            }) || std::ranges::any_of(_quarantinedModules, [&](const QuarantinedModule& module) {
                return NamesEqual(module.name, moduleName);
            });
        }
        if (duplicateName) {
            Diagnostics::ReportFrameworkFailureForModule(
                moduleName,
                HF_INVALID_LOG_HANDLE,
                HF_ERROR_MODULE_DUPLICATE_NAME);
            REX::WARN("Ignoring native module '{}': duplicate module name '{}'", a_path.filename().string(), moduleName);
            REX::W32::FreeLibrary(handle);
            return false;
        }

        // TOML is only an error catalog. The module signature supplies the prefix.
        if (!RegisterModuleErrorCatalog(moduleName, errorPrefix, moduleToml)) {
            Diagnostics::ReportFrameworkFailureForModule(
                moduleName, HF_INVALID_LOG_HANDLE, HF_ERROR_MODULE_ERROR_CATALOG_INVALID);
            REX::WARN("Ignoring native module '{}': error catalog rejected", a_path.filename().string());
            REX::W32::FreeLibrary(handle);
            return false;
        }

        auto moduleIni = a_path;
        moduleIni.replace_extension(L".ini");
        ConfigManager::GetSingleton().RegisterModule(moduleName, moduleIni);

        const auto moduleLogger = ModuleLogManager::GetSingleton().Open(moduleName);
        ModuleLogManager::GetSingleton().Write(moduleLogger, HF_LOG_INFO, "HolyFramework module log initialized");

        if (info->requiredABIVersion != HF_ABI_VERSION) {
            Diagnostics::ReportFrameworkFailureForModule(
                moduleName,
                moduleLogger,
                HF_ERROR_MODULE_ABI_MISMATCH);
            ConfigManager::GetSingleton().UnregisterModule(moduleName);
            UnregisterModuleErrorCatalog(moduleName);
            ModuleLogManager::GetSingleton().Close(moduleLogger);
            REX::W32::FreeLibrary(handle);
            return false;
        }

        {
            std::scoped_lock lock{ _lock };
            _provisionalModules.push_back(ProvisionalModule{
                .handle = handle,
                .name = moduleName,
                .logger = moduleLogger
            });
        }

        HF_Bool loaded = HF_FALSE;
        bool loadRaisedException = false;
        {
            ModuleContext::Scope scope{ moduleName.c_str(), moduleLogger };
            PerformanceMonitor::Scope perfScope{ moduleName, "lifecycle.load", 50'000 };
            try {
                loaded = load(a_api);
            } catch (const std::exception&) {
                loadRaisedException = true;
                Diagnostics::ReportFrameworkFailureForModule(
                    moduleName,
                    moduleLogger,
                    HF_ERROR_MODULE_LOAD_EXCEPTION);
            } catch (...) {
                loadRaisedException = true;
                Diagnostics::ReportFrameworkFailureForModule(
                    moduleName,
                    moduleLogger,
                    HF_ERROR_MODULE_LOAD_EXCEPTION);
            }
        }

        if (loaded != HF_TRUE) {
            if (!loadRaisedException) {
                Diagnostics::ReportFrameworkFailureForModule(
                    moduleName,
                    moduleLogger,
                    HF_ERROR_MODULE_REJECTED);
            }
            if (unload) {
                ModuleContext::Scope scope{ moduleName.c_str(), moduleLogger };
                try {
                    unload();
                } catch (...) {
                    Diagnostics::ReportFrameworkFailureForModule(
                        moduleName,
                        moduleLogger,
                        HF_ERROR_MODULE_UNLOAD_EXCEPTION);
                }
            }
            const auto removedSubscriptions =
                EventBus::GetSingleton().UnsubscribeOwnedBy(moduleName);
            const auto removedRenderSubscriptions =
                RenderPipelineService::GetSingleton().UnsubscribeOwnedBy(moduleName);
            const auto removedPresentationSubscriptions =
                PresentationService::GetSingleton().UnsubscribeOwnedBy(moduleName);
            const auto removedUIMenuSubscriptions =
                UIStateService::GetSingleton().UnsubscribeMenuEventsOwnedBy(moduleName);
            const auto removedUIPolicies =
                UIStateService::GetSingleton().ReleaseLoadingMenuPoliciesOwnedBy(moduleName);
            const auto releasedPresentationPolicies =
                PresentationPolicyService::GetSingleton().ReleaseOwnedBy(moduleName);
            const auto releasedStateFPSPolicies =
                StateFPSService::GetSingleton().ReleaseOwnedBy(moduleName);
            const auto releasedCPUScheduling =
                CPUSchedulingService::GetSingleton().ReleaseOwnedBy(moduleName);
            const auto releasedRuntimeTuning =
                RuntimeTuningService::GetSingleton().ReleaseOwnedBy(moduleName);
            const auto releasedFrameLimits =
                FramePacingService::GetSingleton().ReleaseOwnedBy(moduleName);
            const auto releasedCursorClips =
                WindowService::GetSingleton().ReleaseOwnedBy(moduleName);
            const auto releasedActorAdjustments =
                ReferenceActorService::GetSingleton().ReleaseAdjustmentsOwnedBy(moduleName);
            const auto closedConfigDocuments =
                ConfigManager::GetSingleton().CloseDocumentsOwnedBy(moduleName);
            const auto removedMovementSubscriptions =
                PlayerMovementService::GetSingleton().UnsubscribeOwnedBy(moduleName);
            const auto destroyedPostProcessEffects =
                PostProcessService::GetSingleton().DestroyOwnedBy(moduleName);
            const auto canceledTasks =
                TaskScheduler::GetSingleton().CancelOwnedBy(moduleName);
            const auto restoredHooks =
                HookManager::GetSingleton().RestoreOwnedBy(moduleName);
            const auto restoredPatches =
                MemoryManager::GetSingleton().RestoreOwnedBy(moduleName);
            const auto removedRegistryEntries =
                ResourceRegistry::GetSingleton().RemoveOwnedBy(moduleName);
            const auto removedSerializationRecords =
                SerializationService::GetSingleton().ClearRecordsOwned(moduleName);
            const auto releasedGameSettings =
                GameSettingsManager::GetSingleton().ReleaseOwnedBy(moduleName);
            PerformanceMonitor::GetSingleton().RemoveOwnedBy(moduleName);
            if (removedSubscriptions != 0 || removedRenderSubscriptions != 0 || removedPresentationSubscriptions != 0 || removedUIMenuSubscriptions != 0 || removedUIPolicies != 0 || releasedPresentationPolicies != 0 || releasedStateFPSPolicies != 0 || releasedCPUScheduling != 0 || releasedRuntimeTuning != 0 || releasedFrameLimits != 0 || releasedCursorClips != 0 || releasedActorAdjustments != 0 || closedConfigDocuments != 0 || removedMovementSubscriptions != 0 || destroyedPostProcessEffects != 0 || canceledTasks != 0 || restoredHooks != 0 || restoredPatches != 0 || removedRegistryEntries != 0 || removedSerializationRecords != 0 || releasedGameSettings != 0) {
                REX::INFO(
                    "Cleaned rejected module '{}': {} event subscription(s), {} render-stage subscription(s), {} presentation subscription(s), {} UI-menu subscription(s), {} UI policy ownership record(s), {} presentation-policy lease(s), {} state-FPS policy lease(s), {} CPU scheduling lease(s), {} runtime-tuning lease(s), {} frame-limit ownership record(s), {} cursor-confinement lease(s), {} actor-value adjustment(s), {} config document(s), {} movement subscription(s), {} post-process effect(s), {} task(s), {} hook(s), {} patch(es) restored, {} capability/resource entry(s), {} serialization record(s), {} live setting ownership record(s) released",
                    moduleName,
                    removedSubscriptions,
                    removedRenderSubscriptions,
                    removedPresentationSubscriptions,
                    removedUIMenuSubscriptions,
                    removedUIPolicies,
                    releasedPresentationPolicies,
                    releasedStateFPSPolicies,
                    releasedCPUScheduling,
                    releasedRuntimeTuning,
                    releasedFrameLimits,
                    releasedCursorClips,
                    releasedActorAdjustments,
                    closedConfigDocuments,
                    removedMovementSubscriptions,
                    destroyedPostProcessEffects,
                    canceledTasks,
                    restoredHooks,
                    restoredPatches,
                    removedRegistryEntries,
                    removedSerializationRecords,
                    releasedGameSettings);
            }

            const auto remainingHooks =
                HookManager::GetSingleton().GetUnrestoredCountOwnedBy(moduleName);
            const auto remainingPatches =
                MemoryManager::GetSingleton().GetUnrestoredCountOwnedBy(moduleName);
            if (remainingHooks != 0 || remainingPatches != 0) {
                Diagnostics::ReportFrameworkFailureForModule(
                    moduleName,
                    moduleLogger,
                    HF_ERROR_MODULE_QUARANTINED);
                {
                    std::scoped_lock lock{ _lock };
                    std::erase_if(_provisionalModules, [handle](const ProvisionalModule& module) {
                        return module.handle == handle;
                    });
                    _quarantinedModules.push_back(QuarantinedModule{
                        .handle = handle,
                        .name = moduleName,
                        .logger = moduleLogger
                    });
                }
                REX::WARN(
                    "Rejected module '{}' quarantined in memory; {} hook(s) and {} patch(es) remain unresolved",
                    moduleName,
                    remainingHooks,
                    remainingPatches);
                ModuleLogManager::GetSingleton().Flush(moduleLogger);
                return false;
            }

            {
                std::scoped_lock lock{ _lock };
                std::erase_if(_provisionalModules, [handle](const ProvisionalModule& module) {
                    return module.handle == handle;
                });
            }
            ConfigManager::GetSingleton().UnregisterModule(moduleName);
            UnregisterModuleErrorCatalog(moduleName);
            ModuleLogManager::GetSingleton().Close(moduleLogger);
            REX::W32::FreeLibrary(handle);
            return false;
        }

        {
            std::scoped_lock lock{ _lock };
            HF_ModuleHealth health = HF_MODULE_HEALTH_HEALTHY;
            HF_ErrorCode lastError = HF_ERROR_NONE;
            std::string lastErrorPrefix;
            if (const auto it = std::ranges::find_if(_provisionalModules, [handle](const ProvisionalModule& module) {
                    return module.handle == handle;
                }); it != _provisionalModules.end()) {
                health = it->health;
                lastError = it->lastError;
                lastErrorPrefix = it->lastErrorPrefix;
            }
            std::erase_if(_provisionalModules, [handle](const ProvisionalModule& module) {
                return module.handle == handle;
            });
            _modules.push_back(LoadedModule{
                .handle = handle,
                .name = moduleName,
                .version = info->version,
                .logger = moduleLogger,
                .health = health,
                .lastError = lastError,
                .lastErrorPrefix = std::move(lastErrorPrefix)
            });
        }

        ModuleLogManager::GetSingleton().Write(moduleLogger, HF_LOG_INFO, "Module initialized successfully");
        REX::INFO("Loaded native module '{}' v{}.{}.{}.{}",
            moduleName,
            info->version.major,
            info->version.minor,
            info->version.patch,
            info->version.build);
        return true;
    }

    std::uint32_t ModuleLoader::GetLoadedCount() const noexcept
    {
        std::scoped_lock lock{ _lock };
        return static_cast<std::uint32_t>(_modules.size());
    }

    std::uint32_t ModuleLoader::GetHealthyCount() const noexcept
    {
        std::scoped_lock lock{ _lock };
        return static_cast<std::uint32_t>(std::ranges::count_if(_modules, [](const LoadedModule& a_module) {
            return a_module.health == HF_MODULE_HEALTH_HEALTHY;
        }));
    }

    std::uint32_t ModuleLoader::GetDegradedCount() const noexcept
    {
        std::scoped_lock lock{ _lock };
        return static_cast<std::uint32_t>(std::ranges::count_if(_modules, [](const LoadedModule& a_module) {
            return a_module.health == HF_MODULE_HEALTH_DEGRADED;
        }));
    }

    bool ModuleLoader::NamesEqual(const std::string_view a_left, const std::string_view a_right) noexcept
    {
        if (a_left.size() != a_right.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a_left.size(); ++i) {
            const auto left = static_cast<unsigned char>(a_left[i]);
            const auto right = static_cast<unsigned char>(a_right[i]);
            if (std::tolower(left) != std::tolower(right)) {
                return false;
            }
        }
        return true;
    }

    void ModuleLoader::FillRecord(const LoadedModule& a_module, HF_ModuleRecordV2& a_out) noexcept
    {
        a_out = {};
        a_out.structSize = sizeof(HF_ModuleRecordV2);
        a_out.version = a_module.version;
        a_out.health = a_module.health;
        a_out.lastError = a_module.lastError;
        std::snprintf(a_out.name, sizeof(a_out.name), "%s", a_module.name.c_str());

        const auto prefix = ResolveModuleErrorPrefix(a_module.name);
        std::snprintf(a_out.prefix, sizeof(a_out.prefix), "%s", prefix.c_str());
        if (a_module.lastError != HF_ERROR_NONE && !a_module.lastErrorPrefix.empty()) {
            std::snprintf(
                a_out.lastErrorPrefix,
                sizeof(a_out.lastErrorPrefix),
                "%s",
                a_module.lastErrorPrefix.c_str());
        }
    }

    bool ModuleLoader::GetRecordByIndex(const std::uint32_t a_index, HF_ModuleRecordV2& a_out) const noexcept
    {
        std::scoped_lock lock{ _lock };
        if (a_index >= _modules.size()) {
            return false;
        }
        FillRecord(_modules[a_index], a_out);
        return true;
    }

    bool ModuleLoader::FindRecordByName(const std::string_view a_name, HF_ModuleRecordV2& a_out) const noexcept
    {
        if (a_name.empty()) {
            return false;
        }

        std::scoped_lock lock{ _lock };
        for (const auto& module : _modules) {
            if (NamesEqual(module.name, a_name)) {
                FillRecord(module, a_out);
                return true;
            }
        }
        return false;
    }


    bool ModuleLoader::FindExecutionIdentityByCodeAddress(
        const void* const a_address,
        std::string& a_moduleName,
        HF_LogHandle& a_logger) const noexcept
    {
        if (!a_address) {
            return false;
        }

        HMODULE ownerModule{};
        if (!::GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(a_address),
                &ownerModule) ||
            !ownerModule) {
            return false;
        }

        std::scoped_lock lock{ _lock };
        for (const auto& module : _modules) {
            if (reinterpret_cast<void*>(module.handle) == reinterpret_cast<void*>(ownerModule)) {
                a_moduleName = module.name;
                a_logger = module.logger;
                return true;
            }
        }
        for (const auto& module : _provisionalModules) {
            if (reinterpret_cast<void*>(module.handle) == reinterpret_cast<void*>(ownerModule)) {
                a_moduleName = module.name;
                a_logger = module.logger;
                return true;
            }
        }
        for (const auto& module : _quarantinedModules) {
            if (reinterpret_cast<void*>(module.handle) == reinterpret_cast<void*>(ownerModule)) {
                a_moduleName = module.name;
                a_logger = module.logger;
                return true;
            }
        }
        return false;
    }

    void ModuleLoader::MarkDegraded(
        const std::string_view a_moduleName,
        const std::string_view a_errorPrefix,
        const HF_ErrorCode a_code) noexcept
    {
        if (a_moduleName.empty() || a_errorPrefix.empty() || a_code == HF_ERROR_NONE) {
            return;
        }

        std::scoped_lock lock{ _lock };
        for (auto& module : _modules) {
            if (NamesEqual(module.name, a_moduleName)) {
                module.health = HF_MODULE_HEALTH_DEGRADED;
                module.lastError = a_code;
                module.lastErrorPrefix = a_errorPrefix;
                return;
            }
        }
        for (auto& module : _provisionalModules) {
            if (NamesEqual(module.name, a_moduleName)) {
                module.health = HF_MODULE_HEALTH_DEGRADED;
                module.lastError = a_code;
                module.lastErrorPrefix = a_errorPrefix;
                return;
            }
        }
    }
}
