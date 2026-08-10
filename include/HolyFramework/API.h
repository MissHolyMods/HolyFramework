#pragma once

// HolyFramework public ABI.
// IMPORTANT: Keep this header independent from CommonLibF4, F4SE, RE, REL and REX.

#include <cstddef>
#include <cstdint>

#include <HolyFramework/Errors.h>

#if defined(_WIN32)
#    define HF_CALL __cdecl
#    define HF_MODULE_EXPORT extern "C" __declspec(dllexport)
#    if defined(HOLYFRAMEWORK_BUILD)
#        define HF_FRAMEWORK_EXPORT extern "C" __declspec(dllexport)
#    else
#        define HF_FRAMEWORK_EXPORT extern "C"
#    endif
#else
#    define HF_CALL
#    define HF_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#    define HF_FRAMEWORK_EXPORT extern "C"
#endif

using HF_Bool = std::uint32_t;
using HF_SubscriptionHandle = std::uint64_t;
using HF_LogHandle = std::uint64_t;
using HF_TaskHandle = std::uint64_t;
using HF_Address = std::uint64_t;
using HF_PatchHandle = std::uint64_t;
using HF_HookHandle = std::uint64_t;
using HF_CodeBlockHandle = std::uint64_t;
using HF_ResourceHandle = std::uint64_t;
using HF_PerfSampleHandle = std::uint64_t;
using HF_FormHandle = std::uint64_t;
using HF_RenderSubscriptionHandle = std::uint64_t;
using HF_UIMenuSubscriptionHandle = std::uint64_t;
using HF_PresentationSubscriptionHandle = std::uint64_t;
using HF_UIPolicyHandle = std::uint64_t;
using HF_FrameLimitHandle = std::uint64_t;
using HF_CursorClipHandle = std::uint64_t;
using HF_PresentationPolicyHandle = std::uint64_t;
using HF_StateFPSPolicyHandle = std::uint64_t;
using HF_CPUSchedulingHandle = std::uint64_t;
using HF_RuntimeTuningHandle = std::uint64_t;
using HF_ConfigDocumentHandle = std::uint64_t;
using HF_ActorValueAdjustmentHandle = std::uint64_t;
using HF_PlayerMovementSubscriptionHandle = std::uint64_t;
using HF_PostProcessEffectHandle = std::uint64_t;

inline constexpr HF_Bool HF_FALSE = 0;
inline constexpr HF_Bool HF_TRUE = 1;
inline constexpr HF_LogHandle HF_INVALID_LOG_HANDLE = 0;
inline constexpr HF_TaskHandle HF_INVALID_TASK_HANDLE = 0;
inline constexpr HF_PatchHandle HF_INVALID_PATCH_HANDLE = 0;
inline constexpr HF_HookHandle HF_INVALID_HOOK_HANDLE = 0;
inline constexpr HF_CodeBlockHandle HF_INVALID_CODE_BLOCK_HANDLE = 0;
inline constexpr HF_ResourceHandle HF_INVALID_RESOURCE_HANDLE = 0;
inline constexpr HF_PerfSampleHandle HF_INVALID_PERF_SAMPLE_HANDLE = 0;
inline constexpr HF_FormHandle HF_INVALID_FORM_HANDLE = 0;
inline constexpr HF_RenderSubscriptionHandle HF_INVALID_RENDER_SUBSCRIPTION_HANDLE = 0;
inline constexpr HF_UIMenuSubscriptionHandle HF_INVALID_UI_MENU_SUBSCRIPTION_HANDLE = 0;
inline constexpr HF_PresentationSubscriptionHandle HF_INVALID_PRESENTATION_SUBSCRIPTION_HANDLE = 0;
inline constexpr HF_UIPolicyHandle HF_INVALID_UI_POLICY_HANDLE = 0;
inline constexpr HF_FrameLimitHandle HF_INVALID_FRAME_LIMIT_HANDLE = 0;
inline constexpr HF_CursorClipHandle HF_INVALID_CURSOR_CLIP_HANDLE = 0;
inline constexpr HF_PresentationPolicyHandle HF_INVALID_PRESENTATION_POLICY_HANDLE = 0;
inline constexpr HF_StateFPSPolicyHandle HF_INVALID_STATE_FPS_POLICY_HANDLE = 0;
inline constexpr HF_CPUSchedulingHandle HF_INVALID_CPU_SCHEDULING_HANDLE = 0;
inline constexpr HF_RuntimeTuningHandle HF_INVALID_RUNTIME_TUNING_HANDLE = 0;
inline constexpr HF_ConfigDocumentHandle HF_INVALID_CONFIG_DOCUMENT_HANDLE = 0;
inline constexpr HF_ActorValueAdjustmentHandle HF_INVALID_ACTOR_VALUE_ADJUSTMENT_HANDLE = 0;
inline constexpr HF_PlayerMovementSubscriptionHandle HF_INVALID_PLAYER_MOVEMENT_SUBSCRIPTION_HANDLE = 0;
inline constexpr HF_PostProcessEffectHandle HF_INVALID_POST_PROCESS_EFFECT_HANDLE = 0;
inline constexpr std::uint32_t HF_ABI_VERSION = 3;

// Simple HolyFramework module marker. This is intentionally not DRM or
// cryptographic authentication; it only prevents unrelated DLLs from being
// loaded accidentally from Data/F4SE/Plugins/HolyFramework.
inline constexpr std::uint32_t HF_MODULE_SIGNATURE_VERSION = 1;
inline constexpr char HF_MODULE_SIGNATURE_TEXT[] = "HolyFramework signature";

struct HF_ModuleSignatureV1
{
    std::uint32_t structSize;
    std::uint32_t signatureVersion;
    char signatureText[32];
    char errorPrefix[4];
};

static_assert(sizeof(HF_ModuleSignatureV1) == 44);

#define HF_DECLARE_MODULE_SIGNATURE(PREFIX3) \
    static_assert(sizeof(PREFIX3) == 4, "HolyFramework module error prefix must be exactly 3 characters"); \
    static_assert((PREFIX3)[0] >= 'A' && (PREFIX3)[0] <= 'Z' && \
                  (PREFIX3)[1] >= 'A' && (PREFIX3)[1] <= 'Z' && \
                  (PREFIX3)[2] >= 'A' && (PREFIX3)[2] <= 'Z', \
                  "HolyFramework module error prefix must use uppercase ASCII A-Z"); \
    static_assert(!((PREFIX3)[0] == 'H' && (PREFIX3)[1] == 'F' && (PREFIX3)[2] == 'W'), \
                  "HFW is reserved for HolyFramework"); \
    HF_MODULE_EXPORT const HF_ModuleSignatureV1 HF_HolyFrameworkSignature = { \
        sizeof(HF_ModuleSignatureV1), HF_MODULE_SIGNATURE_VERSION, "HolyFramework signature", PREFIX3 \
    }

struct HF_Version
{
    std::uint16_t major;
    std::uint16_t minor;
    std::uint16_t patch;
    std::uint16_t build;
};

static_assert(sizeof(HF_Version) == 8);

enum HF_InterfaceID : std::uint32_t
{
    HF_INTERFACE_CORE = 1,
    HF_INTERFACE_LOG = 2,
    HF_INTERFACE_EVENTS = 3,
    HF_INTERFACE_UI = 4,
    HF_INTERFACE_DIAGNOSTICS = 5,
    HF_INTERFACE_RUNTIME = 6,
    HF_INTERFACE_MODULES = 7,
    HF_INTERFACE_TASKS = 8,
    HF_INTERFACE_MEMORY = 9,
    HF_INTERFACE_HOOKS = 10,
    HF_INTERFACE_CAPABILITIES = 11,
    HF_INTERFACE_RESOURCES = 12,
    HF_INTERFACE_PERFORMANCE = 13,
    HF_INTERFACE_CONFIG = 14,
    HF_INTERFACE_GAME = 15,
    HF_INTERFACE_GAME_SETTINGS = 16,
    HF_INTERFACE_FORMS = 17,
    HF_INTERFACE_REFERENCES = 18,
    HF_INTERFACE_ACTORS = 19,
    HF_INTERFACE_PLAYER = 20,
    HF_INTERFACE_GRAPHICS = 21,
    HF_INTERFACE_GAME_TIME = 22,
    HF_INTERFACE_ENVIRONMENT = 23,
    HF_INTERFACE_LIGHTING = 24,
    HF_INTERFACE_RENDER_PIPELINE = 25,
    HF_INTERFACE_SERIALIZATION = 26,
    HF_INTERFACE_PRESENTATION = 27,
    HF_INTERFACE_FRAME_TIMING = 28,
    HF_INTERFACE_FRAME_PACING = 29,
    HF_INTERFACE_WINDOW = 30,
    HF_INTERFACE_DISPLAY = 31,
    HF_INTERFACE_PRESENTATION_POLICY = 32,
    HF_INTERFACE_STATE_FPS = 33,
    HF_INTERFACE_CPU_SCHEDULING = 34,
    HF_INTERFACE_RUNTIME_TUNING = 35,
    HF_INTERFACE_CONFIG_DOCUMENTS = 36,
    HF_INTERFACE_PLAYER_MOVEMENT = 37,
    HF_INTERFACE_POST_PROCESS = 38
};

enum HF_LogLevel : std::uint32_t
{
    HF_LOG_TRACE = 0,
    HF_LOG_DEBUG = 1,
    HF_LOG_INFO = 2,
    HF_LOG_WARNING = 3,
    HF_LOG_ERROR = 4,
    HF_LOG_CRITICAL = 5
};

enum HF_Event : std::uint32_t
{
    HF_EVENT_POST_LOAD = 1,
    HF_EVENT_POST_POST_LOAD = 2,
    HF_EVENT_PRE_LOAD_GAME = 3,
    HF_EVENT_POST_LOAD_GAME = 4,
    HF_EVENT_PRE_SAVE_GAME = 5,
    HF_EVENT_POST_SAVE_GAME = 6,
    HF_EVENT_DELETE_GAME = 7,
    // F4SE lifecycle notification. Movement-state observation is exposed
    // separately through HF_PlayerMovementV1 when a module explicitly subscribes.
    HF_EVENT_INPUT_LOADED = 8,
    HF_EVENT_NEW_GAME = 9,
    HF_EVENT_GAME_LOADED = 10,
    HF_EVENT_GAME_DATA_READY = 11,
    // HolyFramework synthetic event: dispatched on the game task queue after
    // a session has been active for the framework startup grace period.
    HF_EVENT_SESSION_READY = 12,
    // Synthetic HolyFramework event. Emitted after Fallout reports that a menu
    // was opened or closed. No internal menu pointer crosses the public ABI; query
    // HF_UI/HF_GameV1 for the current state.
    HF_EVENT_UI_STATE_CHANGED = 13
};

using HF_QueryInterfaceFn = const void*(HF_CALL*)(std::uint32_t a_interfaceID, std::uint32_t a_interfaceVersion);

struct HF_API
{
    std::uint32_t structSize;
    std::uint32_t abiVersion;
    HF_QueryInterfaceFn QueryInterface;
};

inline constexpr std::uint32_t HF_CORE_INTERFACE_VERSION = 1;

struct HF_CoreV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    const char*(HF_CALL* GetFrameworkName)();
    HF_Version(HF_CALL* GetFrameworkVersion)();
    HF_Version(HF_CALL* GetRuntimeVersion)();
    HF_Version(HF_CALL* GetF4SEVersion)();
    HF_Bool(HF_CALL* IsReady)();
    std::uint32_t(HF_CALL* GetLoadedModuleCount)();
};

inline constexpr std::uint32_t HF_LOG_INTERFACE_VERSION = 2;

struct HF_Log
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    HF_LogHandle(HF_CALL* GetCurrentLogger)();
    HF_LogHandle(HF_CALL* Open)(const char* a_moduleName);
    void(HF_CALL* Write)(HF_LogHandle a_logger, HF_LogLevel a_level, const char* a_message);
    void(HF_CALL* Flush)(HF_LogHandle a_logger);
};

using HF_EventCallback = void(HF_CALL*)(HF_Event a_event, void* a_userData);

inline constexpr std::uint32_t HF_EVENTS_INTERFACE_VERSION = 1;

struct HF_EventsV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    HF_SubscriptionHandle(HF_CALL* Subscribe)(HF_Event a_event, HF_EventCallback a_callback, void* a_userData);
    HF_Bool(HF_CALL* Unsubscribe)(HF_SubscriptionHandle a_handle);
};

enum HF_UIStateFlags : std::uint32_t
{
    HF_UI_STATE_NONE = 0,
    HF_UI_STATE_AVAILABLE = 1u << 0,
    HF_UI_STATE_ANY_MENU_OPEN = 1u << 1,
    HF_UI_STATE_LOADING_MENU_OPEN = 1u << 2,
    HF_UI_STATE_MAIN_MENU_OPEN = 1u << 3,
    HF_UI_STATE_PAUSE_MENU_OPEN = 1u << 4,
    HF_UI_STATE_PIPBOY_MENU_OPEN = 1u << 5,
    HF_UI_STATE_CONSOLE_OPEN = 1u << 6,
    HF_UI_STATE_DIALOGUE_MENU_OPEN = 1u << 7,
    HF_UI_STATE_CONTAINER_MENU_OPEN = 1u << 8,
    HF_UI_STATE_BARTER_MENU_OPEN = 1u << 9,
    HF_UI_STATE_WORKSHOP_MENU_OPEN = 1u << 10,
    HF_UI_STATE_TERMINAL_MENU_OPEN = 1u << 11,
    HF_UI_STATE_LOCKPICKING_MENU_OPEN = 1u << 12
};

inline constexpr std::uint32_t HF_UI_MENU_NAME_CAPACITY = 128;

struct HF_UIMenuEventV1
{
    std::uint32_t structSize;
    std::uint32_t stateFlags;
    std::uint64_t sequence;
    std::uint64_t sessionGeneration;
    HF_Bool opening;
    std::uint32_t reserved;
    char menuName[HF_UI_MENU_NAME_CAPACITY];
};

static_assert(sizeof(HF_UIMenuEventV1) == 160);

using HF_UIMenuEventCallback = void(HF_CALL*)(const HF_UIMenuEventV1* a_event, void* a_userData);

enum HF_LoadingMenuPolicyFlags : std::uint32_t
{
    HF_LOADING_MENU_POLICY_NONE = 0,
    // Prevents the 3D loading model from auto/manual rotation and clears the
    // transient mouse/stick rotation state while the LoadingMenu is open.
    HF_LOADING_MENU_POLICY_DISABLE_MODEL_INTERACTION = 1u << 0
};

enum HF_LoadingMenuStateFlags : std::uint32_t
{
    HF_LOADING_MENU_STATE_NONE = 0,
    HF_LOADING_MENU_STATE_AVAILABLE = 1u << 0,
    HF_LOADING_MENU_STATE_OPEN = 1u << 1,
    HF_LOADING_MENU_STATE_POLICY_ACTIVE = 1u << 2,
    HF_LOADING_MENU_STATE_AUTO_ROTATE = 1u << 3,
    HF_LOADING_MENU_STATE_ALLOW_ROTATION = 1u << 4,
    HF_LOADING_MENU_STATE_LEFT_BUTTON_DOWN = 1u << 5,
    HF_LOADING_MENU_STATE_RIGHT_BUTTON_DOWN = 1u << 6,
    HF_LOADING_MENU_STATE_LEFT_STICK_READY = 1u << 7,
    HF_LOADING_MENU_STATE_RIGHT_STICK_READY = 1u << 8
};

struct HF_LoadingMenuStateV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    std::uint32_t activePolicyFlags;
    std::uint32_t reserved;
};

static_assert(sizeof(HF_LoadingMenuStateV1) == 16);

inline constexpr std::uint32_t HF_UI_INTERFACE_VERSION = 4;

struct HF_UI
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    HF_Bool(HF_CALL* ShowNotification)(const char* a_message, HF_Bool a_warning);
    HF_Bool(HF_CALL* IsMenuOpen)(const char* a_menuName);
    std::uint32_t(HF_CALL* GetStateFlags)();
    HF_Bool(HF_CALL* HasState)(std::uint32_t a_requiredFlags);

    // Exact menu open/close notifications. a_menuNameFilter may be null or empty
    // to receive all menus, otherwise only the named Fallout menu is delivered.
    // Callbacks are marshalled through HolyFramework's game-task path; no game
    // menu object crosses the public ABI.
    HF_UIMenuSubscriptionHandle(HF_CALL* SubscribeMenuEvents)(
        const char* a_menuNameFilter,
        HF_UIMenuEventCallback a_callback,
        void* a_userData);
    HF_Bool(HF_CALL* UnsubscribeMenuEvents)(HF_UIMenuSubscriptionHandle a_handle);

    // LoadingMenu policy is owner-tracked. Multiple modules may request the same
    // restrictive policy; HolyFramework restores the captured menu state when
    // the last owner releases it or is unloaded.
    HF_Bool(HF_CALL* GetLoadingMenuState)(HF_LoadingMenuStateV1* a_outState);
    HF_UIPolicyHandle(HF_CALL* AcquireLoadingMenuPolicy)(std::uint32_t a_policyFlags);
    HF_Bool(HF_CALL* ReleaseLoadingMenuPolicy)(HF_UIPolicyHandle a_handle);
};

enum HF_GameStateFlags : std::uint32_t
{
    HF_GAME_STATE_NONE = 0,
    HF_GAME_STATE_UI_AVAILABLE = 1u << 0,
    HF_GAME_STATE_SESSION_ACTIVE = 1u << 1,
    HF_GAME_STATE_SESSION_READY = 1u << 2,
    HF_GAME_STATE_LOADING = 1u << 3,
    HF_GAME_STATE_MAIN_MENU = 1u << 4,
    HF_GAME_STATE_PAUSED = 1u << 5,
    HF_GAME_STATE_IN_GAME = 1u << 6
};

inline constexpr std::uint32_t HF_GAME_INTERFACE_VERSION = 1;

struct HF_GameV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    // Aggregated, read-only game state. "Paused" means at least one active
    // menu is marked by Fallout with UI_MENU_FLAGS::kPausesGame.
    std::uint32_t(HF_CALL* GetStateFlags)();
    HF_Bool(HF_CALL* HasState)(std::uint32_t a_requiredFlags);
    HF_Bool(HF_CALL* IsPaused)();
    HF_Bool(HF_CALL* IsLoading)();
    HF_Bool(HF_CALL* IsInGame)();
};

// Live Fallout settings, not <Module>.ini configuration. HF_GAME_SETTING_SOURCE_FALLOUT_INI maps
// to Fallout/F4SE-visible INI settings such as "iFPSClamp:General" while
// HF_GAME_SETTING_SOURCE_GAME maps to GameSettingCollection entries (GMST).
enum HF_GameSettingSource : std::uint32_t
{
    HF_GAME_SETTING_SOURCE_FALLOUT_INI = 1,
    HF_GAME_SETTING_SOURCE_GAME = 2
};

enum HF_GameSettingType : std::uint32_t
{
    HF_GAME_SETTING_TYPE_UNKNOWN = 0,
    HF_GAME_SETTING_TYPE_BOOL = 1,
    HF_GAME_SETTING_TYPE_INT32 = 2,
    HF_GAME_SETTING_TYPE_UINT32 = 3,
    HF_GAME_SETTING_TYPE_FLOAT = 4,
    HF_GAME_SETTING_TYPE_STRING = 5,
    HF_GAME_SETTING_TYPE_CHAR = 6,
    HF_GAME_SETTING_TYPE_UCHAR = 7
};

inline constexpr std::uint32_t HF_GAME_SETTINGS_INTERFACE_VERSION = 1;

struct HF_GameSettingsV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    HF_Bool(HF_CALL* Exists)(HF_GameSettingSource a_source, const char* a_name);
    HF_GameSettingType(HF_CALL* GetType)(HF_GameSettingSource a_source, const char* a_name);
    HF_Bool(HF_CALL* GetBool)(HF_GameSettingSource a_source, const char* a_name, HF_Bool* a_outValue);
    // CHAR/UCHAR settings are widened through the Int32/UInt32 accessors.
    HF_Bool(HF_CALL* GetInt32)(HF_GameSettingSource a_source, const char* a_name, std::int32_t* a_outValue);
    HF_Bool(HF_CALL* GetUInt32)(HF_GameSettingSource a_source, const char* a_name, std::uint32_t* a_outValue);
    HF_Bool(HF_CALL* GetFloat)(HF_GameSettingSource a_source, const char* a_name, float* a_outValue);
    HF_Bool(HF_CALL* GetString)(HF_GameSettingSource a_source, const char* a_name, char* a_buffer, std::uint32_t a_bufferSize);

    // Numeric writes are tracked per module. The first writer owns the setting
    // until Release(), allowing repeated updates while preventing two
    // HolyFramework modules from silently fighting over the same live value.
    HF_Bool(HF_CALL* SetBool)(HF_GameSettingSource a_source, const char* a_name, HF_Bool a_value);
    HF_Bool(HF_CALL* SetInt32)(HF_GameSettingSource a_source, const char* a_name, std::int32_t a_value);
    HF_Bool(HF_CALL* SetUInt32)(HF_GameSettingSource a_source, const char* a_name, std::uint32_t a_value);
    HF_Bool(HF_CALL* SetFloat)(HF_GameSettingSource a_source, const char* a_name, float a_value);
    HF_Bool(HF_CALL* Release)(HF_GameSettingSource a_source, const char* a_name, HF_Bool a_restoreOriginal);
};

struct HF_FormInfoV1
{
    std::uint32_t structSize;
    std::uint32_t formID;
    std::uint32_t formType;
    std::uint32_t formFlags;
    char typeCode[8];
    char editorID[128];
};

inline constexpr std::uint32_t HF_FORMS_INTERFACE_VERSION = 1;

struct HF_FormsV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    // Lookups are available after GAME_DATA_READY. No TESForm pointer crosses
    // the ABI. Static/load-order forms receive process-stable handles; runtime
    // FFxxxxxx forms are session-generation scoped and become invalid after a
    // save/new-game transition.
    HF_FormHandle(HF_CALL* LookupByID)(std::uint32_t a_formID);
    HF_FormHandle(HF_CALL* LookupByEditorID)(const char* a_editorID);
    HF_Bool(HF_CALL* IsValid)(HF_FormHandle a_handle);
    HF_Bool(HF_CALL* GetInfo)(HF_FormHandle a_handle, HF_FormInfoV1* a_outInfo);
    HF_Bool(HF_CALL* IsType)(HF_FormHandle a_handle, const char* a_typeCode);
};

struct HF_Vector3
{
    float x;
    float y;
    float z;
};

enum HF_ReferenceStateFlags : std::uint32_t
{
    HF_REFERENCE_STATE_NONE = 0,
    HF_REFERENCE_STATE_VALID = 1u << 0,
    HF_REFERENCE_STATE_DISABLED = 1u << 1,
    HF_REFERENCE_STATE_DELETED = 1u << 2,
    HF_REFERENCE_STATE_ACTOR = 1u << 3,
    HF_REFERENCE_STATE_PLAYER = 1u << 4
};

inline constexpr std::uint32_t HF_REFERENCES_INTERFACE_VERSION = 2;

struct HF_ReferencesV2
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    HF_Bool(HF_CALL* IsReference)(HF_FormHandle a_handle);
    std::uint32_t(HF_CALL* GetStateFlags)(HF_FormHandle a_handle);
    HF_Bool(HF_CALL* GetPosition)(HF_FormHandle a_handle, HF_Vector3* a_outPosition);
    HF_FormHandle(HF_CALL* GetBaseForm)(HF_FormHandle a_handle);
    HF_FormHandle(HF_CALL* GetParentCell)(HF_FormHandle a_handle);

    // Copies Fallout's current display/full name for a reference. A successful
    // empty name is valid and writes an empty string.
    HF_Bool(HF_CALL* GetDisplayName)(
        HF_FormHandle a_handle,
        char* a_buffer,
        std::uint32_t a_bufferSize);
};

enum HF_ActorStateFlags : std::uint32_t
{
    HF_ACTOR_STATE_NONE = 0,
    HF_ACTOR_STATE_VALID = 1u << 0,
    HF_ACTOR_STATE_DEAD = 1u << 1,
    HF_ACTOR_STATE_IN_COMBAT = 1u << 2,
    HF_ACTOR_STATE_SNEAKING = 1u << 3,
    HF_ACTOR_STATE_VISIBLE = 1u << 4,
    HF_ACTOR_STATE_PLAYER = 1u << 5
};

enum HF_ActorValue : std::uint32_t
{
    HF_ACTOR_VALUE_HEALTH = 1,
    HF_ACTOR_VALUE_ACTION_POINTS = 2,
    HF_ACTOR_VALUE_RADS = 3,
    HF_ACTOR_VALUE_FATIGUE = 4,
    HF_ACTOR_VALUE_SPEED_MULT = 5,
    HF_ACTOR_VALUE_CARRY_WEIGHT = 6,
    HF_ACTOR_VALUE_STRENGTH = 7,
    HF_ACTOR_VALUE_PERCEPTION = 8,
    HF_ACTOR_VALUE_ENDURANCE = 9,
    HF_ACTOR_VALUE_CHARISMA = 10,
    HF_ACTOR_VALUE_INTELLIGENCE = 11,
    HF_ACTOR_VALUE_AGILITY = 12,
    HF_ACTOR_VALUE_LUCK = 13,
    HF_ACTOR_VALUE_DAMAGE_RESISTANCE = 14,
    HF_ACTOR_VALUE_ENERGY_RESISTANCE = 15,
    HF_ACTOR_VALUE_RAD_EXPOSURE_RESISTANCE = 16,
    HF_ACTOR_VALUE_RAD_INGESTION_RESISTANCE = 17,
    HF_ACTOR_VALUE_POWER_ARMOR_BATTERY = 18,
    HF_ACTOR_VALUE_STAMINA = 19
};

enum HF_ActorValueModifier : std::uint32_t
{
    HF_ACTOR_VALUE_MODIFIER_PERMANENT = 0,
    HF_ACTOR_VALUE_MODIFIER_TEMPORARY = 1,
    HF_ACTOR_VALUE_MODIFIER_DAMAGE = 2
};

inline constexpr std::uint32_t HF_ACTORS_INTERFACE_VERSION = 3;

struct HF_ActorsV3
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    HF_Bool(HF_CALL* IsActor)(HF_FormHandle a_handle);
    std::uint32_t(HF_CALL* GetStateFlags)(HF_FormHandle a_handle);
    HF_FormHandle(HF_CALL* GetBaseActor)(HF_FormHandle a_handle);

    // Convenience accessors for common Fallout actor values.
    HF_Bool(HF_CALL* GetActorValue)(HF_FormHandle a_handle, HF_ActorValue a_value, float* a_outValue);
    HF_Bool(HF_CALL* GetBaseActorValue)(HF_FormHandle a_handle, HF_ActorValue a_value, float* a_outValue);
    HF_Bool(HF_CALL* GetPermanentActorValue)(HF_FormHandle a_handle, HF_ActorValue a_value, float* a_outValue);

    // Resolves one of HolyFramework's common actor-value identifiers to the
    // same generic AVIF form handle accepted by the ByForm operations below.
    // This lets modules mutate/query common values without knowing Fallout
    // FormIDs or accessing RE::ActorValue directly.
    HF_FormHandle(HF_CALL* GetActorValueForm)(HF_ActorValue a_value);

    // Generic AVIF access. a_actorValue is a normal HF_FormHandle resolving to
    // an ActorValueInfo form, allowing modules to use values such as CA_Affinity
    // without exposing RE::ActorValueInfo pointers through the public ABI.
    HF_Bool(HF_CALL* IsActorValueForm)(HF_FormHandle a_actorValue);
    HF_Bool(HF_CALL* GetActorValueByForm)(
        HF_FormHandle a_actor,
        HF_FormHandle a_actorValue,
        float* a_outValue);
    HF_Bool(HF_CALL* GetBaseActorValueByForm)(
        HF_FormHandle a_actor,
        HF_FormHandle a_actorValue,
        float* a_outValue);
    HF_Bool(HF_CALL* GetPermanentActorValueByForm)(
        HF_FormHandle a_actor,
        HF_FormHandle a_actorValue,
        float* a_outValue);
    HF_Bool(HF_CALL* GetModifierByForm)(
        HF_FormHandle a_actor,
        HF_FormHandle a_actorValue,
        HF_ActorValueModifier a_modifier,
        float* a_outValue);

    // One-shot gameplay mutation. This intentionally has no automatic restore:
    // use it for consumptive/resource changes such as Action Points damage.
    HF_Bool(HF_CALL* ModifyActorValueByForm)(
        HF_FormHandle a_actor,
        HF_FormHandle a_actorValue,
        HF_ActorValueModifier a_modifier,
        float a_delta);

    // Reversible additive contribution. HolyFramework tracks only the amount
    // requested through this handle, so several modules can coexist on the same
    // actor value without owning the actor value exclusively.
    HF_ActorValueAdjustmentHandle(HF_CALL* AcquireAdjustment)(
        HF_FormHandle a_actor,
        HF_FormHandle a_actorValue,
        HF_ActorValueModifier a_modifier,
        float a_amount);
    HF_Bool(HF_CALL* UpdateAdjustment)(
        HF_ActorValueAdjustmentHandle a_handle,
        float a_amount);
    HF_Bool(HF_CALL* ReleaseAdjustment)(
        HF_ActorValueAdjustmentHandle a_handle);
};

inline constexpr std::uint32_t HF_PLAYER_INTERFACE_VERSION = 2;

struct HF_PlayerV2
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    HF_Bool(HF_CALL* IsAvailable)();
    HF_FormHandle(HF_CALL* GetHandle)();
    HF_Bool(HF_CALL* IsGodMode)();
    HF_Bool(HF_CALL* IsImmortal)();
    HF_Bool(HF_CALL* IsPipboyLightOn)();

    // Current dialogue item target, or HF_INVALID_FORM_HANDLE when no valid
    // target is available. No engine pointer crosses the ABI.
    HF_FormHandle(HF_CALL* GetDialogueTarget)();
};

// Opaque native process handle/address. Graphics never transfers ownership;
// returned HWND/DXGI/D3D pointers are borrowed snapshots and must not be released.
using HF_NativeHandle = std::uint64_t;

inline constexpr std::uint32_t HF_INVALID_GRAPHICS_WINDOW_INDEX = 0xFFFFFFFFu;

enum HF_GraphicsStateFlags : std::uint32_t
{
    HF_GRAPHICS_STATE_NONE = 0,
    HF_GRAPHICS_STATE_AVAILABLE = 1u << 0,
    HF_GRAPHICS_STATE_INITIALIZED = 1u << 1,
    HF_GRAPHICS_STATE_WINDOW_AVAILABLE = 1u << 2,
    HF_GRAPHICS_STATE_SWAP_CHAIN_AVAILABLE = 1u << 3,
    HF_GRAPHICS_STATE_DEVICE_AVAILABLE = 1u << 4,
    HF_GRAPHICS_STATE_CONTEXT_AVAILABLE = 1u << 5,
    HF_GRAPHICS_STATE_FULLSCREEN = 1u << 6,
    HF_GRAPHICS_STATE_APP_FULLSCREEN = 1u << 7,
    HF_GRAPHICS_STATE_BORDERLESS = 1u << 8,
    HF_GRAPHICS_STATE_VSYNC_ENABLED = 1u << 9,
    HF_GRAPHICS_STATE_WINDOW_SIZE_CHANGE_PENDING = 1u << 10
};

struct HF_GraphicsStateV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    std::int32_t windowX;
    std::int32_t windowY;
    std::uint32_t windowWidth;
    std::uint32_t windowHeight;
    std::uint32_t adapterIndex;
    std::uint32_t presentInterval;
    std::uint32_t desiredRefreshNumerator;
    std::uint32_t desiredRefreshDenominator;
    std::uint32_t actualRefreshNumerator;
    std::uint32_t actualRefreshDenominator;
    std::uint32_t currentWindowIndex;
};

static_assert(sizeof(HF_GraphicsStateV1) == 52);

struct HF_GraphicsNativeHandlesV1
{
    std::uint32_t structSize;
    std::uint32_t reserved;
    HF_NativeHandle windowHandle;
    HF_NativeHandle swapChain;
    HF_NativeHandle device;
    HF_NativeHandle immediateContext;
};

static_assert(sizeof(HF_GraphicsNativeHandlesV1) == 40);

inline constexpr std::uint32_t HF_GRAPHICS_INTERFACE_VERSION = 1;

struct HF_GraphicsV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    // Renderer state is read-only. The returned native handles are
    // borrowed snapshots; callers must reacquire them after renderer/window
    // recreation and must never Release()/destroy them.
    HF_Bool(HF_CALL* IsAvailable)();
    HF_Bool(HF_CALL* GetState)(HF_GraphicsStateV1* a_outState);
    HF_Bool(HF_CALL* GetNativeHandles)(HF_GraphicsNativeHandlesV1* a_outHandles);
};


enum HF_GameTimeStateFlags : std::uint32_t
{
    HF_GAME_TIME_STATE_NONE = 0,
    HF_GAME_TIME_STATE_AVAILABLE = 1u << 0,
    HF_GAME_TIME_STATE_HOUR_AVAILABLE = 1u << 1,
    HF_GAME_TIME_STATE_DATE_AVAILABLE = 1u << 2,
    HF_GAME_TIME_STATE_DAYS_PASSED_AVAILABLE = 1u << 3,
    HF_GAME_TIME_STATE_TIME_SCALE_AVAILABLE = 1u << 4
};

struct HF_GameTimeStateV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    float gameHour;
    float timeScale;
    float daysPassed;
    float hoursPassed;
    float rawDaysPassed;
    std::uint32_t year;
    std::uint32_t month;
    std::uint32_t day;
    std::uint32_t midnightsPassed;
};

static_assert(sizeof(HF_GameTimeStateV1) == 44);

inline constexpr std::uint32_t HF_GAME_TIME_INTERFACE_VERSION = 1;

struct HF_GameTimeV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    // Read-only view over Fallout's Calendar globals. GetGameHour is the
    // lightweight hot-path call for render/update code that only needs time of day.
    HF_Bool(HF_CALL* IsAvailable)();
    HF_Bool(HF_CALL* GetGameHour)(float* a_outHour);
    HF_Bool(HF_CALL* GetTimeScale)(float* a_outTimeScale);
    HF_Bool(HF_CALL* GetState)(HF_GameTimeStateV1* a_outState);
};


enum HF_WeatherFlags : std::uint32_t
{
    HF_WEATHER_NONE = 0,
    HF_WEATHER_PLEASANT = 1u << 0,
    HF_WEATHER_CLOUDY = 1u << 1,
    HF_WEATHER_RAINY = 1u << 2,
    HF_WEATHER_SNOW = 1u << 3,
    HF_WEATHER_PERMANENT_AURORA = 1u << 4,
    HF_WEATHER_AURORA_FOLLOWS_SUN = 1u << 5,
    HF_WEATHER_RAIN_OCCLUSION = 1u << 6,
    HF_WEATHER_HUD_RAIN = 1u << 7,
    HF_WEATHER_HAS_PRECIPITATION_DATA = 1u << 8
};

enum HF_SkyMode : std::uint32_t
{
    HF_SKY_MODE_NONE = 0,
    HF_SKY_MODE_INTERIOR = 1,
    HF_SKY_MODE_DOME_ONLY = 2,
    HF_SKY_MODE_FULL = 3
};

enum HF_EnvironmentStateFlags : std::uint32_t
{
    HF_ENVIRONMENT_STATE_NONE = 0,
    HF_ENVIRONMENT_STATE_AVAILABLE = 1u << 0,
    HF_ENVIRONMENT_STATE_CURRENT_WEATHER_AVAILABLE = 1u << 1,
    HF_ENVIRONMENT_STATE_LAST_WEATHER_AVAILABLE = 1u << 2,
    HF_ENVIRONMENT_STATE_DEFAULT_WEATHER_AVAILABLE = 1u << 3,
    HF_ENVIRONMENT_STATE_OVERRIDE_WEATHER_AVAILABLE = 1u << 4,
    HF_ENVIRONMENT_STATE_CLIMATE_AVAILABLE = 1u << 5,
    HF_ENVIRONMENT_STATE_REGION_AVAILABLE = 1u << 6,
    HF_ENVIRONMENT_STATE_PRECIPITATION_ACTIVE = 1u << 7,
    HF_ENVIRONMENT_STATE_TRANSITIONING = 1u << 8,
    HF_ENVIRONMENT_STATE_INTERIOR = 1u << 9,
    HF_ENVIRONMENT_STATE_SKY_DOME_ONLY = 1u << 10,
    HF_ENVIRONMENT_STATE_FULL_SKY = 1u << 11
};

struct HF_WeatherInfoV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    float visibilityMultiplier;
    float volatilityMultiplier;
};

static_assert(sizeof(HF_WeatherInfoV1) == 16);

struct HF_EnvironmentStateV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    std::uint32_t currentWeatherFlags;
    std::uint32_t skyMode;
    float currentWeatherPercent;
    float lightingTransition;
    float windSpeed;
    float windAngle;
    float windTurbulence;
    float fogNear;
    float fogFar;
    float fogHeight;
    float fogPower;
    float fogClamp;
    float fogHighDensityScale;
    HF_FormHandle currentWeather;
    HF_FormHandle lastWeather;
    HF_FormHandle defaultWeather;
    HF_FormHandle overrideWeather;
    HF_FormHandle currentClimate;
    HF_FormHandle currentRegion;
};

static_assert(sizeof(HF_EnvironmentStateV1) == 112);

inline constexpr std::uint32_t HF_ENVIRONMENT_INTERFACE_VERSION = 1;

struct HF_EnvironmentV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    // Read-only view over Fallout's sky/weather state. Forms are returned as
    // HolyFramework handles; no engine pointers cross the public ABI.
    HF_Bool(HF_CALL* IsAvailable)();
    HF_Bool(HF_CALL* GetState)(HF_EnvironmentStateV1* a_outState);
    HF_Bool(HF_CALL* GetWeatherInfo)(HF_FormHandle a_weather, HF_WeatherInfoV1* a_outInfo);
};



enum HF_LightingStateFlags : std::uint32_t
{
    HF_LIGHTING_STATE_NONE = 0,
    HF_LIGHTING_STATE_AVAILABLE = 1u << 0,
    HF_LIGHTING_STATE_PLAYER_CELL_AVAILABLE = 1u << 1,
    HF_LIGHTING_STATE_INTERIOR = 1u << 2,
    HF_LIGHTING_STATE_EXTERIOR = 1u << 3,
    HF_LIGHTING_STATE_CELL_TEMPLATE_AVAILABLE = 1u << 4,
    HF_LIGHTING_STATE_WORLDSPACE_AVAILABLE = 1u << 5,
    HF_LIGHTING_STATE_WORLDSPACE_TEMPLATE_AVAILABLE = 1u << 6,
    HF_LIGHTING_STATE_EXTERIOR_OVERRIDE_AVAILABLE = 1u << 7,
    HF_LIGHTING_STATE_PREVIOUS_IMAGE_SPACE_AVAILABLE = 1u << 8
};

struct HF_LightingStateV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    float lightingTransition;
    std::uint32_t reserved;
    HF_FormHandle playerCell;
    HF_FormHandle worldspace;
    HF_FormHandle cellLightingTemplate;
    HF_FormHandle worldspaceLightingTemplate;
    HF_FormHandle exteriorLightingOverride;
    HF_FormHandle previousImageSpace;
};

static_assert(sizeof(HF_LightingStateV1) == 64);

struct HF_LightingTemplateInfoV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    std::uint32_t ambientColor;
    std::uint32_t directionalColor;
    std::uint32_t fogColorNear;
    std::uint32_t fogColorFar;
    std::uint32_t fogColorHighNear;
    std::uint32_t fogColorHighFar;
    std::uint32_t directionalXY;
    std::uint32_t directionalZ;
    std::uint32_t inheritanceFlags;
    std::uint32_t directionalAmbientColors[7];
    float fresnelPower;
    float fogNear;
    float fogFar;
    float directionalFade;
    float clipDistance;
    float fogPower;
    float fogClamp;
    float lightFadeStart;
    float lightFadeEnd;
    float fogHeightMid;
    float fogHeightRange;
    float fogHighDensityScale;
    float fogNearColorScale;
    float fogFarColorScale;
    float fogHighNearColorScale;
    float fogHighFarColorScale;
    float fogFarHeightMid;
    float fogFarHeightRange;
};

static_assert(sizeof(HF_LightingTemplateInfoV1) == 144);

enum HF_ImageSpaceFlags : std::uint32_t
{
    HF_IMAGE_SPACE_NONE = 0,
    HF_IMAGE_SPACE_HAS_LUT_TEXTURE = 1u << 0
};

struct HF_ImageSpaceInfoV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    float eyeAdaptSpeed;
    float bloomBlurRadius;
    float bloomThreshold;
    float bloomScale;
    float receiveBloomThreshold;
    float whitePoint;
    float sunlightScale;
    float skyScale;
    float eyeAdaptStrength;
    float saturation;
    float brightness;
    float contrast;
    float tintAmount;
    float tintRed;
    float tintGreen;
    float tintBlue;
    float dofStrength;
    float dofDistance;
    float dofRange;
    float vignetteRadius;
    float vignetteStrength;
    float dofMode;
    char lutTexture[128];
};

static_assert(sizeof(HF_ImageSpaceInfoV1) == 224);

enum HF_WeatherTimeSlot : std::uint32_t
{
    HF_WEATHER_TIME_SUNRISE = 0,
    HF_WEATHER_TIME_DAY = 1,
    HF_WEATHER_TIME_SUNSET = 2,
    HF_WEATHER_TIME_NIGHT = 3,
    HF_WEATHER_TIME_EARLY_SUNRISE = 4,
    HF_WEATHER_TIME_LATE_SUNRISE = 5,
    HF_WEATHER_TIME_EARLY_SUNSET = 6,
    HF_WEATHER_TIME_LATE_SUNSET = 7,
    HF_WEATHER_TIME_COUNT = 8
};

inline constexpr std::uint32_t HF_LIGHTING_INTERFACE_VERSION = 1;

struct HF_LightingV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;
    HF_Bool(HF_CALL* IsAvailable)();
    HF_Bool(HF_CALL* GetState)(HF_LightingStateV1* a_outState);
    HF_Bool(HF_CALL* GetLightingTemplateInfo)(HF_FormHandle a_template, HF_LightingTemplateInfoV1* a_outInfo);
    HF_Bool(HF_CALL* GetImageSpaceInfo)(HF_FormHandle a_imageSpace, HF_ImageSpaceInfoV1* a_outInfo);
    HF_Bool(HF_CALL* GetWeatherImageSpace)(HF_FormHandle a_weather, HF_WeatherTimeSlot a_timeSlot, HF_FormHandle* a_outImageSpace);
};


// Render-pipeline callbacks are high-level HolyFramework observation points.
// Modules never receive engine ImageSpace objects or vtable/relocation addresses.
enum HF_RenderStage : std::uint32_t
{
    // Invoked immediately after Fallout's ImageSpaceEffectHDR::Render has
    // completed. This is the world-HDR boundary for shared fullscreen
    // post-processing and other read/observe operations at that stage.
    HF_RENDER_STAGE_POST_HDR_WORLD = 1
};

enum HF_RenderPriority : std::int32_t
{
    HF_RENDER_PRIORITY_EARLY = -100,
    HF_RENDER_PRIORITY_NORMAL = 0,
    HF_RENDER_PRIORITY_LATE = 100
};

enum HF_RenderContextFlags : std::uint32_t
{
    HF_RENDER_CONTEXT_NONE = 0,
    HF_RENDER_CONTEXT_WINDOW_AVAILABLE = 1u << 0,
    HF_RENDER_CONTEXT_SWAP_CHAIN_AVAILABLE = 1u << 1,
    HF_RENDER_CONTEXT_DEVICE_AVAILABLE = 1u << 2,
    HF_RENDER_CONTEXT_CONTEXT_AVAILABLE = 1u << 3,
    HF_RENDER_CONTEXT_SESSION_ACTIVE = 1u << 4,
    HF_RENDER_CONTEXT_SESSION_READY = 1u << 5
};

struct HF_RenderStageContextV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    HF_RenderStage stage;
    std::uint32_t reserved;
    std::uint64_t sequence;
    std::uint64_t sessionGeneration;
    HF_NativeHandle windowHandle;
    HF_NativeHandle swapChain;
    HF_NativeHandle device;
    HF_NativeHandle immediateContext;
};

static_assert(sizeof(HF_RenderStageContextV1) == 64);

using HF_RenderStageCallback = void(HF_CALL*)(
    const HF_RenderStageContextV1* a_context,
    void* a_userData);

inline constexpr std::uint32_t HF_RENDER_PIPELINE_INTERFACE_VERSION = 1;

struct HF_RenderPipelineV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    HF_Bool(HF_CALL* IsStageSupported)(HF_RenderStage a_stage);
    HF_RenderSubscriptionHandle(HF_CALL* Subscribe)(
        HF_RenderStage a_stage,
        std::int32_t a_priority,
        HF_RenderStageCallback a_callback,
        void* a_userData);
    HF_Bool(HF_CALL* Unsubscribe)(HF_RenderSubscriptionHandle a_handle);
};


// DXGI presentation interception owned by HolyFramework. Modules receive only
// ABI-stable snapshots/arguments; no IDXGISwapChain, REX or CommonLib type
// crosses this interface. Pre-present callbacks run before the chained DXGI
// Present target and may adjust only syncInterval/presentFlags. Pre-resize
// callbacks run before ResizeBuffers and may adjust its scalar arguments. When
// HF_PresentationPolicyV1 is active, callbacks observe the effective framework
// policy and the same policy is re-applied after callbacks before DXGI is called.
enum HF_PresentationStateFlags : std::uint32_t
{
    HF_PRESENTATION_STATE_NONE = 0,
    HF_PRESENTATION_STATE_AVAILABLE = 1u << 0,
    HF_PRESENTATION_STATE_HOOKS_INSTALLED = 1u << 1,
    HF_PRESENTATION_STATE_COMPROMISED = 1u << 2,
    HF_PRESENTATION_STATE_WINDOWED = 1u << 3,
    HF_PRESENTATION_STATE_FLIP_SEQUENTIAL = 1u << 4,
    HF_PRESENTATION_STATE_FLIP_DISCARD = 1u << 5,
    HF_PRESENTATION_STATE_ALLOW_TEARING = 1u << 6,
    HF_PRESENTATION_STATE_CREATE_HOOK_INSTALLED = 1u << 7,
    HF_PRESENTATION_STATE_CREATE_HOOK_COMPROMISED = 1u << 8
};

inline constexpr std::uint32_t HF_PRESENT_FLAG_TEST = 0x00000001u;
inline constexpr std::uint32_t HF_PRESENT_FLAG_ALLOW_TEARING = 0x00000200u;
inline constexpr std::uint32_t HF_SWAP_CHAIN_FLAG_ALLOW_TEARING = 0x00000800u;
inline constexpr std::uint32_t HF_SWAP_EFFECT_DISCARD = 0u;
inline constexpr std::uint32_t HF_SWAP_EFFECT_SEQUENTIAL = 1u;
inline constexpr std::uint32_t HF_SWAP_EFFECT_FLIP_SEQUENTIAL = 3u;
inline constexpr std::uint32_t HF_SWAP_EFFECT_FLIP_DISCARD = 4u;

struct HF_PresentationStateV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    std::uint32_t bufferCount;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t format;
    std::uint32_t swapEffect;
    std::uint32_t swapChainFlags;
    std::uint32_t sampleCount;
    std::uint32_t sampleQuality;
    HF_Bool windowed;
    std::uint32_t reserved;
};

static_assert(sizeof(HF_PresentationStateV1) == 48);

enum HF_PresentationContextFlags : std::uint32_t
{
    HF_PRESENTATION_CONTEXT_NONE = 0,
    HF_PRESENTATION_CONTEXT_SESSION_ACTIVE = 1u << 0,
    HF_PRESENTATION_CONTEXT_SESSION_READY = 1u << 1,
    HF_PRESENTATION_CONTEXT_TEST_PRESENT = 1u << 2
};

struct HF_PresentContextV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    std::uint64_t sequence;
    std::uint64_t sessionGeneration;
    HF_NativeHandle swapChain;
    std::uint32_t syncInterval;
    std::uint32_t presentFlags;
    std::uint64_t reserved;
};

static_assert(sizeof(HF_PresentContextV1) == 48);

struct HF_ResizeBuffersContextV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    std::uint64_t sequence;
    std::uint64_t sessionGeneration;
    HF_NativeHandle swapChain;
    std::uint32_t bufferCount;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t format;
    std::uint32_t swapChainFlags;
    std::uint32_t reserved;
};

static_assert(sizeof(HF_ResizeBuffersContextV1) == 56);

// Context pointers are valid only for the duration of the callback. Present
// callbacks may change syncInterval (0..4) and presentFlags except for the
// immutable TEST bit. ResizeBuffers callbacks may change only the scalar
// ResizeBuffers arguments carried by the context.
using HF_PresentCallback = void(HF_CALL*)(HF_PresentContextV1* a_context, void* a_userData);
using HF_ResizeBuffersCallback = void(HF_CALL*)(HF_ResizeBuffersContextV1* a_context, void* a_userData);

inline constexpr std::int32_t HF_PRESENTATION_PRIORITY_EARLY = -100;
inline constexpr std::int32_t HF_PRESENTATION_PRIORITY_NORMAL = 0;
inline constexpr std::int32_t HF_PRESENTATION_PRIORITY_LATE = 100;

inline constexpr std::uint32_t HF_PRESENTATION_INTERFACE_VERSION = 2;

enum HF_PresentationCapabilityFlags : std::uint32_t
{
    HF_PRESENTATION_CAPABILITY_NONE = 0,
    HF_PRESENTATION_CAPABILITY_FLIP_SEQUENTIAL = 1u << 0,
    HF_PRESENTATION_CAPABILITY_FLIP_DISCARD = 1u << 1,
    HF_PRESENTATION_CAPABILITY_ALLOW_TEARING = 1u << 2
};

struct HF_PresentationCapabilitiesV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
};

static_assert(sizeof(HF_PresentationCapabilitiesV1) == 8);

enum HF_SwapChainCreateContextFlags : std::uint32_t
{
    HF_SWAP_CHAIN_CREATE_CONTEXT_NONE = 0,
    HF_SWAP_CHAIN_CREATE_CONTEXT_WINDOWED = 1u << 0
};

struct HF_SwapChainCreateContextV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    std::uint64_t sequence;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t refreshNumerator;
    std::uint32_t refreshDenominator;
    std::uint32_t format;
    std::uint32_t scanlineOrdering;
    std::uint32_t scaling;
    std::uint32_t sampleCount;
    std::uint32_t sampleQuality;
    std::uint32_t bufferUsage;
    std::uint32_t bufferCount;
    HF_NativeHandle outputWindow;
    HF_Bool windowed;
    std::uint32_t swapEffect;
    std::uint32_t swapChainFlags;
    std::uint32_t reserved;
};

static_assert(sizeof(HF_SwapChainCreateContextV1) == 88);

// The create callback runs before Fallout calls D3D11CreateDeviceAndSwapChain.
// width/height/refresh/format/scanline/scaling/bufferUsage/outputWindow/windowed
// are informational and immutable. Modules may change only sampleCount,
// sampleQuality, bufferCount, swapEffect and swapChainFlags. HolyFramework
// validates the mutation and automatically retries the original descriptor if
// DXGI rejects the combined policy. HF_PresentationPolicyV1, when active, is
// applied both before callbacks (for observation) and after them (final authority).
using HF_SwapChainCreateCallback = void(HF_CALL*)(HF_SwapChainCreateContextV1* a_context, void* a_userData);

struct HF_Presentation
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    HF_Bool(HF_CALL* IsAvailable)();
    HF_Bool(HF_CALL* GetState)(HF_PresentationStateV1* a_outState);

    // Subscriptions may be created before the swap chain exists. HolyFramework
    // attaches its shared observers when the corresponding presentation path
    // becomes available and dispatches modules in stable priority order.
    HF_PresentationSubscriptionHandle(HF_CALL* SubscribePresent)(
        std::int32_t a_priority,
        HF_PresentCallback a_callback,
        void* a_userData);
    HF_PresentationSubscriptionHandle(HF_CALL* SubscribeResizeBuffers)(
        std::int32_t a_priority,
        HF_ResizeBuffersCallback a_callback,
        void* a_userData);
    HF_Bool(HF_CALL* Unsubscribe)(HF_PresentationSubscriptionHandle a_handle);

    HF_Bool(HF_CALL* GetCapabilities)(HF_PresentationCapabilitiesV1* a_outCapabilities);
    HF_PresentationSubscriptionHandle(HF_CALL* SubscribeSwapChainCreate)(
        std::int32_t a_priority,
        HF_SwapChainCreateCallback a_callback,
        void* a_userData);
};

static_assert(sizeof(HF_Presentation) == 64);

enum HF_FrameTimingStateFlags : std::uint32_t
{
    HF_FRAME_TIMING_STATE_NONE = 0,
    HF_FRAME_TIMING_STATE_AVAILABLE = 1u << 0,
    HF_FRAME_TIMING_STATE_VALID_SAMPLE = 1u << 1,
    HF_FRAME_TIMING_STATE_SESSION_ACTIVE = 1u << 2,
    HF_FRAME_TIMING_STATE_SESSION_READY = 1u << 3
};

struct HF_FrameTimingStateV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    float deltaSeconds;
    float deltaMilliseconds;
    float framesPerSecond;
    float reservedFloat;
    std::uint64_t sessionGeneration;
};

static_assert(sizeof(HF_FrameTimingStateV1) == 32);

inline constexpr std::uint32_t HF_FRAME_TIMING_INTERFACE_VERSION = 1;

struct HF_FrameTimingV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    HF_Bool(HF_CALL* IsAvailable)();
    HF_Bool(HF_CALL* GetState)(HF_FrameTimingStateV1* a_outState);
    HF_Bool(HF_CALL* GetDeltaSeconds)(float* a_outDeltaSeconds);
    HF_Bool(HF_CALL* GetFramesPerSecond)(float* a_outFramesPerSecond);
};

static_assert(sizeof(HF_FrameTimingV1) == 40);

// Shared pre-Present frame limiter. Limits are owner-tracked and arbitrated by
// HolyFramework: the lowest non-zero target wins. A target of 0 keeps the
// handle but suspends that request, allowing modules to switch policies without
// reallocating ownership records every frame. TEST Presents are never delayed.
enum HF_FramePacingStateFlags : std::uint32_t
{
    HF_FRAME_PACING_STATE_NONE = 0,
    HF_FRAME_PACING_STATE_AVAILABLE = 1u << 0,
    HF_FRAME_PACING_STATE_LIMIT_ACTIVE = 1u << 1
};

struct HF_FramePacingStateV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    std::uint32_t activeLimitFPS;
    std::uint32_t requestCount;
    std::uint64_t pacedPresentCount;
    std::uint64_t lastWaitMicroseconds;
};

static_assert(sizeof(HF_FramePacingStateV1) == 32);

inline constexpr std::uint32_t HF_FRAME_PACING_INTERFACE_VERSION = 1;
inline constexpr std::uint32_t HF_FRAME_PACING_MIN_FPS = 10;
inline constexpr std::uint32_t HF_FRAME_PACING_MAX_FPS = 1000;

struct HF_FramePacingV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    HF_Bool(HF_CALL* IsAvailable)();
    HF_Bool(HF_CALL* GetState)(HF_FramePacingStateV1* a_outState);
    HF_FrameLimitHandle(HF_CALL* AcquireLimit)(std::uint32_t a_targetFPS);
    HF_Bool(HF_CALL* UpdateLimit)(HF_FrameLimitHandle a_handle, std::uint32_t a_targetFPS);
    HF_Bool(HF_CALL* ReleaseLimit)(HF_FrameLimitHandle a_handle);
};

static_assert(sizeof(HF_FramePacingV1) == 48);

// Native game-window state and cooperative cursor confinement. Cursor leases are
// owner-tracked by HolyFramework. Any active lease requests confinement to the
// current Fallout client area while the game window is foreground; confinement
// is released automatically when focus is lost or when the last lease ends.
enum HF_WindowStateFlags : std::uint32_t
{
    HF_WINDOW_STATE_NONE = 0,
    HF_WINDOW_STATE_AVAILABLE = 1u << 0,
    HF_WINDOW_STATE_WINDOW_AVAILABLE = 1u << 1,
    HF_WINDOW_STATE_FOREGROUND = 1u << 2,
    HF_WINDOW_STATE_CURSOR_CLIP_AVAILABLE = 1u << 3,
    HF_WINDOW_STATE_CURSOR_CLIPPED = 1u << 4
};

struct HF_WindowStateV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    HF_NativeHandle windowHandle;
    HF_NativeHandle foregroundWindowHandle;
    std::uint32_t cursorClipRequestCount;
    std::uint32_t reserved;
};

static_assert(sizeof(HF_WindowStateV1) == 32);

inline constexpr std::uint32_t HF_WINDOW_INTERFACE_VERSION = 1;

struct HF_WindowV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    HF_Bool(HF_CALL* IsAvailable)();
    HF_Bool(HF_CALL* GetState)(HF_WindowStateV1* a_outState);
    HF_Bool(HF_CALL* IsForeground)();
    HF_CursorClipHandle(HF_CALL* AcquireCursorClip)();
    HF_Bool(HF_CALL* ReleaseCursorClip)(HF_CursorClipHandle a_handle);
};

// Read-only display/output information for the monitor currently containing
// Fallout's swap chain. Refresh rates are exact engine/DXGI rationals where
// available. maxRefresh* is the highest mode reported for the output's current
// desktop resolution from the output mode list; implementations may use a compatible
// DXGI format fallback when the swap-chain format exposes no modes.
enum HF_DisplayStateFlags : std::uint32_t
{
    HF_DISPLAY_STATE_NONE = 0,
    HF_DISPLAY_STATE_AVAILABLE = 1u << 0,
    HF_DISPLAY_STATE_OUTPUT_AVAILABLE = 1u << 1,
    HF_DISPLAY_STATE_ATTACHED_TO_DESKTOP = 1u << 2,
    HF_DISPLAY_STATE_DESKTOP_BOUNDS_VALID = 1u << 3,
    HF_DISPLAY_STATE_MONITOR_HANDLE_AVAILABLE = 1u << 4,
    HF_DISPLAY_STATE_CURRENT_REFRESH_VALID = 1u << 5,
    HF_DISPLAY_STATE_MAX_REFRESH_VALID = 1u << 6
};

struct HF_DisplayStateV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    HF_NativeHandle monitorHandle;
    std::int32_t desktopLeft;
    std::int32_t desktopTop;
    std::int32_t desktopRight;
    std::int32_t desktopBottom;
    std::uint32_t desktopWidth;
    std::uint32_t desktopHeight;
    std::uint32_t currentRefreshNumerator;
    std::uint32_t currentRefreshDenominator;
    std::uint32_t maxRefreshNumerator;
    std::uint32_t maxRefreshDenominator;
    std::uint32_t format;
    std::uint32_t rotation;
};

static_assert(sizeof(HF_DisplayStateV1) == 64);

inline constexpr std::uint32_t HF_DISPLAY_INTERFACE_VERSION = 1;

struct HF_DisplayV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    HF_Bool(HF_CALL* IsAvailable)();
    HF_Bool(HF_CALL* GetState)(HF_DisplayStateV1* a_outState);
};

static_assert(sizeof(HF_DisplayV1) == 24);


// Cooperative DXGI policy layer. Structural fields (flip model, buffer count and
// swap-chain tearing capability) are applied when the swap chain is created or
// resized. syncInterval and Present tearing are applied dynamically every Present.
// For conflicting scalar requests the highest priority wins; ties are stable in
// acquisition order. PREFER_FLIP_MODEL is cooperative and is enabled when any
// active request asks for it. A request with flags=NONE is a suspended lease.
using HF_PresentationPolicyPriority = std::int32_t;

inline constexpr HF_PresentationPolicyPriority HF_PRESENTATION_POLICY_PRIORITY_LOW = -100;
inline constexpr HF_PresentationPolicyPriority HF_PRESENTATION_POLICY_PRIORITY_NORMAL = 0;
inline constexpr HF_PresentationPolicyPriority HF_PRESENTATION_POLICY_PRIORITY_HIGH = 100;

enum HF_PresentationPolicyRequestFlags : std::uint32_t
{
    HF_PRESENTATION_POLICY_REQUEST_NONE = 0,
    HF_PRESENTATION_POLICY_REQUEST_SYNC_INTERVAL = 1u << 0,
    HF_PRESENTATION_POLICY_REQUEST_ALLOW_TEARING = 1u << 1,
    HF_PRESENTATION_POLICY_REQUEST_PREFER_FLIP_MODEL = 1u << 2,
    HF_PRESENTATION_POLICY_REQUEST_BUFFER_COUNT = 1u << 3
};

struct HF_PresentationPolicyRequestV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    HF_PresentationPolicyPriority priority;
    std::uint32_t syncInterval;
    std::uint32_t bufferCount;
    HF_Bool allowTearing;
    std::uint32_t reserved0;
    std::uint64_t reserved1;
};

static_assert(sizeof(HF_PresentationPolicyRequestV1) == 40);

enum HF_PresentationPolicyStateFlags : std::uint32_t
{
    HF_PRESENTATION_POLICY_STATE_NONE = 0,
    HF_PRESENTATION_POLICY_STATE_AVAILABLE = 1u << 0,
    HF_PRESENTATION_POLICY_STATE_ACTIVE = 1u << 1,
    HF_PRESENTATION_POLICY_STATE_SYNC_INTERVAL_OVERRIDE = 1u << 2,
    HF_PRESENTATION_POLICY_STATE_ALLOW_TEARING_OVERRIDE = 1u << 3,
    HF_PRESENTATION_POLICY_STATE_ALLOW_TEARING = 1u << 4,
    HF_PRESENTATION_POLICY_STATE_PREFER_FLIP_MODEL = 1u << 5,
    HF_PRESENTATION_POLICY_STATE_BUFFER_COUNT_OVERRIDE = 1u << 6,
    HF_PRESENTATION_POLICY_STATE_SWAP_CHAIN_FLIP_MODEL = 1u << 7,
    HF_PRESENTATION_POLICY_STATE_SWAP_CHAIN_ALLOW_TEARING = 1u << 8
};

struct HF_PresentationPolicyStateV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    std::uint32_t requestCount;
    std::uint32_t syncInterval;
    std::uint32_t bufferCount;
    HF_Bool allowTearing;
    HF_Bool preferFlipModel;
    std::uint32_t reserved;
    std::uint64_t generation;
};

static_assert(sizeof(HF_PresentationPolicyStateV1) == 40);

inline constexpr std::uint32_t HF_PRESENTATION_POLICY_INTERFACE_VERSION = 1;
inline constexpr std::uint32_t HF_PRESENTATION_POLICY_MIN_BUFFER_COUNT = 2;
inline constexpr std::uint32_t HF_PRESENTATION_POLICY_MAX_BUFFER_COUNT = 8;

struct HF_PresentationPolicyV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    HF_Bool(HF_CALL* IsAvailable)();
    HF_Bool(HF_CALL* GetState)(HF_PresentationPolicyStateV1* a_outState);
    HF_PresentationPolicyHandle(HF_CALL* AcquirePolicy)(const HF_PresentationPolicyRequestV1* a_request);
    HF_Bool(HF_CALL* UpdatePolicy)(HF_PresentationPolicyHandle a_handle, const HF_PresentationPolicyRequestV1* a_request);
    HF_Bool(HF_CALL* ReleasePolicy)(HF_PresentationPolicyHandle a_handle);
};

static_assert(sizeof(HF_PresentationPolicyV1) == 48);


// Declarative frame-pacing policies selected from current game/window state.
// Each field is optional through request flags. For the currently active state,
// the highest-priority matching request wins; equal priorities keep acquisition
// order. The selected target is fed into the shared HF_FramePacing limiter.
using HF_StateFPSPriority = std::int32_t;
inline constexpr HF_StateFPSPriority HF_STATE_FPS_PRIORITY_LOW = -100;
inline constexpr HF_StateFPSPriority HF_STATE_FPS_PRIORITY_NORMAL = 0;
inline constexpr HF_StateFPSPriority HF_STATE_FPS_PRIORITY_HIGH = 100;

enum HF_StateFPSPolicyFlags : std::uint32_t
{
    HF_STATE_FPS_POLICY_NONE = 0,
    HF_STATE_FPS_POLICY_GAMEPLAY = 1u << 0,
    HF_STATE_FPS_POLICY_MAIN_MENU = 1u << 1,
    HF_STATE_FPS_POLICY_LOADING = 1u << 2,
    HF_STATE_FPS_POLICY_LOCKPICKING = 1u << 3,
    HF_STATE_FPS_POLICY_PIPBOY = 1u << 4,
    HF_STATE_FPS_POLICY_BACKGROUND = 1u << 5
};

enum HF_StateFPSActiveState : std::uint32_t
{
    HF_STATE_FPS_ACTIVE_NONE = 0,
    HF_STATE_FPS_ACTIVE_GAMEPLAY = 1,
    HF_STATE_FPS_ACTIVE_MAIN_MENU = 2,
    HF_STATE_FPS_ACTIVE_LOADING = 3,
    HF_STATE_FPS_ACTIVE_LOCKPICKING = 4,
    HF_STATE_FPS_ACTIVE_PIPBOY = 5,
    HF_STATE_FPS_ACTIVE_BACKGROUND = 6
};

struct HF_StateFPSPolicyV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    HF_StateFPSPriority priority;
    std::uint32_t reserved;
    std::uint32_t gameplayFPS;
    std::uint32_t mainMenuFPS;
    std::uint32_t loadingFPS;
    std::uint32_t lockpickingFPS;
    std::uint32_t pipboyFPS;
    std::uint32_t backgroundFPS;
};
static_assert(sizeof(HF_StateFPSPolicyV1) == 40);

enum HF_StateFPSStateFlags : std::uint32_t
{
    HF_STATE_FPS_STATE_NONE = 0,
    HF_STATE_FPS_STATE_AVAILABLE = 1u << 0,
    HF_STATE_FPS_STATE_POLICY_ACTIVE = 1u << 1,
    HF_STATE_FPS_STATE_TARGET_ACTIVE = 1u << 2,
    HF_STATE_FPS_STATE_FOREGROUND = 1u << 3,
    HF_STATE_FPS_STATE_UI_AVAILABLE = 1u << 4
};

struct HF_StateFPSStateV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    std::uint32_t activeState;
    std::uint32_t activeTargetFPS;
    std::uint32_t requestCount;
    std::uint32_t reserved;
    std::uint64_t generation;
};
static_assert(sizeof(HF_StateFPSStateV1) == 32);

inline constexpr std::uint32_t HF_STATE_FPS_INTERFACE_VERSION = 1;
struct HF_StateFPSV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;
    HF_Bool(HF_CALL* IsAvailable)();
    HF_Bool(HF_CALL* GetState)(HF_StateFPSStateV1* a_outState);
    HF_StateFPSPolicyHandle(HF_CALL* AcquirePolicy)(const HF_StateFPSPolicyV1* a_policy);
    HF_Bool(HF_CALL* UpdatePolicy)(HF_StateFPSPolicyHandle a_handle, const HF_StateFPSPolicyV1* a_policy);
    HF_Bool(HF_CALL* ReleasePolicy)(HF_StateFPSPolicyHandle a_handle);
};
static_assert(sizeof(HF_StateFPSV1) == 48);

// Cooperative process CPU scheduling constraint. A non-zero maxLogicalProcessors
// limits the process default scheduling set to at most that many eligible logical
// processors. Zero suspends the lease. timeoutMs=0 disables automatic expiry;
// otherwise HolyFramework restores/re-arbitrates after the timeout even if the
// owning module forgets to release its temporary lease.
enum HF_CPUSchedulingStateFlags : std::uint32_t
{
    HF_CPU_SCHEDULING_STATE_NONE = 0,
    HF_CPU_SCHEDULING_STATE_AVAILABLE = 1u << 0,
    HF_CPU_SCHEDULING_STATE_ACTIVE = 1u << 1,
    HF_CPU_SCHEDULING_STATE_CPU_SETS = 1u << 2,
    HF_CPU_SCHEDULING_STATE_LEGACY_AFFINITY = 1u << 3
};

struct HF_CPUSchedulingStateV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    std::uint32_t activeMaxLogicalProcessors;
    std::uint32_t requestCount;
    std::uint32_t appliedLogicalProcessors;
    std::uint32_t availableLogicalProcessors;
    std::uint64_t generation;
};
static_assert(sizeof(HF_CPUSchedulingStateV1) == 32);

inline constexpr std::uint32_t HF_CPU_SCHEDULING_INTERFACE_VERSION = 1;
inline constexpr std::uint32_t HF_CPU_SCHEDULING_MAX_TIMEOUT_MS = 10u * 60u * 1000u;
struct HF_CPUSchedulingV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;
    HF_Bool(HF_CALL* IsAvailable)();
    HF_Bool(HF_CALL* GetState)(HF_CPUSchedulingStateV1* a_outState);
    HF_CPUSchedulingHandle(HF_CALL* AcquireProcessorLimit)(std::uint32_t a_maxLogicalProcessors, std::uint32_t a_timeoutMs);
    HF_Bool(HF_CALL* UpdateProcessorLimit)(HF_CPUSchedulingHandle a_handle, std::uint32_t a_maxLogicalProcessors, std::uint32_t a_timeoutMs);
    HF_Bool(HF_CALL* ReleaseProcessorLimit)(HF_CPUSchedulingHandle a_handle);
};
static_assert(sizeof(HF_CPUSchedulingV1) == 48);

// Shared runtime tuning for Fallout settings whose safe value depends on frame
// timing. DISABLE_FPS_CLAMP cooperatively keeps iFPSClamp:General at zero while
// requested. Dynamic Papyrus requests are priority-arbitrated; a base budget of
// 0 captures the current positive fUpdateBudgetMS:Papyrus value when activated.
using HF_RuntimeTuningPriority = std::int32_t;
inline constexpr HF_RuntimeTuningPriority HF_RUNTIME_TUNING_PRIORITY_LOW = -100;
inline constexpr HF_RuntimeTuningPriority HF_RUNTIME_TUNING_PRIORITY_NORMAL = 0;
inline constexpr HF_RuntimeTuningPriority HF_RUNTIME_TUNING_PRIORITY_HIGH = 100;

enum HF_RuntimeTuningPolicyFlags : std::uint32_t
{
    HF_RUNTIME_TUNING_POLICY_NONE = 0,
    HF_RUNTIME_TUNING_POLICY_DISABLE_FPS_CLAMP = 1u << 0,
    HF_RUNTIME_TUNING_POLICY_DYNAMIC_PAPYRUS_BUDGET = 1u << 1
};

struct HF_RuntimeTuningPolicyV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    HF_RuntimeTuningPriority priority;
    float papyrusBaseBudgetMS;
    std::uint32_t papyrusMaxFPS;
    std::uint32_t reserved;
};
static_assert(sizeof(HF_RuntimeTuningPolicyV1) == 24);

enum HF_RuntimeTuningStateFlags : std::uint32_t
{
    HF_RUNTIME_TUNING_STATE_NONE = 0,
    HF_RUNTIME_TUNING_STATE_AVAILABLE = 1u << 0,
    HF_RUNTIME_TUNING_STATE_FPS_CLAMP_MANAGED = 1u << 1,
    HF_RUNTIME_TUNING_STATE_PAPYRUS_MANAGED = 1u << 2,
    HF_RUNTIME_TUNING_STATE_FRAME_SAMPLE_VALID = 1u << 3
};

struct HF_RuntimeTuningStateV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    std::uint32_t requestCount;
    std::int32_t effectivePapyrusPriority;
    float papyrusBaseBudgetMS;
    float currentPapyrusBudgetMS;
    float lastIntervalSeconds;
    std::uint32_t papyrusMaxFPS;
    std::int32_t originalFPSClamp;
    std::uint32_t reserved;
    std::uint64_t generation;
};
static_assert(sizeof(HF_RuntimeTuningStateV1) == 48);

inline constexpr std::uint32_t HF_RUNTIME_TUNING_INTERFACE_VERSION = 1;
struct HF_RuntimeTuningV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;
    HF_Bool(HF_CALL* IsAvailable)();
    HF_Bool(HF_CALL* GetState)(HF_RuntimeTuningStateV1* a_outState);
    HF_RuntimeTuningHandle(HF_CALL* AcquirePolicy)(const HF_RuntimeTuningPolicyV1* a_policy);
    HF_Bool(HF_CALL* UpdatePolicy)(HF_RuntimeTuningHandle a_handle, const HF_RuntimeTuningPolicyV1* a_policy);
    HF_Bool(HF_CALL* ReleasePolicy)(HF_RuntimeTuningHandle a_handle);
};
static_assert(sizeof(HF_RuntimeTuningV1) == 48);


enum HF_ConfigDocumentRoot : std::uint32_t
{
    HF_CONFIG_DOCUMENT_ROOT_GAME = 1,
    HF_CONFIG_DOCUMENT_ROOT_DATA = 2,
    HF_CONFIG_DOCUMENT_ROOT_MODULE_DIRECTORY = 3
};

enum HF_ConfigDocumentStateFlags : std::uint32_t
{
    HF_CONFIG_DOCUMENT_STATE_NONE = 0,
    HF_CONFIG_DOCUMENT_STATE_OPEN = 1u << 0,
    HF_CONFIG_DOCUMENT_STATE_FILE_EXISTS = 1u << 1,
    HF_CONFIG_DOCUMENT_STATE_LOADED = 1u << 2
};

struct HF_ConfigDocumentStateV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    std::uint64_t generation;
    std::uint64_t fileSize;
};
static_assert(sizeof(HF_ConfigDocumentStateV1) == 24);

inline constexpr std::uint32_t HF_CONFIG_DOCUMENTS_INTERFACE_VERSION = 1;

struct HF_ConfigDocumentsV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    // Paths must be relative to the selected root and cannot escape it.
    // Missing files are valid open documents so optional MCM user overrides
    // can appear later and be discovered through RefreshIfChanged().
    HF_ConfigDocumentHandle(HF_CALL* OpenIni)(
        HF_ConfigDocumentRoot a_root,
        const char* a_relativePath);
    HF_Bool(HF_CALL* Close)(HF_ConfigDocumentHandle a_handle);
    HF_Bool(HF_CALL* GetState)(
        HF_ConfigDocumentHandle a_handle,
        HF_ConfigDocumentStateV1* a_outState);

    // Checks the backing file at most once per a_minCheckIntervalMs. A value of
    // zero forces an immediate check. a_outChanged reports an appearance,
    // disappearance, timestamp/size change, or successful content reload.
    HF_Bool(HF_CALL* RefreshIfChanged)(
        HF_ConfigDocumentHandle a_handle,
        std::uint32_t a_minCheckIntervalMs,
        HF_Bool* a_outChanged);

    HF_Bool(HF_CALL* HasKey)(HF_ConfigDocumentHandle a_handle, const char* a_key);
    HF_Bool(HF_CALL* GetBool)(
        HF_ConfigDocumentHandle a_handle,
        const char* a_key,
        HF_Bool a_defaultValue,
        HF_Bool* a_outValue);
    HF_Bool(HF_CALL* GetInt64)(
        HF_ConfigDocumentHandle a_handle,
        const char* a_key,
        std::int64_t a_defaultValue,
        std::int64_t* a_outValue);
    HF_Bool(HF_CALL* GetDouble)(
        HF_ConfigDocumentHandle a_handle,
        const char* a_key,
        double a_defaultValue,
        double* a_outValue);
    HF_Bool(HF_CALL* GetString)(
        HF_ConfigDocumentHandle a_handle,
        const char* a_key,
        const char* a_defaultValue,
        char* a_buffer,
        std::uint32_t a_bufferSize);
};

enum HF_PlayerMovementFlags : std::uint32_t
{
    HF_PLAYER_MOVEMENT_NONE = 0,
    HF_PLAYER_MOVEMENT_AVAILABLE = 1u << 0,
    HF_PLAYER_MOVEMENT_INPUT_BLOCKED = 1u << 1,
    HF_PLAYER_MOVEMENT_AUTO_MOVE = 1u << 2,
    HF_PLAYER_MOVEMENT_RUNNING = 1u << 3,
    HF_PLAYER_MOVEMENT_SPRINTING = 1u << 4,
    HF_PLAYER_MOVEMENT_SNEAKING = 1u << 5,
    HF_PLAYER_MOVEMENT_SWIMMING = 1u << 6,
    HF_PLAYER_MOVEMENT_ON_GROUND = 1u << 7,
    HF_PLAYER_MOVEMENT_POWER_ARMOR = 1u << 8
};

struct HF_PlayerMovementSampleV1
{
    std::uint32_t structSize;
    std::uint32_t flags;
    std::uint64_t sequence;
    std::uint64_t sessionGeneration;
    HF_FormHandle player;
    float moveX;
    float moveY;
    float magnitude;
    std::uint32_t reserved;
};
static_assert(sizeof(HF_PlayerMovementSampleV1) == 48);

using HF_PlayerMovementCallback = void(HF_CALL*)(
    const HF_PlayerMovementSampleV1* a_sample,
    void* a_userData);

inline constexpr std::uint32_t HF_PLAYER_MOVEMENT_INTERFACE_VERSION = 1;

struct HF_PlayerMovementV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    HF_Bool(HF_CALL* IsAvailable)();
    HF_Bool(HF_CALL* GetLatest)(HF_PlayerMovementSampleV1* a_outSample);

    // The shared PlayerControls observer is installed only after the first
    // subscription. Callbacks run before Fallout's chained GetControllerOutput
    // target and receive a read-only snapshot of the current movement state.
    HF_PlayerMovementSubscriptionHandle(HF_CALL* Subscribe)(
        HF_PlayerMovementCallback a_callback,
        void* a_userData);
    HF_Bool(HF_CALL* Unsubscribe)(
        HF_PlayerMovementSubscriptionHandle a_handle);
};

inline constexpr std::uint32_t HF_POST_PROCESS_MAX_SHADER_SOURCE_BYTES = 64u * 1024u;
inline constexpr std::uint32_t HF_POST_PROCESS_MAX_CONSTANT_BYTES = 256u;

struct HF_PostProcessEffectDescV1
{
    std::uint32_t structSize;
    HF_RenderStage stage;
    std::uint32_t constantBufferSize;
    std::uint32_t reserved;
    const char* pixelShaderSource;
    const char* pixelShaderEntryPoint;
    const char* label;
};
static_assert(sizeof(HF_PostProcessEffectDescV1) == 40);

inline constexpr std::uint32_t HF_POST_PROCESS_INTERFACE_VERSION = 1;

struct HF_PostProcessV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    // Creation copies the descriptor strings but performs no draw and changes
    // no game state. GPU resources are created lazily on the first Draw().
    // HolyFramework prepends HF_PSInput plus SceneColor:t0 and LinearClamp:s0
    // to the module source. The requested entry point must return SV_TARGET.
    // When constantBufferSize is non-zero, the module may declare its matching
    // cbuffer at b0; size must be 16-byte aligned and at most 256 bytes.
    HF_PostProcessEffectHandle(HF_CALL* CreateEffect)(
        const HF_PostProcessEffectDescV1* a_desc);
    HF_Bool(HF_CALL* DestroyEffect)(HF_PostProcessEffectHandle a_handle);

    // Draw is valid only with a render-stage context delivered by
    // HF_RenderPipelineV1. HolyFramework copies the current full-size target,
    // runs the module pixel shader as a fullscreen triangle, and restores the
    // D3D11 pipeline state before returning.
    HF_Bool(HF_CALL* Draw)(
        HF_PostProcessEffectHandle a_handle,
        const HF_RenderStageContextV1* a_context,
        const void* a_constants,
        std::uint32_t a_constantBytes);
};

inline constexpr std::uint32_t HF_SERIALIZATION_INTERFACE_VERSION = 1;
inline constexpr std::uint32_t HF_SERIALIZATION_MAX_RECORD_SIZE = 4u * 1024u * 1024u;

struct HF_SerializationRecordInfoV1
{
    std::uint32_t structSize;
    std::uint32_t key;
    std::uint32_t version;
    std::uint32_t dataSize;
};

static_assert(sizeof(HF_SerializationRecordInfoV1) == 16);

struct HF_SerializationV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    HF_Bool(HF_CALL* IsAvailable)();

    // Copies a module-local record into HolyFramework-owned memory. The latest
    // value is written automatically when the framework save channel runs.
    // key 0 is reserved and dataSize must not exceed HF_SERIALIZATION_MAX_RECORD_SIZE.
    HF_Bool(HF_CALL* SetRecord)(
        std::uint32_t a_key,
        std::uint32_t a_version,
        const void* a_data,
        std::uint32_t a_dataSize);

    HF_Bool(HF_CALL* GetRecordInfo)(
        std::uint32_t a_key,
        HF_SerializationRecordInfoV1* a_outInfo);

    // On insufficient buffer capacity, returns HF_FALSE and writes the required
    // byte count to a_outBytes when it is non-null. A null buffer with size 0 is
    // therefore a valid size query.
    HF_Bool(HF_CALL* ReadRecord)(
        std::uint32_t a_key,
        void* a_buffer,
        std::uint32_t a_bufferSize,
        std::uint32_t* a_outBytes);

    HF_Bool(HF_CALL* RemoveRecord)(std::uint32_t a_key);
    std::uint32_t(HF_CALL* ClearRecords)();
    std::uint32_t(HF_CALL* GetRecordCount)();

    // Increments whenever the in-memory serialized store changes, including
    // save-load/revert processing. It is process-wide and diagnostic only.
    std::uint64_t(HF_CALL* GetGeneration)();
};


inline constexpr std::uint32_t HF_DIAGNOSTICS_INTERFACE_VERSION = 2;

struct HF_DiagnosticsV2
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    // Marks the current logical operation inside a module callback/load scope.
    // Checkpoint IDs are module-defined and are written to diagnostics on failure.
    void(HF_CALL* SetCheckpoint)(std::uint32_t a_checkpoint);
    void(HF_CALL* ClearCheckpoint)();

    // Reports a recoverable/module-detected failure. Error numbers are local to
    // the calling module's declared 3-letter namespace. <Module>.toml is only
    // the error catalog; failures are written to logs and never shown on HUD.
    void(HF_CALL* ReportFailure)(HF_ErrorCode a_code);

    // Resolves names/descriptions in the current module namespace. When called
    // outside a module execution scope, HolyFramework's HFW namespace is used.
    // Returns stable static strings owned by HolyFramework.
    const char*(HF_CALL* GetErrorName)(HF_ErrorCode a_code);
    const char*(HF_CALL* GetErrorDescription)(HF_ErrorCode a_code);
};



enum HF_RuntimeStateFlags : std::uint32_t
{
    HF_RUNTIME_STATE_NONE = 0,
    HF_RUNTIME_STATE_FRAMEWORK_READY = 1u << 0,
    HF_RUNTIME_STATE_GAME_DATA_READY = 1u << 1,
    HF_RUNTIME_STATE_INPUT_READY = 1u << 2,
    HF_RUNTIME_STATE_GAME_LOADED = 1u << 3,
    HF_RUNTIME_STATE_SESSION_ACTIVE = 1u << 4,
    HF_RUNTIME_STATE_SESSION_READY = 1u << 5
};

inline constexpr std::uint32_t HF_RUNTIME_INTERFACE_VERSION = 1;

struct HF_RuntimeV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    std::uint32_t(HF_CALL* GetStateFlags)();
    std::uint64_t(HF_CALL* GetSessionGeneration)();
    HF_Bool(HF_CALL* HasState)(std::uint32_t a_requiredFlags);
};

enum HF_ModuleHealth : std::uint32_t
{
    HF_MODULE_HEALTH_UNKNOWN = 0,
    HF_MODULE_HEALTH_HEALTHY = 1,
    HF_MODULE_HEALTH_DEGRADED = 2
};

struct HF_ModuleRecordV2
{
    std::uint32_t structSize;
    HF_Version version;
    HF_ModuleHealth health;
    HF_ErrorCode lastError;
    char name[96];

    // Namespace declared by this module's HF_ModuleSignatureV1.
    char prefix[4];

    // Namespace that owns lastError. Empty when lastError == HF_ERROR_NONE.
    // This is "HFW" for framework-generated failures and the module prefix for
    // errors reported through HF_DiagnosticsV2::ReportFailure().
    char lastErrorPrefix[4];
};

static_assert(sizeof(HF_ModuleRecordV2) == 124);

inline constexpr std::uint32_t HF_MODULES_INTERFACE_VERSION = 2;

struct HF_ModulesV2
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    std::uint32_t(HF_CALL* GetCount)();
    std::uint32_t(HF_CALL* GetHealthyCount)();
    std::uint32_t(HF_CALL* GetDegradedCount)();
    HF_Bool(HF_CALL* GetByIndex)(std::uint32_t a_index, HF_ModuleRecordV2* a_outRecord);
    HF_Bool(HF_CALL* FindByName)(const char* a_name, HF_ModuleRecordV2* a_outRecord);
};

static_assert(sizeof(HF_ModulesV2) == 48);


enum HF_TaskQueue : std::uint32_t
{
    HF_TASK_QUEUE_GAME = 1,
    HF_TASK_QUEUE_UI = 2
};

enum HF_TaskFlags : std::uint32_t
{
    HF_TASK_FLAG_NONE = 0,
    // Skip the task if another load/new-game session supersedes the session
    // that was active when the task was scheduled.
    HF_TASK_FLAG_CANCEL_ON_SESSION_CHANGE = 1u << 0,
    // Run only while a game session is active.
    HF_TASK_FLAG_REQUIRE_SESSION_ACTIVE = 1u << 1,
    // Run only after HolyFramework has emitted HF_EVENT_SESSION_READY.
    HF_TASK_FLAG_REQUIRE_SESSION_READY = 1u << 2
};

using HF_TaskCallback = void(HF_CALL*)(void* a_userData);

inline constexpr std::uint32_t HF_TASKS_INTERFACE_VERSION = 1;

struct HF_TasksV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    // Queue one execution on Fallout/F4SE's game or UI task queue.
    // HolyFramework captures task ownership and restores the module execution
    // context before invoking the callback.
    HF_TaskHandle(HF_CALL* Queue)(
        HF_TaskQueue a_queue,
        HF_TaskCallback a_callback,
        void* a_userData,
        std::uint32_t a_flags);

    // Same as Queue(), but waits at least a_delayMs before handing the task to
    // the requested Fallout/F4SE queue. Timing is managed by HolyFramework.
    HF_TaskHandle(HF_CALL* QueueDelayed)(
        HF_TaskQueue a_queue,
        std::uint32_t a_delayMs,
        HF_TaskCallback a_callback,
        void* a_userData,
        std::uint32_t a_flags);

    // Best-effort cancellation. Returns false once the task has already begun.
    HF_Bool(HF_CALL* Cancel)(HF_TaskHandle a_handle);

    // Total one-shot tasks owned by the scheduler that have not begun yet.
    std::uint32_t(HF_CALL* GetPendingCount)();
};


enum HF_MemoryAccessFlags : std::uint32_t
{
    HF_MEMORY_ACCESS_NONE = 0,
    HF_MEMORY_ACCESS_READ = 1u << 0,
    HF_MEMORY_ACCESS_WRITE = 1u << 1,
    HF_MEMORY_ACCESS_EXECUTE = 1u << 2,
    HF_MEMORY_ACCESS_GUARD = 1u << 3
};

struct HF_MemoryRegionV1
{
    std::uint32_t structSize;
    HF_Address baseAddress;
    std::uint64_t regionSize;
    std::uint32_t accessFlags;
    HF_Bool committed;
};

enum HF_PatchStatus : std::uint32_t
{
    HF_PATCH_STATUS_UNKNOWN = 0,
    HF_PATCH_STATUS_APPLIED = 1,
    HF_PATCH_STATUS_RESTORED = 2,
    // The bytes no longer match the patch HolyFramework installed. This is
    // evidence of later modification, not proof that another DLL caused a CTD.
    HF_PATCH_STATUS_MODIFIED = 3
};

struct HF_PatchRecordV1
{
    std::uint32_t structSize;
    HF_PatchHandle handle;
    HF_Address address;
    std::uint32_t size;
    HF_PatchStatus status;
    std::uint32_t checkpoint;
    char owner[96];
    char label[96];
};

inline constexpr std::uint32_t HF_MEMORY_INTERFACE_VERSION = 1;

struct HF_MemoryV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    // Resolve an Address Library relocation without exposing implementation-specific relocation types
    // to modules. a_offset is a signed byte offset added to the resolved address.
    HF_Bool(HF_CALL* ResolveID)(std::uint64_t a_id, std::int64_t a_offset, HF_Address* a_outAddress);

    // Safe inspection helpers. They do not modify process memory.
    HF_Bool(HF_CALL* QueryRegion)(HF_Address a_address, HF_MemoryRegionV1* a_outRegion);
    HF_Bool(HF_CALL* Read)(HF_Address a_address, void* a_outData, std::uint32_t a_size);
    HF_Bool(HF_CALL* Compare)(HF_Address a_address, const void* a_expected, std::uint32_t a_size);

    // Tracked write path. HolyFramework validates a_expected, rejects overlap
    // with an active HolyFramework patch, stores original/replacement bytes and
    // records module ownership/checkpoint before returning a handle. ApplyPatch
    // and RestorePatch must run inside a HolyFramework-supervised module scope
    // (HF_ModuleLoad, an event callback, or a Tasks callback) so ownership is
    // unambiguous.
    HF_PatchHandle(HF_CALL* ApplyPatch)(
        HF_Address a_address,
        const void* a_expected,
        const void* a_replacement,
        std::uint32_t a_size,
        const char* a_label);

    // Restore refuses to overwrite memory that no longer contains the bytes
    // installed by this patch, preventing HolyFramework from erasing a later
    // modification made by another component.
    HF_Bool(HF_CALL* RestorePatch)(HF_PatchHandle a_handle);
    HF_Bool(HF_CALL* VerifyPatch)(HF_PatchHandle a_handle, HF_PatchStatus* a_outStatus);

    std::uint32_t(HF_CALL* GetPatchCount)();
    HF_Bool(HF_CALL* GetPatchByIndex)(std::uint32_t a_index, HF_PatchRecordV1* a_outRecord);

    // Forces an immediate integrity scan. HolyFramework also audits tracked
    // patches periodically in the background. Returns the number currently
    // observed as modified.
    std::uint32_t(HF_CALL* AuditPatches)();
};


enum HF_HookKind : std::uint32_t
{
    HF_HOOK_CALL5 = 1,
    HF_HOOK_CALL6 = 2,
    HF_HOOK_JMP5 = 3,
    HF_HOOK_JMP6 = 4
};

enum HF_HookStatus : std::uint32_t
{
    HF_HOOK_STATUS_UNKNOWN = 0,
    HF_HOOK_STATUS_APPLIED = 1,
    HF_HOOK_STATUS_RESTORED = 2,
    // The hook-site bytes no longer match what HolyFramework installed.
    HF_HOOK_STATUS_MODIFIED = 3
};

struct HF_HookRecordV1
{
    std::uint32_t structSize;
    HF_HookHandle handle;
    HF_Address site;
    HF_Address destination;
    HF_Address originalTarget;
    HF_HookKind kind;
    HF_HookStatus status;
    std::uint32_t checkpoint;
    char owner[96];
    char label[96];
};

inline constexpr std::uint32_t HF_HOOKS_INTERFACE_VERSION = 1;

struct HF_HooksV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    // Global CommonLib/F4SE trampoline statistics.
    std::uint32_t(HF_CALL* GetTrampolineCapacity)();
    std::uint32_t(HF_CALL* GetTrampolineFreeSize)();

    // Allocates executable memory only from HolyFramework's trampoline. This
    // is intended for module-generated stub/code bytes without exposing the internal trampoline implementation.
    HF_CodeBlockHandle(HF_CALL* AllocateCode)(
        std::uint32_t a_size,
        const char* a_label,
        HF_Address* a_outAddress);
    HF_Bool(HF_CALL* WriteCode)(
        HF_CodeBlockHandle a_handle,
        std::uint32_t a_offset,
        const void* a_data,
        std::uint32_t a_size);

    // Installs a tracked CALL/JMP hook. expectedSize must exactly match the
    // hook kind (5 or 6 bytes), so HolyFramework never hooks an unexpected
    // instruction sequence. destination is normally a function in the module
    // or a code block allocated above.
    HF_HookHandle(HF_CALL* Install)(
        HF_HookKind a_kind,
        HF_Address a_site,
        const void* a_expected,
        std::uint32_t a_expectedSize,
        HF_Address a_destination,
        const char* a_label,
        HF_Address* a_outOriginalTarget);

    // Restore is conservative: if another component changed the installed
    // bytes after HolyFramework, the restore is refused rather than overwriting
    // that later change.
    HF_Bool(HF_CALL* Restore)(HF_HookHandle a_handle);
    HF_Bool(HF_CALL* Verify)(HF_HookHandle a_handle, HF_HookStatus* a_outStatus);

    // Registry enumeration reports active/modified hooks only. Restored records
    // remain internally queryable by handle until the next session boundary.
    std::uint32_t(HF_CALL* GetCount)();
    HF_Bool(HF_CALL* GetByIndex)(std::uint32_t a_index, HF_HookRecordV1* a_outRecord);
    std::uint32_t(HF_CALL* Audit)();
};


// Capability names are stable, lowercase-friendly identifiers such as
// "renderer.present" or "player.stamina". Multiple modules may publish the
// same capability; consumers can enumerate providers and choose intentionally.
struct HF_CapabilityRecordV1
{
    std::uint32_t structSize;
    std::uint32_t version;
    char owner[96];
    char name[96];
};

inline constexpr std::uint32_t HF_CAPABILITIES_INTERFACE_VERSION = 1;

struct HF_CapabilitiesV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    HF_Bool(HF_CALL* Publish)(const char* a_name, std::uint32_t a_version);
    std::uint32_t(HF_CALL* GetCount)();
    HF_Bool(HF_CALL* GetByIndex)(std::uint32_t a_index, HF_CapabilityRecordV1* a_outRecord);
    HF_Bool(HF_CALL* FindFirst)(const char* a_name, HF_CapabilityRecordV1* a_outRecord);
};

enum HF_ResourceAccess : std::uint32_t
{
    HF_RESOURCE_ACCESS_SHARED = 1,
    HF_RESOURCE_ACCESS_EXCLUSIVE = 2
};

enum HF_ResourceFlags : std::uint32_t
{
    HF_RESOURCE_FLAG_NONE = 0,
    // Automatically released when the active game session changes.
    HF_RESOURCE_FLAG_SESSION_SCOPED = 1u << 0
};

struct HF_ResourceRecordV1
{
    std::uint32_t structSize;
    HF_ResourceHandle handle;
    HF_ResourceAccess access;
    std::uint32_t flags;
    std::uint64_t sessionGeneration;
    char owner[96];
    char name[96];
    char label[96];
};

inline constexpr std::uint32_t HF_RESOURCES_INTERFACE_VERSION = 1;

struct HF_ResourcesV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    // Claims a logical resource such as "renderer.present". Shared claims may
    // coexist with other shared claims. An exclusive claim conflicts with any
    // active claim for the same normalized resource name.
    HF_ResourceHandle(HF_CALL* Claim)(
        const char* a_name,
        HF_ResourceAccess a_access,
        std::uint32_t a_flags,
        const char* a_label);
    HF_Bool(HF_CALL* Release)(HF_ResourceHandle a_handle);
    HF_Bool(HF_CALL* IsAvailable)(const char* a_name, HF_ResourceAccess a_access);
    std::uint32_t(HF_CALL* GetCount)();
    HF_Bool(HF_CALL* GetByIndex)(std::uint32_t a_index, HF_ResourceRecordV1* a_outRecord);
};

struct HF_PerformanceRecordV1
{
    std::uint32_t structSize;
    std::uint64_t calls;
    std::uint64_t totalMicroseconds;
    std::uint64_t maxMicroseconds;
    std::uint64_t lastMicroseconds;
    std::uint64_t slowCalls;
    std::uint32_t warningThresholdMicroseconds;
    char owner[96];
    char label[96];
};

inline constexpr std::uint32_t HF_PERFORMANCE_INTERFACE_VERSION = 1;

struct HF_PerformanceV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    // Manual samples are intended for module code that does not run through
    // HolyFramework Events/Tasks (for example, a native hook callback). A
    // threshold of 0 uses HolyFramework's default slow-call threshold.
    HF_PerfSampleHandle(HF_CALL* BeginSample)(const char* a_label, std::uint32_t a_warningThresholdMicroseconds);
    HF_Bool(HF_CALL* EndSample)(HF_PerfSampleHandle a_handle);
    std::uint32_t(HF_CALL* GetCount)();
    HF_Bool(HF_CALL* GetByIndex)(std::uint32_t a_index, HF_PerformanceRecordV1* a_outRecord);
};



// Optional user-facing configuration for native modules is read from
// <Module>.ini beside the module DLL. HolyFramework core intentionally has no
// INI and does not expose framework policy as user configuration. The paired
// <Module>.toml is reserved for the module diagnostic/error catalog.
// INI keys are addressed as "Section.Key" (for
// example, [Display] bEnableVSync=true is read as "Display.bEnableVSync").
inline constexpr std::uint32_t HF_CONFIG_INTERFACE_VERSION = 1;

struct HF_ConfigV1
{
    std::uint32_t structSize;
    std::uint32_t interfaceVersion;

    HF_Bool(HF_CALL* HasKey)(const char* a_key);

    // Getters always write a value when a_outValue/a_buffer is valid. They
    // return HF_TRUE when a compatible value exists in the module INI and
    // HF_FALSE when the supplied default had to be used.
    HF_Bool(HF_CALL* GetBool)(const char* a_key, HF_Bool a_defaultValue, HF_Bool* a_outValue);
    HF_Bool(HF_CALL* GetInt64)(const char* a_key, std::int64_t a_defaultValue, std::int64_t* a_outValue);
    HF_Bool(HF_CALL* GetDouble)(const char* a_key, double a_defaultValue, double* a_outValue);
    HF_Bool(HF_CALL* GetString)(
        const char* a_key,
        const char* a_defaultValue,
        char* a_buffer,
        std::uint32_t a_bufferSize);

    // Re-reads only the calling module's optional INI. Existing values remain
    // active if parsing fails. Generation increments after each successful load.
    HF_Bool(HF_CALL* Reload)();
    std::uint64_t(HF_CALL* GetGeneration)();
};

// HolyFramework modules must declare HF_DECLARE_MODULE_SIGNATURE("ABC") in one
// translation unit. HolyFramework checks the marker before normal module loading.
struct HF_ModuleInfoV1
{
    std::uint32_t structSize;
    std::uint32_t requiredABIVersion;
    const char* name;
    const char* author;
    HF_Version version;
};

using HF_GetModuleInfoFn = const HF_ModuleInfoV1*(HF_CALL*)();
using HF_ModuleLoadFn = HF_Bool(HF_CALL*)(const HF_API* a_api);
using HF_ModuleUnloadFn = void(HF_CALL*)();

HF_FRAMEWORK_EXPORT const HF_API* HF_CALL HF_GetAPI(std::uint32_t a_requestedABIVersion);

#ifdef __cplusplus
namespace HF
{
    [[nodiscard]] inline const HF_CoreV1* GetCore(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_CoreV1*>(
            a_api->QueryInterface(HF_INTERFACE_CORE, HF_CORE_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_Log* GetLog(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_Log*>(
            a_api->QueryInterface(HF_INTERFACE_LOG, HF_LOG_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_EventsV1* GetEvents(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_EventsV1*>(
            a_api->QueryInterface(HF_INTERFACE_EVENTS, HF_EVENTS_INTERFACE_VERSION));
    }


    [[nodiscard]] inline const HF_RuntimeV1* GetRuntime(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_RuntimeV1*>(
            a_api->QueryInterface(HF_INTERFACE_RUNTIME, HF_RUNTIME_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_ModulesV2* GetModules(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_ModulesV2*>(
            a_api->QueryInterface(HF_INTERFACE_MODULES, HF_MODULES_INTERFACE_VERSION));
    }


    [[nodiscard]] inline const HF_TasksV1* GetTasks(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_TasksV1*>(
            a_api->QueryInterface(HF_INTERFACE_TASKS, HF_TASKS_INTERFACE_VERSION));
    }



    [[nodiscard]] inline const HF_MemoryV1* GetMemory(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_MemoryV1*>(
            a_api->QueryInterface(HF_INTERFACE_MEMORY, HF_MEMORY_INTERFACE_VERSION));
    }



    [[nodiscard]] inline const HF_HooksV1* GetHooks(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_HooksV1*>(
            a_api->QueryInterface(HF_INTERFACE_HOOKS, HF_HOOKS_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_CapabilitiesV1* GetCapabilities(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_CapabilitiesV1*>(
            a_api->QueryInterface(HF_INTERFACE_CAPABILITIES, HF_CAPABILITIES_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_ResourcesV1* GetResources(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_ResourcesV1*>(
            a_api->QueryInterface(HF_INTERFACE_RESOURCES, HF_RESOURCES_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_PerformanceV1* GetPerformance(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_PerformanceV1*>(
            a_api->QueryInterface(HF_INTERFACE_PERFORMANCE, HF_PERFORMANCE_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_ConfigV1* GetConfig(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_ConfigV1*>(
            a_api->QueryInterface(HF_INTERFACE_CONFIG, HF_CONFIG_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_DiagnosticsV2* GetDiagnostics(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_DiagnosticsV2*>(
            a_api->QueryInterface(HF_INTERFACE_DIAGNOSTICS, HF_DIAGNOSTICS_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_UI* GetUI(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_UI*>(
            a_api->QueryInterface(HF_INTERFACE_UI, HF_UI_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_GameV1* GetGame(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_GameV1*>(
            a_api->QueryInterface(HF_INTERFACE_GAME, HF_GAME_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_GameSettingsV1* GetGameSettings(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_GameSettingsV1*>(
            a_api->QueryInterface(HF_INTERFACE_GAME_SETTINGS, HF_GAME_SETTINGS_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_FormsV1* GetForms(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_FormsV1*>(
            a_api->QueryInterface(HF_INTERFACE_FORMS, HF_FORMS_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_ReferencesV2* GetReferences(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_ReferencesV2*>(
            a_api->QueryInterface(HF_INTERFACE_REFERENCES, HF_REFERENCES_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_ActorsV3* GetActors(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_ActorsV3*>(
            a_api->QueryInterface(HF_INTERFACE_ACTORS, HF_ACTORS_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_PlayerV2* GetPlayer(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_PlayerV2*>(
            a_api->QueryInterface(HF_INTERFACE_PLAYER, HF_PLAYER_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_GraphicsV1* GetGraphics(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_GraphicsV1*>(
            a_api->QueryInterface(HF_INTERFACE_GRAPHICS, HF_GRAPHICS_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_GameTimeV1* GetGameTime(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_GameTimeV1*>(
            a_api->QueryInterface(HF_INTERFACE_GAME_TIME, HF_GAME_TIME_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_EnvironmentV1* GetEnvironment(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_EnvironmentV1*>(
            a_api->QueryInterface(HF_INTERFACE_ENVIRONMENT, HF_ENVIRONMENT_INTERFACE_VERSION));
    }


    [[nodiscard]] inline const HF_LightingV1* GetLighting(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_LightingV1*>(
            a_api->QueryInterface(HF_INTERFACE_LIGHTING, HF_LIGHTING_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_RenderPipelineV1* GetRenderPipeline(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_RenderPipelineV1*>(
            a_api->QueryInterface(HF_INTERFACE_RENDER_PIPELINE, HF_RENDER_PIPELINE_INTERFACE_VERSION));
    }


    [[nodiscard]] inline const HF_Presentation* GetPresentation(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_Presentation*>(
            a_api->QueryInterface(HF_INTERFACE_PRESENTATION, HF_PRESENTATION_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_FrameTimingV1* GetFrameTiming(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_FrameTimingV1*>(
            a_api->QueryInterface(HF_INTERFACE_FRAME_TIMING, HF_FRAME_TIMING_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_FramePacingV1* GetFramePacing(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_FramePacingV1*>(
            a_api->QueryInterface(HF_INTERFACE_FRAME_PACING, HF_FRAME_PACING_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_WindowV1* GetWindow(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_WindowV1*>(
            a_api->QueryInterface(HF_INTERFACE_WINDOW, HF_WINDOW_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_DisplayV1* GetDisplay(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_DisplayV1*>(
            a_api->QueryInterface(HF_INTERFACE_DISPLAY, HF_DISPLAY_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_PresentationPolicyV1* GetPresentationPolicy(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_PresentationPolicyV1*>(
            a_api->QueryInterface(HF_INTERFACE_PRESENTATION_POLICY, HF_PRESENTATION_POLICY_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_StateFPSV1* GetStateFPS(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) return nullptr;
        return static_cast<const HF_StateFPSV1*>(
            a_api->QueryInterface(HF_INTERFACE_STATE_FPS, HF_STATE_FPS_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_CPUSchedulingV1* GetCPUScheduling(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) return nullptr;
        return static_cast<const HF_CPUSchedulingV1*>(
            a_api->QueryInterface(HF_INTERFACE_CPU_SCHEDULING, HF_CPU_SCHEDULING_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_RuntimeTuningV1* GetRuntimeTuning(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) return nullptr;
        return static_cast<const HF_RuntimeTuningV1*>(
            a_api->QueryInterface(HF_INTERFACE_RUNTIME_TUNING, HF_RUNTIME_TUNING_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_ConfigDocumentsV1* GetConfigDocuments(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) return nullptr;
        return static_cast<const HF_ConfigDocumentsV1*>(
            a_api->QueryInterface(HF_INTERFACE_CONFIG_DOCUMENTS, HF_CONFIG_DOCUMENTS_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_PlayerMovementV1* GetPlayerMovement(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) return nullptr;
        return static_cast<const HF_PlayerMovementV1*>(
            a_api->QueryInterface(HF_INTERFACE_PLAYER_MOVEMENT, HF_PLAYER_MOVEMENT_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_PostProcessV1* GetPostProcess(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) return nullptr;
        return static_cast<const HF_PostProcessV1*>(
            a_api->QueryInterface(HF_INTERFACE_POST_PROCESS, HF_POST_PROCESS_INTERFACE_VERSION));
    }

    [[nodiscard]] inline const HF_SerializationV1* GetSerialization(const HF_API* a_api) noexcept
    {
        if (!a_api || !a_api->QueryInterface) {
            return nullptr;
        }
        return static_cast<const HF_SerializationV1*>(
            a_api->QueryInterface(HF_INTERFACE_SERIALIZATION, HF_SERIALIZATION_INTERFACE_VERSION));
    }
}
#endif
