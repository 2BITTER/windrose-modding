#include <Mod/CppUserModBase.hpp>
#include <UE4SSProgram.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/Rotator.hpp>
#include <Unreal/Quat.hpp>
#include <Unreal/NameTypes.hpp>
#include <Input/Handler.hpp>
#include <Input/KeyDef.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <LuaMadeSimple/LuaMadeSimple.hpp>
extern "C" {
#include <lua.h>
}
#include <windows.h>
#include <deque>
#include <mutex>
#include <vector>
#include <set>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <fstream>
#include <string>
#include <cctype>
#include <cstring>

using namespace RC;
using namespace RC::Unreal;
using namespace RC::Input;

static constexpr double EYE_PI = 3.14159265358979323846;

// ──────────────────────────────────────────────────────────────────────────────
// Config (read from config.txt next to the mod, at load)
// ──────────────────────────────────────────────────────────────────────────────
// Toggle fields are std::atomic: the ImGui tab callback runs on UE4SS's render
// thread while the tick dispatcher reads/applies them on the game thread.
struct BBTConfig
{
    std::atomic<bool> copyObjEnabled{true};
    Key               copyObjKey     = Key::C;
    bool              copyObjShift   = true;
    std::atomic<bool> copyAngleEnabled{true};
    Key               copyAngleKey  = Key::C;
    bool              copyAngleAlt  = true; // Alt, not Shift — Alt = Aim, doesn't move the player like dodge does
    std::atomic<bool> undoEnabled{true};
    Key               undoKey        = Key::Z;
    bool              undoShift      = true;
    std::atomic<int>  undoMaxStack{10};
    // Free-build (OFF by default — cheats). Synced to R5BuildingSettings every tick.
    std::atomic<bool> freeNoCost{false};   // bBuildingResourcesValidation = false
    std::atomic<bool> freeNoStability{false}; // IntegritySettings on every DA_BI_* item
    // Fine rotation cycle (1/5/10 merged into the vanilla RotationSteps cycle).
    // No dedicated keybind — rides on whatever key the player has bound to the
    // game's own rotation-step action (see OnChangeRotationStep hook below).
    // Independently toggleable: the game snaps to a neighbor's rotation within
    // ~3 degrees, so a stray 1-degree step can silently misalign adjacent walls.
    std::atomic<bool> rotation1Enabled{true};
    std::atomic<bool> rotation5Enabled{true};
    std::atomic<bool> rotation10Enabled{true};
    // BStat UI (persistent build-mode overlay)
    std::atomic<bool> bstatEnabled{true};
    // Placement Freedom (OFF by default). Removes shelter/roof/bonfire restrictions
    // for specific item categories without affecting build cost.
    std::atomic<bool> placementAllowUnderRoof{false};  // furnace/kiln under roof
    std::atomic<bool> placementNoRoofRequired{false};  // workstations/beds without roof
    std::atomic<bool> placementNoBonfire{false};       // no building center required
    // UI scale (percentage, 70-150)
    std::atomic<int>  uiMenuScale{100};
    std::atomic<int>  uiBStatScale{100};
};
static BBTConfig g_cfg;

static std::string BBT_Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static bool BBT_ParseBool(std::string v)
{
    for (auto& ch : v) ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
    return v == "true" || v == "1" || v == "yes" || v == "on";
}
static Key BBT_ParseKey(const std::string& s, Key fallback)
{
    if (s.empty()) return fallback;
    char c = static_cast<char>(::toupper(static_cast<unsigned char>(s[0])));
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        return static_cast<Key>(c);   // UE4SS Key enum uses VK codes (A=0x41, 0=0x30)
    return fallback;
}

// Folder containing this mod (…\Mods\BetterBuildingTools), derived from our DLL
// path so it's portable on any user's machine (no hardcoded paths).
static std::wstring BBT_ModFolder()
{
    HMODULE hm = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&BBT_ModFolder), &hm);
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(hm, path, MAX_PATH);   // …\BetterBuildingTools\dlls\main.dll
    std::wstring p = path;
    for (int i = 0; i < 2; ++i)               // strip "\main.dll" then "\dlls"
    {
        size_t pos = p.find_last_of(L"\\/");
        if (pos != std::wstring::npos) p = p.substr(0, pos);
    }
    return p;
}

static void BBT_WriteDefaultConfig(const std::wstring& path)
{
    std::ofstream out(path);
    if (!out.good()) return;
    out <<
        "# BetterBuildingTools (BBT) config\n"
        "# Edit values then reload the mod (or restart the game) to apply.\n"
        "# Keys are single letters/digits (A-Z, 0-9). *_shift requires Shift held.\n"
        "\n"
        "copyobject_enabled = true\n"
        "copyobject_key     = C\n"
        "copyobject_shift   = true\n"
        "\n"
        "# Copy angle: look at a placed block and snap your build preview's rotation\n"
        "# to match it exactly (not just the nearest rotation step). Separate from the\n"
        "# copy object on purpose — Alt by default since it's already bound to Aim,\n"
        "# which (unlike Shift/dodge) doesn't move you around while you line up a shot.\n"
        "copyangle_enabled = true\n"
        "copyangle_key     = C\n"
        "copyangle_alt     = true\n"
        "\n"
        "undo_enabled   = true\n"
        "undo_key       = Z\n"
        "undo_shift     = true\n"
        "undo_max_stack = 10\n"
        "\n"
        "# Free build (CHEATS — off by default). Apply on game launch.\n"
        "# no_cost: build without consuming resources.\n"
        "# build_anywhere: skip the building-center requirement (build anywhere).\n"
        "# no_stability: disables the structural integrity check on the global\n"
        "#   build validation profile, allowing floating/unsupported structures.\n"
        "freebuild_no_cost        = false\n"
        "freebuild_no_stability   = false\n"
        "\n"
        "# Fine rotation cycle: merges enabled steps below into the game's own\n"
        "# rotation-step cycle (e.g. 1, 5, 10, then the vanilla steps in order, then\n"
        "# back to 1). No separate keybind — it rides on whatever key YOU have bound\n"
        "# to 'Rotation Step' in the game's own keybindings menu. Each step is its\n"
        "# own toggle: the game snaps to a neighboring block's rotation within ~3\n"
        "# degrees, so 1-degree steps can land inside that window and silently\n"
        "# misalign a wall from the one next to it — disable it here if that bites.\n"
        "rotation_1deg_enabled  = true\n"
        "rotation_5deg_enabled  = true\n"
        "rotation_10deg_enabled = true\n"
        "\n"
        "# Build Status HUD: persistent overlay during build mode showing rotation,\n"
        "# step size, snap state, looked-at block rotation, keybinds.\n"
        "bstat_enabled = true\n"
        "\n"
        "# Placement Freedom: removes shelter/roof/bonfire restrictions for specific\n"
        "# items. Separate from Free Build (cost bypass). Each toggle is independent.\n"
        "# allow_under_roof: furnace/kiln can be placed under a roof.\n"
        "# no_roof_required: workstations/beds don't need a roof overhead.\n"
        "# no_bonfire: skip the bonfire/building-center proximity requirement.\n"
        "placement_allow_under_roof = false\n"
        "placement_no_roof_required = false\n"
        "placement_no_bonfire       = false\n";
}

static void BBT_LoadConfig()
{
    std::wstring cfgPath = BBT_ModFolder() + L"\\config.txt";
    std::ifstream in(cfgPath);
    if (!in.good())
    {
        BBT_WriteDefaultConfig(cfgPath);   // first run — write defaults, keep struct defaults
        Output::send<LogLevel::Verbose>(STR("[BBT] No config.txt — wrote defaults\n"));
        return;
    }
    std::string line;
    while (std::getline(in, line))
    {
        size_t h = line.find('#');
        if (h != std::string::npos) line = line.substr(0, h);
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = BBT_Trim(line.substr(0, eq));
        std::string v = BBT_Trim(line.substr(eq + 1));
        if      (k == "copyobject_enabled") g_cfg.copyObjEnabled = BBT_ParseBool(v);
        else if (k == "copyobject_key")     g_cfg.copyObjKey     = BBT_ParseKey(v, Key::C);
        else if (k == "copyobject_shift")   g_cfg.copyObjShift   = BBT_ParseBool(v);
        else if (k == "copyangle_enabled") g_cfg.copyAngleEnabled = BBT_ParseBool(v);
        else if (k == "copyangle_key")     g_cfg.copyAngleKey     = BBT_ParseKey(v, Key::C);
        else if (k == "copyangle_alt")     g_cfg.copyAngleAlt     = BBT_ParseBool(v);
        else if (k == "undo_enabled")       g_cfg.undoEnabled    = BBT_ParseBool(v);
        else if (k == "undo_key")           g_cfg.undoKey        = BBT_ParseKey(v, Key::Z);
        else if (k == "undo_shift")         g_cfg.undoShift      = BBT_ParseBool(v);
        else if (k == "undo_max_stack")     { int n = std::atoi(v.c_str()); if (n > 0 && n <= 100) g_cfg.undoMaxStack = n; }
        else if (k == "freebuild_no_cost")        g_cfg.freeNoCost      = BBT_ParseBool(v);
        else if (k == "freebuild_build_anywhere") g_cfg.placementNoBonfire = BBT_ParseBool(v); // legacy key → new toggle
        else if (k == "freebuild_no_stability")   g_cfg.freeNoStability = BBT_ParseBool(v);
        else if (k == "rotation_1deg_enabled")    g_cfg.rotation1Enabled  = BBT_ParseBool(v);
        else if (k == "rotation_5deg_enabled")    g_cfg.rotation5Enabled  = BBT_ParseBool(v);
        else if (k == "rotation_10deg_enabled")   g_cfg.rotation10Enabled = BBT_ParseBool(v);
        else if (k == "bstat_enabled")              g_cfg.bstatEnabled      = BBT_ParseBool(v);
        else if (k == "placement_allow_under_roof")   g_cfg.placementAllowUnderRoof = BBT_ParseBool(v);
        else if (k == "placement_no_roof_required")   g_cfg.placementNoRoofRequired = BBT_ParseBool(v);
        else if (k == "placement_no_bonfire")         g_cfg.placementNoBonfire      = BBT_ParseBool(v);
        else if (k == "ui_menu_scale")  { int n = std::atoi(v.c_str()); if (n >= 70 && n <= 150) g_cfg.uiMenuScale  = n; }
        else if (k == "ui_bstat_scale") { int n = std::atoi(v.c_str()); if (n >= 70 && n <= 150) g_cfg.uiBStatScale = n; }
    }
    Output::send<LogLevel::Verbose>(STR("[BBT] Config loaded (copyobj={}, undo={}, maxStack={})\n"),
        g_cfg.copyObjEnabled.load(), g_cfg.undoEnabled.load(), g_cfg.undoMaxStack.load());
}

static char BBT_KeyChar(Key k)
{
    char c = static_cast<char>(k);
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
    return '?';
}

static void BBT_SaveConfig()
{
    std::wstring cfgPath = BBT_ModFolder() + L"\\config.txt";
    std::ofstream out(cfgPath);
    if (!out.good()) return;
    auto b = [](bool v) -> const char* { return v ? "true" : "false"; };
    out <<
        "# BetterBuildingTools (BBT) config\n"
        "# Edit values then reload the mod (or restart the game) to apply.\n"
        "# Keys are single letters/digits (A-Z, 0-9). *_shift requires Shift held.\n"
        "\n"
        "copyobject_enabled = " << b(g_cfg.copyObjEnabled) << "\n"
        "copyobject_key     = " << BBT_KeyChar(g_cfg.copyObjKey) << "\n"
        "copyobject_shift   = " << b(g_cfg.copyObjShift) << "\n"
        "\n"
        "copyangle_enabled = " << b(g_cfg.copyAngleEnabled) << "\n"
        "copyangle_key     = " << BBT_KeyChar(g_cfg.copyAngleKey) << "\n"
        "copyangle_alt     = " << b(g_cfg.copyAngleAlt) << "\n"
        "\n"
        "undo_enabled   = " << b(g_cfg.undoEnabled) << "\n"
        "undo_key       = " << BBT_KeyChar(g_cfg.undoKey) << "\n"
        "undo_shift     = " << b(g_cfg.undoShift) << "\n"
        "undo_max_stack = " << g_cfg.undoMaxStack.load() << "\n"
        "\n"
        "freebuild_no_cost        = " << b(g_cfg.freeNoCost) << "\n"
        "freebuild_no_stability   = " << b(g_cfg.freeNoStability) << "\n"
        "\n"
        "rotation_1deg_enabled  = " << b(g_cfg.rotation1Enabled) << "\n"
        "rotation_5deg_enabled  = " << b(g_cfg.rotation5Enabled) << "\n"
        "rotation_10deg_enabled = " << b(g_cfg.rotation10Enabled) << "\n"
        "\n"
        "bstat_enabled = " << b(g_cfg.bstatEnabled) << "\n"
        "\n"
        "placement_allow_under_roof = " << b(g_cfg.placementAllowUnderRoof) << "\n"
        "placement_no_roof_required = " << b(g_cfg.placementNoRoofRequired) << "\n"
        "placement_no_bonfire       = " << b(g_cfg.placementNoBonfire) << "\n"
        "\n"
        "ui_menu_scale  = " << g_cfg.uiMenuScale.load() << "\n"
        "ui_bstat_scale = " << g_cfg.uiBStatScale.load() << "\n";
    Output::send<LogLevel::Verbose>(STR("[BBT] Config saved\n"));
}

// Keydown callbacks run on the UE4SS input thread, NOT the game thread.
// Touching game objects / the BL async queue off-thread corrupts state
// ("worker thread stopped unexpectedly"). So keys only set a request flag;
// the actual work runs in the engine-tick callback (game thread).
static std::atomic<bool> g_reqUndo{false};
static std::atomic<bool> g_reqCopyObj{false};
static std::atomic<bool> g_reqMatchAngle{false};

// Match-angle hold state — set on Alt+C, cleared when player manually rotates.
struct BBTQuat { double X, Y, Z, W; double RotPitch, RotYaw, RotRoll; };
static BBTQuat           g_copyAngleTarget{0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0};
static std::atomic<bool> g_copyAngleHold{false};

// Undo cooldown + in-flight guard
static std::chrono::steady_clock::time_point g_lastUndoTime{};
static constexpr int64_t UNDO_COOLDOWN_MS = 500;
static std::atomic<bool> g_undoInFlight{false};

// Cached undo infrastructure (expensive lookups done once, reused)
static UObject*    g_cachedASC      = nullptr;
static AActor*     g_cachedASCOwner = nullptr;
static UFunction*  g_cachedSendFn   = nullptr;

// ──────────────────────────────────────────────────────────────────────────────
// Undo stack
// ──────────────────────────────────────────────────────────────────────────────
static std::deque<UObject*> g_UndoStack;
static std::mutex            g_StackMutex;
// Max undo depth is configurable via g_cfg.undoMaxStack.

// ──────────────────────────────────────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────────────────────────────────────
static bool IsObjectValid(UObject* obj)
{
    if (!obj) return false;
    if (obj->IsUnreachable()) return false;
    // Catch actors that were destroyed but not yet GC'd (e.g. manual delete).
    // A destroyed UObject is flagged BeginDestroyed/FinishDestroyed and/or PendingKill.
    if (obj->HasAnyFlags(static_cast<EObjectFlags>(RF_BeginDestroyed | RF_FinishDestroyed)))
        return false;
    if (obj->HasAnyInternalFlags(EInternalObjectFlags::PendingKill))
        return false;
    // Destroyed objects often get renamed to "None"; treat that as invalid too.
    if (obj->GetName() == STR("None"))
        return false;
    return true;
}

// FGameplayTag = FName wrapper (8 bytes in shipping).
// FGameplayEventData is 0xB0 bytes (from GameplayAbilities.hpp dump):
//   EventTag(0x00) Instigator(0x08) Target(0x10) OptionalObject(0x18)
//   OptionalObject2(0x20) ContextHandle(0x28) InstigatorTags(0x40)
//   TargetTags(0x60) EventMagnitude(0x80) TargetData(0x88)  Size: 0xB0
//
// SendGameplayEventToActor(AActor* Actor, FGameplayTag EventTag, FGameplayEventData Payload):
//   params layout:  Actor(0x00) EventTag(0x08) Payload(0x10 .. 0xC0)
#pragma pack(push, 1)
struct SendGameplayEventParams
{
    AActor*  Actor;            // 0x00
    FName    EventTag;         // 0x08 (FName = 8 bytes)
    uint8_t  Payload[0xB0];    // 0x10 — full FGameplayEventData, zeroed
};
#pragma pack(pop)

// ──────────────────────────────────────────────────────────────────────────────
// TryUndo — called on Shift+Z
// ──────────────────────────────────────────────────────────────────────────────
static void EnsureUndoInfrastructure()
{
    if (!g_cachedSendFn)
    {
        UObject* fnObj = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr,
            STR("/Script/GameplayAbilities.AbilitySystemBlueprintLibrary:SendGameplayEventToActor"));
        g_cachedSendFn = reinterpret_cast<UFunction*>(fnObj);
    }

    if (!g_cachedASC || !IsObjectValid(g_cachedASC))
    {
        g_cachedASC = nullptr;
        g_cachedASCOwner = nullptr;
        std::vector<UObject*> ascs;
        UObjectGlobals::FindAllOf(STR("R5AbilitySystemComponent"), ascs);

        UObject* fallback = nullptr;
        AActor*  fallbackOwner = nullptr;
        for (UObject* a : ascs)
        {
            if (!IsObjectValid(a)) continue;
            AActor* o = a->GetTypedOuter<AActor>();
            if (!o) continue;
            if (!fallback) { fallback = a; fallbackOwner = o; }
            if (o->GetName().find(STR("PlayerState")) != File::StringType::npos)
            {
                g_cachedASC = a;
                g_cachedASCOwner = o;
                break;
            }
        }
        if (!g_cachedASC) { g_cachedASC = fallback; g_cachedASCOwner = fallbackOwner; }
    }
}

static void TryUndo()
{
    // Cooldown
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastUndoTime).count();
    if (elapsed < UNDO_COOLDOWN_MS) return;

    // In-flight guard — only one undo at a time
    bool expected = false;
    if (!g_undoInFlight.compare_exchange_strong(expected, true)) return;

    g_lastUndoTime = now;

    // Pop stale entries
    {
        std::lock_guard<std::mutex> lock(g_StackMutex);
        while (!g_UndoStack.empty() && !IsObjectValid(g_UndoStack.back()))
            g_UndoStack.pop_back();

        if (g_UndoStack.empty())
        {
            g_undoInFlight.store(false);
            return;
        }
    }

    EnsureUndoInfrastructure();

    if (!g_cachedASC || !IsObjectValid(g_cachedASC) || !g_cachedASCOwner || !g_cachedSendFn)
    {
        Output::send<LogLevel::Verbose>(STR("[BBT] Undo: missing ASC or SendFn\n"));
        g_cachedASC = nullptr;
        g_undoInFlight.store(false);
        return;
    }

    UObject* block = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_StackMutex);
        // Re-validate after infrastructure lookup
        while (!g_UndoStack.empty() && !IsObjectValid(g_UndoStack.back()))
            g_UndoStack.pop_back();
        if (g_UndoStack.empty())
        {
            g_undoInFlight.store(false);
            return;
        }
        block = g_UndoStack.back();
        g_UndoStack.pop_back();
    }

    if (!block || !IsObjectValid(block))
    {
        Output::send<LogLevel::Verbose>(STR("[BBT] Undo: block became invalid\n"));
        g_undoInFlight.store(false);
        return;
    }

    Output::send<LogLevel::Verbose>(STR("[BBT] Undoing: {}\n"), block->GetName());

    FName undoTag{STR("GAS.GameplayEvent.Building.Undo"), FNAME_Add};
    SendGameplayEventParams params{};
    params.Actor    = g_cachedASCOwner;
    params.EventTag = undoTag;
    *reinterpret_cast<FName*>(&params.Payload[0x00])   = undoTag;
    *reinterpret_cast<AActor**>(&params.Payload[0x08]) = g_cachedASCOwner;
    *reinterpret_cast<AActor**>(&params.Payload[0x10]) = g_cachedASCOwner;

    g_cachedASC->ProcessEvent(g_cachedSendFn, &params);
    Output::send<LogLevel::Verbose>(STR("[BBT] Undo event sent\n"));

    g_undoInFlight.store(false);
}

// ──────────────────────────────────────────────────────────────────────────────
// TryCopyObject — called on Shift+C (Phase 1: detect & log only)
// ──────────────────────────────────────────────────────────────────────────────
// UE TArray header (UE5 64-bit): { T* Data; int32 Num; int32 Max; }
struct FTArray
{
    uint8_t* Data;
    int32_t  Num;
    int32_t  Max;
};

// GetPlayerViewPoint(FVector& Location, FRotator& Rotation) — two 0x18 out-structs.
#pragma pack(push, 1)
struct GetViewPointParams
{
    double LocX, LocY, LocZ;          // FVector  Location (out)  @ 0x00
    double RotPitch, RotYaw, RotRoll; // FRotator Rotation (out)  @ 0x18
};
#pragma pack(pop)

// Cached UClass for AR5BuildingBlock (IsA covers subclasses: doors, torches, etc.)
static UClass* g_blockClass = nullptr;
static UClass* GetBlockClass()
{
    if (!g_blockClass)
        g_blockClass = reinterpret_cast<UClass*>(UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/R5.R5BuildingBlock")));
    return g_blockClass;
}

static UClass* g_singleTaskClass   = nullptr;
static UClass* g_fastBuildTaskClass = nullptr;

static UClass* GetSingleTaskClass()
{
    if (!g_singleTaskClass)
        g_singleTaskClass = reinterpret_cast<UClass*>(UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/R5.R5AbilityTask_FindConstructTarget_Single")));
    return g_singleTaskClass;
}

static UClass* GetFastBuildTaskClass()
{
    if (!g_fastBuildTaskClass)
        g_fastBuildTaskClass = reinterpret_cast<UClass*>(UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/R5.R5AbilityTask_FindConstructTarget_FastBuilding")));
    return g_fastBuildTaskClass;
}

// Cached lookups — these scan the whole UObject table, so we do them once and
// reuse. Re-find only if the cached pointer goes stale (world reload, etc.).
static UObject* g_cachedPC        = nullptr;
static UObject* g_cachedConstruct = nullptr;

static UObject* GetPlayerController()
{
    if (!IsObjectValid(g_cachedPC))
    {
        g_cachedPC = UObjectGlobals::FindFirstOf(STR("R5PlayerController"));
        if (!IsObjectValid(g_cachedPC))
            g_cachedPC = UObjectGlobals::FindFirstOf(STR("PlayerController"));
    }
    return g_cachedPC;
}

static UObject* GetConstructAbility()
{
    if (!IsObjectValid(g_cachedConstruct))
        g_cachedConstruct = UObjectGlobals::FindFirstOf(STR("R5Ability_Building_MakeConstructCommand"));
    return g_cachedConstruct;
}

// Free-build: continuously mirrors g_cfg.freeNoCost into R5BuildingSettings
// (CDO + instances), so the ImGui checkbox is fully live and reversible.
//   bBuildingResourcesValidation @ 0x051C → false  = build with no resource cost
// Original byte values are captured once so turning a toggle back off restores
// the game's real default instead of guessing at it.
struct FreeBuildTarget
{
    uint8_t* costByte;
    uint8_t  origCost;
};
static std::vector<FreeBuildTarget> g_freeTargets;
static std::vector<UObject*>        g_validationProfiles;
static bool g_freeTargetsCached = false;
static bool g_prevNoCost = false;

static void CacheFreeBuildTargets()
{
    if (g_freeTargetsCached) return;

    bool any = false;
    auto addTarget = [&](UObject* s)
    {
        uint8_t* b = reinterpret_cast<uint8_t*>(s);
        FreeBuildTarget t{};
        t.costByte     = b + 0x051C;
        t.origCost     = *t.costByte;
        g_freeTargets.push_back(t);
        any = true;
    };
    UObject* cdo = UObjectGlobals::StaticFindObject<UObject*>(
        nullptr, nullptr, STR("/Script/R5.Default__R5BuildingSettings"));
    if (IsObjectValid(cdo)) addTarget(cdo);
    std::vector<UObject*> inst;
    UObjectGlobals::FindAllOf(STR("R5BuildingSettings"), inst);
    for (UObject* s : inst) if (IsObjectValid(s)) addTarget(s);
    if (!any) return; // not loaded yet — retry next tick

    g_freeTargetsCached = true;
    Output::send<LogLevel::Verbose>(
        STR("[BBT] Free-build targets cached (settings={})\n"), g_freeTargets.size());
}

static void CacheValidationProfiles()
{
    if (!g_validationProfiles.empty()) return;
    UObjectGlobals::FindAllOf(STR("R5BuildValidationProfile"), g_validationProfiles);
    if (!g_validationProfiles.empty())
        Output::send<LogLevel::Verbose>(
            STR("[BBT] Validation profiles found (count={})\n"), g_validationProfiles.size());
}

// Decoration restriction bypass: patch RestrictionType from 0 (BuildingCenterRequirements)
// to 0xFF (invalid — game skips it) in each UR5BuildValidationProfile.Restrictions entry.
// Fully reversible: set back to 0 to restore the restriction.
struct DecorRestrictionPatch
{
    uint8_t* typeByte;
};
static std::vector<DecorRestrictionPatch> g_decorPatches;
static bool g_decorPatchesCached = false;

static void CacheDecorPatches()
{
    if (g_decorPatchesCached) return;
    if (g_validationProfiles.empty()) return;

    constexpr int ELEM = 0x1C;
    int profilesScanned = 0;
    for (UObject* p : g_validationProfiles)
    {
        if (!IsObjectValid(p)) continue;
        uint8_t* pb   = reinterpret_cast<uint8_t*>(p);
        uint8_t* data = *reinterpret_cast<uint8_t**>(pb + 0x0030);
        int32_t  num  = *reinterpret_cast<int32_t*>(pb + 0x0038);
        if (!data || num <= 0) continue;
        ++profilesScanned;
        for (int32_t i = 0; i < num; ++i)
        {
            uint8_t* typePtr = data + i * ELEM;
            uint8_t  typeVal = *typePtr;
            if (typeVal == 0) // BuildingCenterRequirements
            {
                g_decorPatches.push_back({typePtr});
                break;
            }
        }
    }
    g_decorPatchesCached = true;
    Output::send<LogLevel::Verbose>(
        STR("[BBT] Decor patches cached: {} profiles total, {} scanned, {} patches\n"),
        g_validationProfiles.size(), profilesScanned, g_decorPatches.size());
}

static void SyncDecorRestrictions(bool bypass)
{
    for (auto& p : g_decorPatches)
        *p.typeByte = bypass ? 0xFF : 0;
}

static void SyncFreeBuild()
{
    CacheFreeBuildTargets();
    if (!g_freeTargetsCached) return;

    bool noCost = g_cfg.freeNoCost;

    for (auto& t : g_freeTargets)
        *t.costByte = noCost ? 0 : t.origCost;

    if (noCost != g_prevNoCost)
    {
        Output::send<LogLevel::Verbose>(STR("[BBT] Free-build synced (no_cost={})\n"), noCost);
        g_prevNoCost = noCost;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// No stability — single-block patching
// ──────────────────────────────────────────────────────────────────────────────
// Two systems enforce stability:
//   1) DA_BuildRestrictionParams has a "Stability" restriction entry — we flip
//      its RestrictionType to 0xFF to skip the placement validation check.
//   2) Each R5BuildingItem has IntegritySettings (weight/loads) used by the
//      BuildingGraph for structural calculations. We patch ONLY the currently
//      selected brush item, not all ~860 items. The graph saves patched values
//      on placement, so placed blocks stay stable permanently.
//
// Old approach patched all items globally, which poisoned the BuildingGraph
// for every connected structure — toggling NSR off caused entire buildings to
// collapse. Single-block patching avoids this entirely.

constexpr uint8_t RESTRICTION_TYPE_STABILITY = 8;
constexpr uint8_t RESTRICTION_TYPE_DISABLED  = 0xFF;
constexpr int     VALIDATION_RESTRICTIONS_OFFSET = 0x0030;
constexpr int     RESTRICTION_ENTRY_SIZE = 0x1C;
constexpr int     INTEGRITY_OFFSET = 0x04BC;
constexpr float   STABILITY_MAX_LOAD = 10000000.0f;

static bool     g_stabilityProfileCached = false;
static bool     g_prevNoStability        = false;
static uint8_t* g_stabilityTypePtr       = nullptr;
static uint8_t  g_stabilityOrigType      = RESTRICTION_TYPE_STABILITY;

static UObject* g_nsrPatchedItem = nullptr;
static float    g_nsrOrigWeight  = 0.0f;
static float    g_nsrOrigHLoad   = 0.0f;
static float    g_nsrOrigVLoad   = 0.0f;
static float    g_nsrOrigMinExt  = 0.0f;

static void CacheStabilityProfile()
{
    if (g_stabilityProfileCached) return;

    std::vector<UObject*> profiles;
    UObjectGlobals::FindAllOf(STR("R5BuildValidationProfile"), profiles);
    for (UObject* prof : profiles)
    {
        if (!IsObjectValid(prof)) continue;
        File::StringType path = prof->GetPathName();
        if (path.find(STR("DA_BuildRestrictionParams")) == File::StringType::npos) continue;

        uint8_t* base = reinterpret_cast<uint8_t*>(prof);
        uint8_t* arrPtr = base + VALIDATION_RESTRICTIONS_OFFSET;
        uint8_t* data = *reinterpret_cast<uint8_t**>(arrPtr);
        int32_t  num  = *reinterpret_cast<int32_t*>(arrPtr + 8);

        for (int32_t i = 0; i < num; i++)
        {
            uint8_t* entry = data + (i * RESTRICTION_ENTRY_SIZE);
            if (*entry == RESTRICTION_TYPE_STABILITY)
            {
                g_stabilityTypePtr  = entry;
                g_stabilityOrigType = *entry;
                Output::send<LogLevel::Verbose>(
                    STR("[BBT] Stability restriction found in profile (entry {}/{})\n"), i, num);
                break;
            }
        }
        break;
    }

    g_stabilityProfileCached = true;
    Output::send<LogLevel::Verbose>(STR("[BBT] Stability profile cached (found={})\n"),
        g_stabilityTypePtr ? STR("YES") : STR("NO"));
}

static void NsrRestoreItem()
{
    if (!g_nsrPatchedItem) return;
    if (!IsObjectValid(g_nsrPatchedItem)) { g_nsrPatchedItem = nullptr; return; }

    uint8_t* b = reinterpret_cast<uint8_t*>(g_nsrPatchedItem) + INTEGRITY_OFFSET;
    *reinterpret_cast<float*>(b + 0x00) = g_nsrOrigWeight;
    *reinterpret_cast<float*>(b + 0x04) = g_nsrOrigHLoad;
    *reinterpret_cast<float*>(b + 0x08) = g_nsrOrigVLoad;
    *reinterpret_cast<float*>(b + 0x0C) = g_nsrOrigMinExt;

    Output::send<LogLevel::Verbose>(STR("[BBT] NSR restored: {}\n"), g_nsrPatchedItem->GetName());
    g_nsrPatchedItem = nullptr;
}

static void NsrPatchItem(UObject* item)
{
    if (!item || !IsObjectValid(item)) return;

    uint8_t* b = reinterpret_cast<uint8_t*>(item) + INTEGRITY_OFFSET;
    g_nsrOrigWeight = *reinterpret_cast<float*>(b + 0x00);
    g_nsrOrigHLoad  = *reinterpret_cast<float*>(b + 0x04);
    g_nsrOrigVLoad  = *reinterpret_cast<float*>(b + 0x08);
    g_nsrOrigMinExt = *reinterpret_cast<float*>(b + 0x0C);

    *reinterpret_cast<float*>(b + 0x00) = 0.0f;
    *reinterpret_cast<float*>(b + 0x04) = STABILITY_MAX_LOAD;
    *reinterpret_cast<float*>(b + 0x08) = STABILITY_MAX_LOAD;
    *reinterpret_cast<float*>(b + 0x0C) = 0.0f;

    g_nsrPatchedItem = item;
    Output::send<LogLevel::Verbose>(STR("[BBT] NSR patched: {} (w={:.1f} h={:.1f} v={:.1f})\n"),
        item->GetName(), g_nsrOrigWeight, g_nsrOrigHLoad, g_nsrOrigVLoad);
}

static UObject* GetCurrentBrushItem()
{
    if (!g_cachedConstruct || !IsObjectValid(g_cachedConstruct)) return nullptr;

    UObject* context = *reinterpret_cast<UObject**>(
        reinterpret_cast<uint8_t*>(g_cachedConstruct) + 0x03D0);
    if (!context || !IsObjectValid(context)) return nullptr;

    UObject* brush = *reinterpret_cast<UObject**>(
        reinterpret_cast<uint8_t*>(context) + 0x0098);
    if (!brush || !IsObjectValid(brush)) return nullptr;

    auto* compArr = reinterpret_cast<FTArray*>(
        reinterpret_cast<uint8_t*>(brush) + 0x00A8);
    if (!compArr->Data || compArr->Num <= 0) return nullptr;

    UObject* item = *reinterpret_cast<UObject**>(compArr->Data);
    return (item && IsObjectValid(item)) ? item : nullptr;
}

static void SyncStability()
{
    CacheStabilityProfile();

    bool noStability = g_cfg.freeNoStability;

    // Part 1: restriction profile — global toggle, safe
    if (g_stabilityTypePtr)
        *g_stabilityTypePtr = noStability ? RESTRICTION_TYPE_DISABLED : g_stabilityOrigType;

    // Part 2: single-block IntegritySettings
    if (!noStability)
    {
        NsrRestoreItem();
    }
    else
    {
        UObject* currentItem = GetCurrentBrushItem();
        if (currentItem != g_nsrPatchedItem)
        {
            NsrRestoreItem();
            NsrPatchItem(currentItem);
        }
    }

    if (noStability != g_prevNoStability)
    {
        Output::send<LogLevel::Verbose>(STR("[BBT] Stability {} (single-block mode)\n"),
            noStability ? STR("DISABLED") : STR("RESTORED"));
        g_prevNoStability = noStability;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Placement Freedom: shelter/roof/bonfire bypass
// ──────────────────────────────────────────────────────────────────────────────
// Patches UR5ShelterCheckSetup data assets at runtime to bypass roof restrictions.
//   - "Allow Under Roof" (furnace/kiln): set ShelterPercentThreshold > 1.0
//   - "No Roof Required" (workstations/beds): set MandatoryStrategies checker Length to 0
//   - "No Bonfire Required": bSkipBuildingCenterValidation + decoration restriction strip
//
// UR5ShelterCheckSetup layout (from CXX headers):
//   MandatoryStrategies  @ 0x0030  TArray<UObject*>
//   ShelterPercentThreshold @ 0x0040  float
//   ThresholdStrategies  @ 0x0048  TArray<UObject*>
// UR5ShelterChecker (base):
//   Length @ 0x0028  float
struct PlacementTarget
{
    float*    thresholdPtr;    // ShelterPercentThreshold for "not under roof" assets
    float     origThreshold;
    int32_t*  mandatoryNumPtr; // MandatoryStrategies.Num — zero it to skip all mandatory checks
    int32_t   origMandatoryNum;
};
static PlacementTarget g_placeFurnace{};
static PlacementTarget g_placeKiln{};
static PlacementTarget g_placeRoofReq{};
static bool g_placementCached = false;
static bool g_prevAllowUnderRoof = false;
static bool g_prevNoRoofRequired = false;
static bool g_prevNoBonfire      = false;

// Bonfire-only targets: bSkipBuildingCenterValidation on BuildingSettings
// Separate from g_freeTargets so we can toggle bonfire without touching decoration restrictions.
struct BonfireTarget
{
    uint8_t* anywhereByte;
    uint8_t  origAnywhere;
};
static std::vector<BonfireTarget> g_bonfireTargets;
static bool g_bonfireCached = false;

static UObject* FindShelterCheckSetup(const TCHAR* path)
{
    UObject* obj = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, path);
    if (obj && IsObjectValid(obj)) return obj;
    return nullptr;
}

static void CachePlacementTargets()
{
    if (g_placementCached) return;

    // Cache each target independently — they may load at different times.
    // Don't mark fully cached until ALL are resolved.
    if (!g_placeFurnace.thresholdPtr) {
        UObject* obj = FindShelterCheckSetup(
            STR("/Game/Gameplay/Comfort/Shelter/DA_ShelterCheckSetup_NotUnderRoof_Furnace.DA_ShelterCheckSetup_NotUnderRoof_Furnace"));
        if (obj) {
            uint8_t* b = reinterpret_cast<uint8_t*>(obj);
            g_placeFurnace.thresholdPtr = reinterpret_cast<float*>(b + 0x0040);
            g_placeFurnace.origThreshold = *g_placeFurnace.thresholdPtr;
            Output::send<LogLevel::Verbose>(STR("[BBT] Placement: furnace cached (threshold={})\n"), g_placeFurnace.origThreshold);
        }
    }
    if (!g_placeKiln.thresholdPtr) {
        UObject* obj = FindShelterCheckSetup(
            STR("/Game/Gameplay/Comfort/Shelter/DA_ShelterCheckSetup_NotUnderRoof_Kiln.DA_ShelterCheckSetup_NotUnderRoof_Kiln"));
        if (obj) {
            uint8_t* b = reinterpret_cast<uint8_t*>(obj);
            g_placeKiln.thresholdPtr = reinterpret_cast<float*>(b + 0x0040);
            g_placeKiln.origThreshold = *g_placeKiln.thresholdPtr;
            Output::send<LogLevel::Verbose>(STR("[BBT] Placement: kiln cached (threshold={})\n"), g_placeKiln.origThreshold);
        }
    }
    if (!g_placeRoofReq.mandatoryNumPtr) {
        UObject* obj = FindShelterCheckSetup(
            STR("/Game/Gameplay/Comfort/Shelter/DA_ShelterCheckSetup_RoofRequired.DA_ShelterCheckSetup_RoofRequired"));
        if (obj) {
            uint8_t* b = reinterpret_cast<uint8_t*>(obj);
            g_placeRoofReq.mandatoryNumPtr = reinterpret_cast<int32_t*>(b + 0x0030 + 0x08);
            g_placeRoofReq.origMandatoryNum = *g_placeRoofReq.mandatoryNumPtr;
            Output::send<LogLevel::Verbose>(STR("[BBT] Placement: roofReq cached (mandatoryNum={})\n"), g_placeRoofReq.origMandatoryNum);
        }
    }

    bool allFound = g_placeFurnace.thresholdPtr && g_placeKiln.thresholdPtr && g_placeRoofReq.mandatoryNumPtr;
    if (allFound) {
        g_placementCached = true;
        Output::send<LogLevel::Verbose>(STR("[BBT] Placement targets ALL cached\n"));
    }
}

static void CacheBonfireTargets()
{
    if (g_bonfireCached) return;

    auto addTarget = [](UObject* s) {
        uint8_t* b = reinterpret_cast<uint8_t*>(s);
        BonfireTarget t{};
        t.anywhereByte = b + 0x00B9;
        t.origAnywhere = *t.anywhereByte;
        g_bonfireTargets.push_back(t);
    };

    UObject* cdo = UObjectGlobals::StaticFindObject<UObject*>(
        nullptr, nullptr, STR("/Script/R5.Default__R5BuildingSettings"));
    if (!IsObjectValid(cdo)) return;
    addTarget(cdo);

    std::vector<UObject*> inst;
    UObjectGlobals::FindAllOf(STR("R5BuildingSettings"), inst);
    for (UObject* s : inst)
        if (IsObjectValid(s) && s != cdo) addTarget(s);

    g_bonfireCached = true;
    Output::send<LogLevel::Verbose>(
        STR("[BBT] Bonfire targets cached (count={})\n"), g_bonfireTargets.size());
}

static void SyncPlacementFreedom()
{
    CachePlacementTargets();
    CacheBonfireTargets();

    bool allowUnderRoof = g_cfg.placementAllowUnderRoof;
    bool noRoofRequired = g_cfg.placementNoRoofRequired;
    bool noBonfire      = g_cfg.placementNoBonfire;

    // Apply patches for whichever targets have been cached so far (they load independently)
    if (g_placeFurnace.thresholdPtr)
        *g_placeFurnace.thresholdPtr = allowUnderRoof ? 2.0f : g_placeFurnace.origThreshold;
    if (g_placeKiln.thresholdPtr)
        *g_placeKiln.thresholdPtr = allowUnderRoof ? 2.0f : g_placeKiln.origThreshold;
    if (g_placeRoofReq.mandatoryNumPtr)
        *g_placeRoofReq.mandatoryNumPtr = noRoofRequired ? 0 : g_placeRoofReq.origMandatoryNum;

    // No Bonfire: set bSkipBuildingCenterValidation AND strip decoration restrictions.
    if (g_bonfireCached)
    {
        for (auto& t : g_bonfireTargets)
            *t.anywhereByte = noBonfire ? 1 : t.origAnywhere;
    }
    CacheValidationProfiles();
    CacheDecorPatches();
    if (g_decorPatchesCached) SyncDecorRestrictions(noBonfire);

    if (allowUnderRoof != g_prevAllowUnderRoof || noRoofRequired != g_prevNoRoofRequired || noBonfire != g_prevNoBonfire)
    {
        Output::send<LogLevel::Verbose>(
            STR("[BBT] Placement freedom synced (underRoof={}, noRoof={}, noBonfire={})\n"),
            allowUnderRoof, noRoofRequired, noBonfire);
        g_prevAllowUnderRoof = allowUnderRoof;
        g_prevNoRoofRequired = noRoofRequired;
        g_prevNoBonfire = noBonfire;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Fine rotation cycle (roadmap item 8)
// ──────────────────────────────────────────────────────────────────────────────
// Quartermaster's "Building Degrees of Freedom" merges 1/5/10 into
// R5BuildingSettings.RotationSteps via a static config-ini patch (loose pak,
// no DLL). We get the same merged cycle live, without touching the TArray at
// all: read the vanilla steps once (read-only — no array growth/allocator
// risk) and advance UR5BuildingConstructionContext::RotationStep
// (UR5BuildingConstructionContext + 0x0140) through {vanilla} merged with
// whichever of {1,5,10} are currently enabled, on the player's own keybind.
// The game's own rotation-step input/array are never written to.
//
// 1/5/10 are independently toggleable (not one switch) because the game
// snaps to a nearby block's rotation within ~3 degrees — a stray 1-degree
// step can land just inside that window and silently misalign a wall from
// its neighbor. Users who hit that can drop 1 deg (or 5) without losing the
// rest of the cycle.
static std::vector<int32_t> g_vanillaRotationSteps;
static bool                 g_vanillaRotationStepsCached = false;

static void CacheVanillaRotationSteps()
{
    if (g_vanillaRotationStepsCached) return;

    UObject* cdo = UObjectGlobals::StaticFindObject<UObject*>(
        nullptr, nullptr, STR("/Script/R5.Default__R5BuildingSettings"));
    if (!IsObjectValid(cdo)) return; // not loaded yet — retry next press

    // RotationSteps: TArray<int32> @ UR5BuildingSettings + 0x0388 (read-only).
    auto* arr  = reinterpret_cast<FTArray*>(reinterpret_cast<uint8_t*>(cdo) + 0x0388);
    auto* data = reinterpret_cast<int32_t*>(arr->Data);

    g_vanillaRotationSteps.assign(data, data + arr->Num);
    std::sort(g_vanillaRotationSteps.begin(), g_vanillaRotationSteps.end());
    g_vanillaRotationStepsCached = true;

    Output::send<LogLevel::Verbose>(
        STR("[BBT] Vanilla rotation steps cached: {}\n"), g_vanillaRotationSteps.size());
}

// Rebuilt on every keypress (cheap — a handful of ints) so toggling 1/5/10 in
// the ImGui tab takes effect immediately, same as every other BBT toggle.
static std::vector<int32_t> BuildRotationCycle()
{
    std::set<int32_t> merged(g_vanillaRotationSteps.begin(), g_vanillaRotationSteps.end());
    if (g_cfg.rotation1Enabled)  merged.insert(1);
    if (g_cfg.rotation5Enabled)  merged.insert(5);
    if (g_cfg.rotation10Enabled) merged.insert(10);
    return std::vector<int32_t>(merged.begin(), merged.end());
}

// Pure lookup: given the step value the context had BEFORE this advance,
// return the next value in the merged cycle (wraps after the last/highest).
static int32_t NextInRotationCycle(const std::vector<int32_t>& cycle, int32_t current)
{
    auto found = std::find(cycle.begin(), cycle.end(), current);
    if (found != cycle.end())
    {
        size_t idx = static_cast<size_t>(std::distance(cycle.begin(), found) + 1) % cycle.size();
        return cycle[idx];
    }
    // Current value isn't an exact cycle member (shouldn't normally happen) —
    // land on the next-greater entry, or wrap to the first.
    auto gt = std::upper_bound(cycle.begin(), cycle.end(), current);
    return (gt == cycle.end()) ? cycle.front() : *gt;
}

// Prefer the reflected Set/GetRotationStep accessors (same naming pattern as
// the already-proven SetBuildingBrush) so any UI/delegate listeners stay in
// sync. Fall back to a raw field write if it turns out not to be reflected.
static void ApplyRotationStep(UObject* constructionContext, int32_t newStep)
{
    UFunction* setFn = constructionContext->GetFunctionByNameInChain(STR("SetRotationStep"));
    if (setFn)
    {
        struct { int32_t StepValue; } params{ newStep };
        constructionContext->ProcessEvent(setFn, &params);
    }
    else
    {
        *reinterpret_cast<int32_t*>(
            reinterpret_cast<uint8_t*>(constructionContext) + 0x0140) = newStep;
    }
}

// ── Hook the game's OWN rotation-step input handler ────────────────────────
// Rather than give BBT a separate keybind, hook
// UR5Ability_Building_MakeConstructCommand::OnChangeRotationStep — the real
// UFUNCTION the engine's Enhanced Input system calls when the player presses
// WHATEVER key they have bound to "Rotation Step" in the game's own
// keybindings menu. That way there's exactly one key to remember, and
// rebinding it in-game just works with no separate config to keep in sync.
//
// RegisterHook always still runs the original function (there's no "cancel"),
// so: the PRE callback only READS+stashes RotationStep (before vanilla's own
// array-only cycling overwrites it); the POST callback then computes the true
// next value from OUR merged cycle (based on the stashed pre-value) and
// overwrites whatever vanilla just set. Net effect: the player's own key
// drives our merged cycle instead of the vanilla-only one.
//
// Threading: this fires from the engine's native Enhanced Input processing on
// the GAME THREAD (NOT UE4SS's separate input thread that register_keydown_event
// uses) — so touching live UObjects/ProcessEvent directly here is safe, same
// as any other native UFunction hook.
static int32_t g_rotationStepPreValue   = 0;
static bool    g_rotationHookRegistered = false;

static bool BBT_AnyFineStepEnabled()
{
    return g_cfg.rotation1Enabled || g_cfg.rotation5Enabled || g_cfg.rotation10Enabled;
}

static void BBT_RotationStepPre(UnrealScriptFunctionCallableContext& ctx, void* /*customData*/)
{
    if (!BBT_AnyFineStepEnabled()) return;
    UObject* ability = ctx.Context;
    if (!IsObjectValid(ability)) return;
    UObject* context = *reinterpret_cast<UObject**>(
        reinterpret_cast<uint8_t*>(ability) + 0x03D0); // ConstructionContext
    if (!IsObjectValid(context)) return;
    g_rotationStepPreValue = *reinterpret_cast<int32_t*>(
        reinterpret_cast<uint8_t*>(context) + 0x0140); // RotationStep, before vanilla runs
}

static void BBT_RotationStepPost(UnrealScriptFunctionCallableContext& ctx, void* /*customData*/)
{
    // All three off — leave vanilla's own result untouched rather than
    // reimplementing its cycling and risk a redundant/mismatched overwrite.
    if (!BBT_AnyFineStepEnabled()) return;
    UObject* ability = ctx.Context;
    if (!IsObjectValid(ability)) return;
    UObject* context = *reinterpret_cast<UObject**>(
        reinterpret_cast<uint8_t*>(ability) + 0x03D0);
    if (!IsObjectValid(context)) return;

    CacheVanillaRotationSteps();
    if (g_vanillaRotationSteps.empty()) return;

    auto    cycle = BuildRotationCycle();
    int32_t next  = NextInRotationCycle(cycle, g_rotationStepPreValue);
    ApplyRotationStep(context, next);
    g_copyAngleHold = false; // player manually rotated — release the match-angle lock
    Output::send<LogLevel::Verbose>(
        STR("[BBT] Rotation step (game key): {} -> {}\n"), g_rotationStepPreValue, next);
}

// Registers the hook once OnChangeRotationStep is resolvable (same class as
// the already-proven SetBuildingBrush lookup, so it's safe once the construct
// ability instance exists — see the JIT-loading caveat in project notes:
// game-class UFunctions aren't guaranteed loaded until an instance spawns).
static void EnsureRotationHook()
{
    if (g_rotationHookRegistered) return;
    UObject* construct = GetConstructAbility();
    if (!IsObjectValid(construct)) return; // not loaded yet — retry next tick

    UFunction* fn = construct->GetFunctionByNameInChain(STR("OnChangeRotationStep"));
    if (!fn) return; // not resolvable yet — retry next tick

    UObjectGlobals::RegisterHook(fn, BBT_RotationStepPre, BBT_RotationStepPost, nullptr);
    g_rotationHookRegistered = true;
    Output::send<LogLevel::Verbose>(STR("[BBT] Rotation-step hook registered\n"));
}

// Build-mode detection (game thread). The construct ability's StrategyTask
// (UR5AbilityTask_FindConstructTarget* @ 0x03D8) exists only while build mode is
// active. When it goes null→non-null we (re)entered build mode → clear the stale
// undo stack, because the game resets its own undo history on build-mode exit.
// Only uses the CACHED construct ptr (no per-frame object-table scan).
static bool g_inBuildPrev = false;
static void CheckBuildMode()
{
    if (!IsObjectValid(g_cachedConstruct)) {
        if (g_cachedConstruct) {
            g_cachedConstruct = nullptr;
            g_cachedPC = nullptr;
            if (g_freeTargetsCached) {
                g_freeTargets.clear();
                g_validationProfiles.clear();
                g_freeTargetsCached = false;
                g_prevNoCost = false;
            }
            if (g_decorPatchesCached) {
                g_decorPatches.clear();
                g_decorPatchesCached = false;
            }
            NsrRestoreItem();
            if (g_stabilityProfileCached) {
                if (g_stabilityTypePtr) *g_stabilityTypePtr = g_stabilityOrigType;
                g_stabilityTypePtr = nullptr;
                g_stabilityProfileCached = false;
                g_prevNoStability = false;
            }
            if (g_placementCached) {
                g_placeFurnace = {};
                g_placeKiln = {};
                g_placeRoofReq = {};
                g_placementCached = false;
                g_prevAllowUnderRoof = false;
                g_prevNoRoofRequired = false;
            }
            if (g_bonfireCached) {
                g_bonfireTargets.clear();
                g_bonfireCached = false;
                g_prevNoBonfire = false;
            }
            Output::send<LogLevel::Verbose>(STR("[BBT] World teardown detected — caches invalidated\n"));
        }
        GetConstructAbility();
        if (!IsObjectValid(g_cachedConstruct)) {
            g_inBuildPrev = false;
            return;
        }
    }
    UObject* strategyTask = *reinterpret_cast<UObject**>(
        reinterpret_cast<uint8_t*>(g_cachedConstruct) + 0x03D8);
    bool inBuild = (strategyTask != nullptr);
    if (inBuild != g_inBuildPrev)
        Output::send<LogLevel::Verbose>(STR("[BBT-diag] build mode {}\n"), inBuild ? STR("ENTER") : STR("EXIT"));
    if (inBuild && !g_inBuildPrev)
    {
        std::lock_guard<std::mutex> lock(g_StackMutex);
        if (!g_UndoStack.empty())
        {
            g_UndoStack.clear();
            Output::send<LogLevel::Verbose>(STR("[BBT] Undo stack reset (build mode reopened)\n"));
        }
    }
    g_inBuildPrev = inBuild;
}

// Physics line trace via UKismetSystemLibrary::LineTraceSingle.
// Params are written at their REAL runtime offsets (looked up from the UFunction),
// so there's no hand-alignment risk. Returns the hit building-block actor or null.
static UObject* TraceForBlock(UObject* worldCtx,
                              double cx, double cy, double cz,
                              double dx, double dy, double dz)
{
    // Cache the UFunction + param offsets once — they never change, and the
    // path lookup + property scan are too costly to repeat every keypress.
    static UFunction* fn = nullptr;
    static int32_t oWctx=-1, oStart=-1, oEnd=-1, oChannel=-1,
                   oComplex=-1, oIgnoreSelf=-1, oDraw=-1, oOutHit=-1, oRet=-1;
    if (!fn)
    {
        UObject* fnObj = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:LineTraceSingle"));
        fn = reinterpret_cast<UFunction*>(fnObj);
        if (!fn)
        {
            Output::send<LogLevel::Verbose>(STR("[CopyObject] LineTraceSingle UFunction not found\n"));
            return nullptr;
        }
        for (auto* prop : fn->ForEachProperty())
        {
            auto nm = prop->GetName(); int32_t o = prop->GetOffset_Internal();
            if      (nm == STR("WorldContextObject")) oWctx = o;
            else if (nm == STR("Start"))         oStart = o;
            else if (nm == STR("End"))           oEnd = o;
            else if (nm == STR("TraceChannel"))  oChannel = o;
            else if (nm == STR("bTraceComplex")) oComplex = o;
            else if (nm == STR("bIgnoreSelf"))   oIgnoreSelf = o;
            else if (nm == STR("DrawDebugType")) oDraw = o;
            else if (nm == STR("OutHit"))        oOutHit = o;
            else if (nm == STR("ReturnValue"))   oRet = o;
        }
    }
    if (oOutHit < 0 || oStart < 0 || oEnd < 0 || oWctx < 0)
    {
        Output::send<LogLevel::Verbose>(STR("[CopyObject] trace: param offsets not resolved\n"));
        return nullptr;
    }

    constexpr double REACH = 5000.0; // 50m
    double ex = cx + dx * REACH, ey = cy + dy * REACH, ez = cz + dz * REACH;

    alignas(16) uint8_t buf[0x400] = {};
    *reinterpret_cast<UObject**>(buf + oWctx) = worldCtx;
    { auto* v = reinterpret_cast<double*>(buf + oStart); v[0]=cx; v[1]=cy; v[2]=cz; }
    { auto* v = reinterpret_cast<double*>(buf + oEnd);   v[0]=ex; v[1]=ey; v[2]=ez; }
    buf[oChannel] = 0;                          // ETraceTypeQuery1 (Visibility)
    if (oComplex    >= 0) buf[oComplex]    = 0;
    if (oIgnoreSelf >= 0) buf[oIgnoreSelf] = 1;
    if (oDraw       >= 0) buf[oDraw]       = 0; // EDrawDebugTrace::None

    worldCtx->ProcessEvent(fn, buf);

    bool hit = (oRet >= 0) && buf[oRet] != 0;
    if (!hit) { Output::send<LogLevel::Verbose>(STR("[CopyObject] trace: nothing hit\n")); return nullptr; }

    uint8_t* hr = buf + oOutHit;
    // FHitResult: Component (TWeakObjectPtr<UPrimitiveComponent>) @ +0xD8
    UObject* comp  = reinterpret_cast<FWeakObjectPtr*>(hr + 0xD8)->Get();
    UObject* actor = nullptr;
    if (comp && IsObjectValid(comp))
        actor = comp->GetTypedOuter<AActor>();
    if (!actor)  // fallback: HitObjectHandle.ReferenceObject @ +0xB8
        actor = reinterpret_cast<FWeakObjectPtr*>(hr + 0xB8)->Get();
    if (!actor || !IsObjectValid(actor))
    {
        Output::send<LogLevel::Verbose>(STR("[CopyObject] trace: hit but no actor resolved\n"));
        return nullptr;
    }

    UClass* blockClass = GetBlockClass();
    if (blockClass && !actor->IsA(blockClass))
    {
        Output::send<LogLevel::Verbose>(
            STR("[CopyObject] trace hit non-block: {}\n"), actor->GetName());
        return nullptr;
    }
    Output::send<LogLevel::Verbose>(STR("[CopyObject] TRACE hit block: {}\n"), actor->GetName());
    return actor;
}

static void TryCopyObject()
{
    // 0. Need the construct ability (cached). Absent only before any build session.
    UObject* construct = GetConstructAbility();
    if (!IsObjectValid(construct))
        return; // silent no-op

    // 1. Local player controller (cached) → camera view point
    UObject* pc = GetPlayerController();
    if (!IsObjectValid(pc))
    {
        Output::send<LogLevel::Verbose>(STR("[CopyObject] No PlayerController found\n"));
        return;
    }

    UFunction* vpFn = pc->GetFunctionByNameInChain(STR("GetPlayerViewPoint"));
    if (!vpFn)
    {
        Output::send<LogLevel::Verbose>(STR("[CopyObject] GetPlayerViewPoint not found\n"));
        return;
    }

    GetViewPointParams vp{};
    pc->ProcessEvent(vpFn, &vp);

    // Camera forward from rotation (UE: Z-up, degrees)
    double pitch = vp.RotPitch * EYE_PI / 180.0;
    double yaw   = vp.RotYaw   * EYE_PI / 180.0;
    double cp    = std::cos(pitch);
    double dirX  = cp * std::cos(yaw);
    double dirY  = cp * std::sin(yaw);
    double dirZ  = std::sin(pitch);

    Output::send<LogLevel::Verbose>(
        STR("[CopyObject] cam=({:.0f},{:.0f},{:.0f}) dir=({:.2f},{:.2f},{:.2f})\n"),
        vp.LocX, vp.LocY, vp.LocZ, dirX, dirY, dirZ);

    // 2. Physics line trace (pixel-accurate, hits the actual mesh — single call).
    UObject* best = TraceForBlock(pc, vp.LocX, vp.LocY, vp.LocZ, dirX, dirY, dirZ);
    if (!best)
    {
        Output::send<LogLevel::Verbose>(STR("[CopyObject] No block under crosshair\n"));
        return;
    }

    // BuildingItem at offset 0x0328 on AR5BuildingBlock
    UObject* item = *reinterpret_cast<UObject**>(
        reinterpret_cast<uint8_t*>(best) + 0x0328);
    if (!item || !IsObjectValid(item))
    {
        Output::send<LogLevel::Verbose>(STR("[CopyObject]   BuildingItem: <null>\n"));
        return;
    }
    Output::send<LogLevel::Verbose>(STR("[CopyObject]   BuildingItem: {}\n"), item->GetName());

    // ── Phase 2: swap the active build selection to this item's brush ─────────
    // (construct ability already validated at the top — we're in build mode)
    // AvailableBrushes: TArray<UR5BuildingBrush*> @ 0x03E0
    auto* brushArr = reinterpret_cast<FTArray*>(
        reinterpret_cast<uint8_t*>(construct) + 0x03E0);
    UObject** brushes  = reinterpret_cast<UObject**>(brushArr->Data);
    int32_t   brushNum = brushArr->Num;
    Output::send<LogLevel::Verbose>(STR("[CopyObject] AvailableBrushes={}\n"), brushNum);
    if (!brushes || brushNum <= 0) return;

    // Find the brush whose component Item matches the looked-at block's BuildingItem.
    // (DataAssets are singletons, so pointer equality holds.)
    UObject* matchBrush = nullptr;
    for (int32_t i = 0; i < brushNum && !matchBrush; ++i)
    {
        UObject* brush = brushes[i];
        if (!IsObjectValid(brush)) continue;
        // Components: TArray<FR5BuildingBrushComponent> @ 0x00A8, element 0xD0, Item @ +0x00
        auto*    compArr  = reinterpret_cast<FTArray*>(
            reinterpret_cast<uint8_t*>(brush) + 0x00A8);
        uint8_t* compData = compArr->Data;
        if (!compData) continue;
        for (int32_t c = 0; c < compArr->Num; ++c)
        {
            UObject* compItem = *reinterpret_cast<UObject**>(compData + c * 0xD0);
            if (compItem == item) { matchBrush = brush; break; }
        }
    }

    if (!matchBrush)
    {
        Output::send<LogLevel::Verbose>(
            STR("[CopyObject] No brush matches this item (not in your build list?)\n"));
        return;
    }

    // Call SetBuildingBrush(UR5BuildingBrush*) — single pointer param.
    UFunction* setFn = construct->GetFunctionByNameInChain(STR("SetBuildingBrush"));
    if (!setFn)
    {
        Output::send<LogLevel::Verbose>(STR("[CopyObject] SetBuildingBrush UFunction not found\n"));
        return;
    }
    struct { UObject* Brush; } setParams{ matchBrush };
    construct->ProcessEvent(setFn, &setParams);
    Output::send<LogLevel::Verbose>(
        STR("[CopyObject] >>> Swapped build selection to {} <<<\n"), item->GetName());
}

// ──────────────────────────────────────────────────────────────────────────────
// TryMatchAngle — called on Alt+C (roadmap item 7)
// ──────────────────────────────────────────────────────────────────────────────
// Separate keybind from copy object on purpose (user call): Alt+C, not folded
// into Shift+C. Reuses copy object's physics trace to find whatever block
// is under the crosshair, reads its world rotation via the reflected, BlueprintPure
// AActor::K2_GetActorRotation (confirmed in Engine.hpp), converts Euler->quaternion
// the same way the engine does (RC::Unreal::FRotator::Quaternion()), and writes the
// result straight into the construction context's live brush rotation via the
// reflected RotateBrush(const FQuat&) setter — the same accessor pattern already
// proven for SetBuildingBrush/SetRotationStep.
// RotateBrush(const FQuat&) is a DELTA operation: it computes
//   BrushQuaternion = current × param  (Hamilton product, confirmed from readback logs).
// To SET an absolute target we must pass delta = conjugate(current) × target so that
//   current × delta = current × (conjugate(current) × target) = target.
// conjugate of unit quat (cx,cy,cz,cw) = (-cx,-cy,-cz,cw).
static void ApplyBrushQuaternion(UObject* constructionContext, double tx, double ty, double tz, double tw, bool log)
{
    // Read current BrushQuaternion (@ +0x00E0, 4 doubles XYZW).
    auto* qField = reinterpret_cast<double*>(
        reinterpret_cast<uint8_t*>(constructionContext) + 0x00E0);
    double cx = qField[0], cy = qField[1], cz = qField[2], cw = qField[3];

    if (std::isnan(cx) || std::isnan(cy) || std::isnan(cz) || std::isnan(cw))
    {
        qField[0] = tx; qField[1] = ty; qField[2] = tz; qField[3] = tw;
        if (log) Output::send<LogLevel::Verbose>(STR("[CopyAngle] NaN recovery — raw write to ({:.3f},{:.3f},{:.3f},{:.3f})\n"), tx, ty, tz, tw);
        return;
    }

    // conjugate(current) = (-cx,-cy,-cz,cw)
    // delta = conjugate(current) * target  (Hamilton product)
    double ix = -cx, iy = -cy, iz = -cz, iw = cw;
    double dx = iw*tx + ix*tw + iy*tz - iz*ty;
    double dy = iw*ty + iy*tw + iz*tx - ix*tz;
    double dz = iw*tz + iz*tw + ix*ty - iy*tx;
    double dw = iw*tw - ix*tx - iy*ty - iz*tz;

    UFunction* rotFn = constructionContext->GetFunctionByNameInChain(STR("RotateBrush"));
    if (rotFn)
    {
        struct { double X, Y, Z, W; } params{ dx, dy, dz, dw };
        constructionContext->ProcessEvent(rotFn, &params);
        if (log) Output::send<LogLevel::Verbose>(
            STR("[CopyAngle] RotateBrush: current=({:.3f},{:.3f},{:.3f},{:.3f}) target=({:.3f},{:.3f},{:.3f},{:.3f}) result=({:.3f},{:.3f},{:.3f},{:.3f})\n"),
            cx, cy, cz, cw, tx, ty, tz, tw, qField[0], qField[1], qField[2], qField[3]);
    }
    else
    {
        // RotateBrush not reflected — raw absolute write directly into BrushQuaternion.
        qField[0] = tx; qField[1] = ty; qField[2] = tz; qField[3] = tw;
        if (log) Output::send<LogLevel::Verbose>(
            STR("[CopyAngle] RotateBrush NOT FOUND, raw write to ({:.3f},{:.3f},{:.3f},{:.3f})\n"),
            tx, ty, tz, tw);
    }
}

// Reapplied every Post-tick while g_copyAngleHold is true.
// Keeps BrushQuaternion at the target so placement lands at the copied angle.
// Ghost visual does NOT update live (known limitation — toggle destroy/build
// mode to force re-init). See project notes for full history of attempts.
static void SyncMatchAngle()
{
    if (!g_copyAngleHold) return;
    UObject* construct = g_cachedConstruct;
    if (!IsObjectValid(construct)) { g_copyAngleHold = false; return; }
    UObject* context = *reinterpret_cast<UObject**>(
        reinterpret_cast<uint8_t*>(construct) + 0x03D0);
    if (!IsObjectValid(context))   { g_copyAngleHold = false; return; }

    ApplyBrushQuaternion(context,
        g_copyAngleTarget.X, g_copyAngleTarget.Y,
        g_copyAngleTarget.Z, g_copyAngleTarget.W, false);
}

static void TryMatchAngle()
{
    UObject* construct = GetConstructAbility();
    if (!IsObjectValid(construct))
        return; // silent no-op, not in build mode

    UObject* pc = GetPlayerController();
    if (!IsObjectValid(pc))
    {
        Output::send<LogLevel::Verbose>(STR("[CopyAngle] No PlayerController found\n"));
        return;
    }

    UFunction* vpFn = pc->GetFunctionByNameInChain(STR("GetPlayerViewPoint"));
    if (!vpFn)
    {
        Output::send<LogLevel::Verbose>(STR("[CopyAngle] GetPlayerViewPoint not found\n"));
        return;
    }
    GetViewPointParams vp{};
    pc->ProcessEvent(vpFn, &vp);

    double pitch = vp.RotPitch * EYE_PI / 180.0;
    double yaw   = vp.RotYaw   * EYE_PI / 180.0;
    double cp    = std::cos(pitch);
    double dirX  = cp * std::cos(yaw);
    double dirY  = cp * std::sin(yaw);
    double dirZ  = std::sin(pitch);

    UObject* best = TraceForBlock(pc, vp.LocX, vp.LocY, vp.LocZ, dirX, dirY, dirZ);
    if (!best)
    {
        Output::send<LogLevel::Verbose>(STR("[CopyAngle] No block under crosshair\n"));
        return;
    }

    UFunction* rotFn = best->GetFunctionByNameInChain(STR("K2_GetActorRotation"));
    if (!rotFn)
    {
        Output::send<LogLevel::Verbose>(STR("[CopyAngle] K2_GetActorRotation not found\n"));
        return;
    }
    struct { double Pitch, Yaw, Roll; } rot{};
    best->ProcessEvent(rotFn, &rot);

    FRotator rotator(rot.Pitch, rot.Yaw, rot.Roll);
    FQuat    quat = rotator.Quaternion();

    // ConstructionContext @ UR5Ability_Building_MakeConstructCommand + 0x03D0
    // (same field the rotation-step hook reads).
    UObject* context = *reinterpret_cast<UObject**>(
        reinterpret_cast<uint8_t*>(construct) + 0x03D0);
    if (!IsObjectValid(context))
    {
        Output::send<LogLevel::Verbose>(STR("[CopyAngle] No ConstructionContext\n"));
        return;
    }

    g_copyAngleTarget = { quat.GetX(), quat.GetY(), quat.GetZ(), quat.GetW(), rot.Pitch, rot.Yaw, rot.Roll };
    g_copyAngleHold   = true;
    Output::send<LogLevel::Verbose>(
        STR("[CopyAngle] Locking rotation to {} (pitch={:.1f} yaw={:.1f} roll={:.1f}) quat=({:.3f},{:.3f},{:.3f},{:.3f})\n"),
        best->GetName(), rot.Pitch, rot.Yaw, rot.Roll,
        g_copyAngleTarget.X, g_copyAngleTarget.Y, g_copyAngleTarget.Z, g_copyAngleTarget.W);
    // First immediate application with diagnostic logging (before/after readback).
    ApplyBrushQuaternion(context, g_copyAngleTarget.X, g_copyAngleTarget.Y,
                         g_copyAngleTarget.Z, g_copyAngleTarget.W, true);
}

// ──────────────────────────────────────────────────────────────────────────────
// Looked-at block rotation cache (throttled trace from PostTick)
// ──────────────────────────────────────────────────────────────────────────────
static struct {
    double yaw{0.0};
    std::string name;
    bool   valid{false};
    int    frameSkip{0};
} g_lookAtData;

static UObject* TraceForBlockQuiet(UObject* worldCtx,
                                   double cx, double cy, double cz,
                                   double dx, double dy, double dz)
{
    static UFunction* fn = nullptr;
    static int32_t oWctx=-1, oStart=-1, oEnd=-1, oChannel=-1,
                   oComplex=-1, oIgnoreSelf=-1, oDraw=-1, oOutHit=-1, oRet=-1;
    if (!fn)
    {
        UObject* fnObj = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:LineTraceSingle"));
        fn = reinterpret_cast<UFunction*>(fnObj);
        if (!fn) return nullptr;
        for (auto* prop : fn->ForEachProperty())
        {
            auto nm = prop->GetName(); int32_t o = prop->GetOffset_Internal();
            if      (nm == STR("WorldContextObject")) oWctx = o;
            else if (nm == STR("Start"))         oStart = o;
            else if (nm == STR("End"))           oEnd = o;
            else if (nm == STR("TraceChannel"))  oChannel = o;
            else if (nm == STR("bTraceComplex")) oComplex = o;
            else if (nm == STR("bIgnoreSelf"))   oIgnoreSelf = o;
            else if (nm == STR("DrawDebugType")) oDraw = o;
            else if (nm == STR("OutHit"))        oOutHit = o;
            else if (nm == STR("ReturnValue"))   oRet = o;
        }
    }
    if (oOutHit < 0 || oStart < 0 || oEnd < 0 || oWctx < 0) return nullptr;

    constexpr double REACH = 5000.0;
    double ex = cx + dx * REACH, ey = cy + dy * REACH, ez = cz + dz * REACH;

    alignas(16) uint8_t buf[0x400] = {};
    *reinterpret_cast<UObject**>(buf + oWctx) = worldCtx;
    { auto* v = reinterpret_cast<double*>(buf + oStart); v[0]=cx; v[1]=cy; v[2]=cz; }
    { auto* v = reinterpret_cast<double*>(buf + oEnd);   v[0]=ex; v[1]=ey; v[2]=ez; }
    buf[oChannel] = 0;
    if (oComplex    >= 0) buf[oComplex]    = 0;
    if (oIgnoreSelf >= 0) buf[oIgnoreSelf] = 1;
    if (oDraw       >= 0) buf[oDraw]       = 0;

    worldCtx->ProcessEvent(fn, buf);

    bool hit = (oRet >= 0) && buf[oRet] != 0;
    if (!hit) return nullptr;

    uint8_t* hr = buf + oOutHit;
    UObject* comp  = reinterpret_cast<FWeakObjectPtr*>(hr + 0xD8)->Get();
    UObject* actor = nullptr;
    if (comp && IsObjectValid(comp))
        actor = comp->GetTypedOuter<AActor>();
    if (!actor)
        actor = reinterpret_cast<FWeakObjectPtr*>(hr + 0xB8)->Get();
    if (!actor || !IsObjectValid(actor)) return nullptr;

    UClass* blockClass = GetBlockClass();
    if (blockClass && !actor->IsA(blockClass)) return nullptr;
    return actor;
}

static void UpdateLookAtData()
{
    if (!g_inBuildPrev || !g_cfg.bstatEnabled) { g_lookAtData.valid = false; return; }
    if (++g_lookAtData.frameSkip < 6) return; // ~10 Hz at 60fps
    g_lookAtData.frameSkip = 0;

    UObject* pc = GetPlayerController();
    if (!IsObjectValid(pc)) { g_lookAtData.valid = false; return; }

    UFunction* vpFn = pc->GetFunctionByNameInChain(STR("GetPlayerViewPoint"));
    if (!vpFn) { g_lookAtData.valid = false; return; }

    GetViewPointParams vp{};
    pc->ProcessEvent(vpFn, &vp);

    double pitch = vp.RotPitch * EYE_PI / 180.0;
    double yaw   = vp.RotYaw   * EYE_PI / 180.0;
    double cp    = std::cos(pitch);

    UObject* block = TraceForBlockQuiet(pc, vp.LocX, vp.LocY, vp.LocZ,
        cp * std::cos(yaw), cp * std::sin(yaw), std::sin(pitch));

    if (!block) { g_lookAtData.valid = false; return; }

    UFunction* rotFn = block->GetFunctionByNameInChain(STR("K2_GetActorRotation"));
    if (!rotFn) { g_lookAtData.valid = false; return; }

    struct { double Pitch, Yaw, Roll; } rot{};
    block->ProcessEvent(rotFn, &rot);

    g_lookAtData.yaw   = rot.Yaw;

    UObject* item = *reinterpret_cast<UObject**>(
        reinterpret_cast<uint8_t*>(block) + 0x0328);
    if (item && IsObjectValid(item))
    {
        auto wname = item->GetName();
        g_lookAtData.name.clear();
        g_lookAtData.name.reserve(wname.size());
        for (auto wc : wname)
            g_lookAtData.name.push_back(static_cast<char>(wc < 128 ? wc : '?'));
    }
    else
    {
        g_lookAtData.name.clear();
    }

    g_lookAtData.valid  = true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Lua bridge — BStat data + config get/set
// ──────────────────────────────────────────────────────────────────────────────

// Quaternion → yaw in degrees, normalized to [0, 360)
static double QuatToYawDeg(double x, double y, double z, double w)
{
    double yaw = std::atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z));
    double deg = yaw * 180.0 / EYE_PI;
    if (deg < 0.0) deg += 360.0;
    return deg;
}

static std::string KeyToString(Key k, bool shift, bool alt)
{
    std::string result;
    if (shift) result += "Shift+";
    if (alt)   result += "Alt+";
    char c = static_cast<char>(k);
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        result += c;
    else
        result += '?';
    return result;
}

static int lua_BBT_GetBuildStatus(lua_State* L)
{
    lua_newtable(L);

    lua_pushboolean(L, g_inBuildPrev);
    lua_setfield(L, -2, "inBuildMode");

    lua_pushboolean(L, g_cfg.bstatEnabled.load());
    lua_setfield(L, -2, "bstatEnabled");

    if (g_inBuildPrev && IsObjectValid(g_cachedConstruct))
    {
        UObject* context = *reinterpret_cast<UObject**>(
            reinterpret_cast<uint8_t*>(g_cachedConstruct) + 0x03D0);
        if (IsObjectValid(context))
        {
            double* quat = reinterpret_cast<double*>(
                reinterpret_cast<uint8_t*>(context) + 0x00E0);
            lua_pushnumber(L, QuatToYawDeg(quat[0], quat[1], quat[2], quat[3]));
            lua_setfield(L, -2, "rotationYaw");

            int32_t step = *reinterpret_cast<int32_t*>(
                reinterpret_cast<uint8_t*>(context) + 0x0140);
            lua_pushinteger(L, step);
            lua_setfield(L, -2, "rotationStep");

            int32_t snap = *reinterpret_cast<int32_t*>(
                reinterpret_cast<uint8_t*>(context) + 0x0144);
            lua_pushinteger(L, snap);
            lua_setfield(L, -2, "snappingMode");

            // Current brush item name
            UObject* brush = *reinterpret_cast<UObject**>(
                reinterpret_cast<uint8_t*>(context) + 0x0098);
            if (IsObjectValid(brush))
            {
                auto* compArr = reinterpret_cast<FTArray*>(
                    reinterpret_cast<uint8_t*>(brush) + 0x00A8);
                if (compArr->Data && compArr->Num > 0)
                {
                    UObject* item = *reinterpret_cast<UObject**>(compArr->Data);
                    if (item && IsObjectValid(item))
                    {
                        auto name = item->GetName();
                        std::string narrow;
                        narrow.reserve(name.size());
                        for (auto wc : name)
                            narrow.push_back(static_cast<char>(wc < 128 ? wc : '?'));
                        lua_pushstring(L, narrow.c_str());
                        lua_setfield(L, -2, "itemName");
                    }
                }
            }
        }
    }

    lua_pushboolean(L, g_copyAngleHold.load());
    lua_setfield(L, -2, "copyAngleHeld");

    if (g_copyAngleHold)
    {
        double tgtYaw = g_copyAngleTarget.RotYaw;
        if (tgtYaw < 0.0) tgtYaw += 360.0;
        lua_pushnumber(L, tgtYaw);
        lua_setfield(L, -2, "copyAngleYaw");
    }

    lua_pushboolean(L, g_lookAtData.valid);
    lua_setfield(L, -2, "lookAtValid");

    if (g_lookAtData.valid)
    {
        double lay = g_lookAtData.yaw;
        if (lay < 0.0) lay += 360.0;
        lua_pushnumber(L, lay);
        lua_setfield(L, -2, "lookAtYaw");
        if (!g_lookAtData.name.empty())
        {
            lua_pushstring(L, g_lookAtData.name.c_str());
            lua_setfield(L, -2, "lookAtName");
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_StackMutex);
        lua_pushinteger(L, static_cast<int>(g_UndoStack.size()));
        lua_setfield(L, -2, "undoCount");
    }
    lua_pushinteger(L, g_cfg.undoMaxStack.load());
    lua_setfield(L, -2, "undoMax");

    lua_pushstring(L, "0.35");
    lua_setfield(L, -2, "version");

    std::string coKey = KeyToString(g_cfg.copyObjKey, g_cfg.copyObjShift, false);
    lua_pushstring(L, coKey.c_str());
    lua_setfield(L, -2, "keyCopyObject");

    std::string caKey = KeyToString(g_cfg.copyAngleKey, false, g_cfg.copyAngleAlt);
    lua_pushstring(L, caKey.c_str());
    lua_setfield(L, -2, "keyCopyAngle");

    std::string undKey = KeyToString(g_cfg.undoKey, g_cfg.undoShift, false);
    lua_pushstring(L, undKey.c_str());
    lua_setfield(L, -2, "keyUndo");

    return 1;
}

// BBT_GetConfig(key) → value (bool/int/string)
static int lua_BBT_GetConfig(lua_State* L)
{
    const char* key = luaL_checkstring(L, 1);
    if      (!strcmp(key, "copyobject_enabled"))      lua_pushboolean(L, g_cfg.copyObjEnabled.load());
    else if (!strcmp(key, "copyangle_enabled"))        lua_pushboolean(L, g_cfg.copyAngleEnabled.load());
    else if (!strcmp(key, "undo_enabled"))             lua_pushboolean(L, g_cfg.undoEnabled.load());
    else if (!strcmp(key, "undo_max_stack"))           lua_pushinteger(L, g_cfg.undoMaxStack.load());
    else if (!strcmp(key, "freebuild_no_cost"))        lua_pushboolean(L, g_cfg.freeNoCost.load());
    else if (!strcmp(key, "freebuild_build_anywhere")) lua_pushboolean(L, g_cfg.placementNoBonfire.load()); // legacy key
    else if (!strcmp(key, "freebuild_no_stability"))   lua_pushboolean(L, g_cfg.freeNoStability.load());
    else if (!strcmp(key, "rotation_1deg_enabled"))    lua_pushboolean(L, g_cfg.rotation1Enabled.load());
    else if (!strcmp(key, "rotation_5deg_enabled"))    lua_pushboolean(L, g_cfg.rotation5Enabled.load());
    else if (!strcmp(key, "rotation_10deg_enabled"))   lua_pushboolean(L, g_cfg.rotation10Enabled.load());
    else if (!strcmp(key, "bstat_enabled"))            lua_pushboolean(L, g_cfg.bstatEnabled.load());
    else if (!strcmp(key, "placement_allow_under_roof")) lua_pushboolean(L, g_cfg.placementAllowUnderRoof.load());
    else if (!strcmp(key, "placement_no_roof_required")) lua_pushboolean(L, g_cfg.placementNoRoofRequired.load());
    else if (!strcmp(key, "placement_no_bonfire"))       lua_pushboolean(L, g_cfg.placementNoBonfire.load());
    else if (!strcmp(key, "ui_menu_scale"))             lua_pushinteger(L, g_cfg.uiMenuScale.load());
    else if (!strcmp(key, "ui_bstat_scale"))            lua_pushinteger(L, g_cfg.uiBStatScale.load());
    else lua_pushnil(L);
    return 1;
}

// BBT_SetConfig(key, value)
static int lua_BBT_SetConfig(lua_State* L)
{
    const char* key = luaL_checkstring(L, 1);
    if      (!strcmp(key, "copyobject_enabled"))      g_cfg.copyObjEnabled    = lua_toboolean(L, 2) != 0;
    else if (!strcmp(key, "copyangle_enabled"))        g_cfg.copyAngleEnabled  = lua_toboolean(L, 2) != 0;
    else if (!strcmp(key, "undo_enabled"))             g_cfg.undoEnabled       = lua_toboolean(L, 2) != 0;
    else if (!strcmp(key, "undo_max_stack"))         { int n = static_cast<int>(lua_tointeger(L, 2)); if (n > 0 && n <= 100) g_cfg.undoMaxStack = n; }
    else if (!strcmp(key, "freebuild_no_cost"))        g_cfg.freeNoCost        = lua_toboolean(L, 2) != 0;
    else if (!strcmp(key, "freebuild_build_anywhere")) g_cfg.placementNoBonfire = lua_toboolean(L, 2) != 0; // legacy key
    else if (!strcmp(key, "freebuild_no_stability"))   g_cfg.freeNoStability   = lua_toboolean(L, 2) != 0;
    else if (!strcmp(key, "rotation_1deg_enabled"))    g_cfg.rotation1Enabled  = lua_toboolean(L, 2) != 0;
    else if (!strcmp(key, "rotation_5deg_enabled"))    g_cfg.rotation5Enabled  = lua_toboolean(L, 2) != 0;
    else if (!strcmp(key, "rotation_10deg_enabled"))   g_cfg.rotation10Enabled = lua_toboolean(L, 2) != 0;
    else if (!strcmp(key, "bstat_enabled"))            g_cfg.bstatEnabled      = lua_toboolean(L, 2) != 0;
    else if (!strcmp(key, "placement_allow_under_roof")) g_cfg.placementAllowUnderRoof = lua_toboolean(L, 2) != 0;
    else if (!strcmp(key, "placement_no_roof_required")) g_cfg.placementNoRoofRequired = lua_toboolean(L, 2) != 0;
    else if (!strcmp(key, "placement_no_bonfire"))       g_cfg.placementNoBonfire      = lua_toboolean(L, 2) != 0;
    else if (!strcmp(key, "ui_menu_scale"))  { int n = static_cast<int>(lua_tointeger(L, 2)); if (n >= 70 && n <= 150) g_cfg.uiMenuScale  = n; }
    else if (!strcmp(key, "ui_bstat_scale")) { int n = static_cast<int>(lua_tointeger(L, 2)); if (n >= 70 && n <= 150) g_cfg.uiBStatScale = n; }
    return 0;
}

static int lua_BBT_SaveConfig(lua_State* /*L*/)
{
    BBT_SaveConfig();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────────────
// Lua bridge — undo stack
// ──────────────────────────────────────────────────────────────────────────────
static int lua_BuildingUndo_Push(lua_State* L)
{
    lua_Integer addr = lua_tointeger(L, 1);
    UObject* block   = reinterpret_cast<UObject*>(static_cast<uintptr_t>(addr));
    if (block && !block->IsUnreachable())
    {
        std::lock_guard<std::mutex> lock(g_StackMutex);
        for (auto it = g_UndoStack.begin(); it != g_UndoStack.end(); )
            it = (*it == block) ? g_UndoStack.erase(it) : ++it;
        g_UndoStack.push_back(block);
        if (g_UndoStack.size() > static_cast<size_t>(g_cfg.undoMaxStack)) g_UndoStack.pop_front();
        Output::send<LogLevel::Verbose>(
            STR("[BBT] +block stack={}\n"), g_UndoStack.size());

        // A block was placed → we're in build mode. Cache the construct ability
        // (one-time, no per-frame scan) and mark build active so CheckBuildMode
        // doesn't treat this as a fresh "entry" and wipe what we just tracked.
        if (!IsObjectValid(g_cachedConstruct)) { GetConstructAbility(); g_inBuildPrev = true; }
    }
    return 0;
}

// Called from Lua when build mode (re)opens — the game resets its own undo
// history on build-mode exit, so we clear ours to stay in sync (no phantom queue).
static int lua_BuildingUndo_Clear(lua_State* /*L*/)
{
    std::lock_guard<std::mutex> lock(g_StackMutex);
    if (!g_UndoStack.empty())
    {
        g_UndoStack.clear();
        Output::send<LogLevel::Verbose>(STR("[BBT] Undo stack reset (build mode opened)\n"));
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────────────
// Mod class
// ──────────────────────────────────────────────────────────────────────────────
class BuildingUndoMod : public CppUserModBase
{
public:
    BuildingUndoMod() : CppUserModBase()
    {
        ModName        = STR("BetterBuildingTools");
        ModVersion     = STR("0.35");
        ModDescription = STR("BBT — copy object, copy angle, undo, free-build, placement freedom, fine rotation, BStat HUD, in-game settings");
        ModAuthors     = STR("2BIT");

        register_tab(STR("BetterBuildingTools"), [](CppUserModBase* /*instance*/) {
            UE4SS_ENABLE_IMGUI();

            ImGui::TextUnformatted("BetterBuildingTools v0.35");
            ImGui::Separator();

            bool copyObj = g_cfg.copyObjEnabled;
            if (ImGui::Checkbox("Copy Object", &copyObj)) g_cfg.copyObjEnabled = copyObj;
            ImGui::SameLine();
            ImGui::TextDisabled("(Shift+C)");

            bool copyAngle = g_cfg.copyAngleEnabled;
            if (ImGui::Checkbox("Copy angle", &copyAngle)) g_cfg.copyAngleEnabled = copyAngle;
            ImGui::SameLine();
            ImGui::TextDisabled("(Alt+C)");

            bool undo = g_cfg.undoEnabled;
            if (ImGui::Checkbox("Undo", &undo)) g_cfg.undoEnabled = undo;
            ImGui::SameLine();
            ImGui::TextDisabled("(Shift+Z)");

            int maxStack = g_cfg.undoMaxStack;
            if (ImGui::SliderInt("Undo max stack", &maxStack, 1, 50)) g_cfg.undoMaxStack = maxStack;

            {
                std::lock_guard<std::mutex> lock(g_StackMutex);
                ImGui::Text("Undo stack: %d / %d", static_cast<int>(g_UndoStack.size()), g_cfg.undoMaxStack.load());
                ImGui::SameLine();
                if (ImGui::Button("Clear")) g_UndoStack.clear();
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.1f, 1.0f), "Cheats");

            bool noCost = g_cfg.freeNoCost;
            if (ImGui::Checkbox("No build cost", &noCost)) g_cfg.freeNoCost = noCost;

            bool noStability = g_cfg.freeNoStability;
            if (ImGui::Checkbox("No stability/support requirement", &noStability)) g_cfg.freeNoStability = noStability;
            ImGui::SameLine();
            ImGui::TextDisabled("(fully reversible)");

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Placement Freedom");

            bool underRoof = g_cfg.placementAllowUnderRoof;
            if (ImGui::Checkbox("Allow Under Roof (furnace/kiln)", &underRoof)) g_cfg.placementAllowUnderRoof = underRoof;

            bool noRoof = g_cfg.placementNoRoofRequired;
            if (ImGui::Checkbox("No Roof Required (stations/beds)", &noRoof)) g_cfg.placementNoRoofRequired = noRoof;

            bool noBonfire = g_cfg.placementNoBonfire;
            if (ImGui::Checkbox("No Bonfire Required", &noBonfire)) g_cfg.placementNoBonfire = noBonfire;

            ImGui::Separator();
            ImGui::TextUnformatted("Fine rotation cycle");
            ImGui::SameLine();
            ImGui::TextDisabled("(your Rotation Step key)");

            bool r1 = g_cfg.rotation1Enabled;
            if (ImGui::Checkbox("1 deg", &r1)) g_cfg.rotation1Enabled = r1;
            ImGui::SameLine();
            bool r5 = g_cfg.rotation5Enabled;
            if (ImGui::Checkbox("5 deg", &r5)) g_cfg.rotation5Enabled = r5;
            ImGui::SameLine();
            bool r10 = g_cfg.rotation10Enabled;
            if (ImGui::Checkbox("10 deg", &r10)) g_cfg.rotation10Enabled = r10;
            ImGui::TextWrapped(
                "Disable 1 deg if it snaps to a neighbor's rotation when you don't "
                "want it to (the game's own snap tolerance is ~3 degrees).");

            if (g_vanillaRotationStepsCached)
            {
                auto cycle = BuildRotationCycle();
                std::string list;
                for (int32_t v : cycle) { if (!list.empty()) list += ", "; list += std::to_string(v); }
                ImGui::Text("Cycle: %s", list.c_str());
            }
            else
            {
                ImGui::TextDisabled("Cycle: (cached on first use in a build session)");
            }
        });
    }
    Hook::GlobalCallbackId m_preTickId  = Hook::ERROR_ID;
    Hook::GlobalCallbackId m_postTickId = Hook::ERROR_ID;

    ~BuildingUndoMod() override
    {
        Output::send<LogLevel::Verbose>(STR("[BBT] Destructor: unregistering tick callbacks (pre={:#X}, post={:#X})\n"),
            static_cast<uint64_t>(m_preTickId), static_cast<uint64_t>(m_postTickId));

        if (m_preTickId != Hook::ERROR_ID)
        {
            bool ok = Hook::UnregisterCallback(m_preTickId);
            Output::send<LogLevel::Verbose>(STR("[BBT] Destructor: pre-tick unregister {}\n"), ok ? STR("OK") : STR("FAILED"));
        }
        if (m_postTickId != Hook::ERROR_ID)
        {
            bool ok = Hook::UnregisterCallback(m_postTickId);
            Output::send<LogLevel::Verbose>(STR("[BBT] Destructor: post-tick unregister {}\n"), ok ? STR("OK") : STR("FAILED"));
        }

        g_cachedConstruct  = nullptr;
        g_cachedASC        = nullptr;
        g_cachedASCOwner   = nullptr;
        g_cachedSendFn     = nullptr;
        g_inBuildPrev      = false;
        g_copyAngleHold.store(false);
        g_reqUndo.store(false);
        g_reqCopyObj.store(false);
        g_reqMatchAngle.store(false);
        g_undoInFlight.store(false);
        g_lookAtData.valid = false;
        {
            std::lock_guard<std::mutex> lock(g_StackMutex);
            g_UndoStack.clear();
        }

        Output::send<LogLevel::Verbose>(STR("[BBT] Cleanup complete — safe to unload\n"));
    }

    auto on_unreal_init() -> void override
    {
        BBT_LoadConfig();

        Output::send<LogLevel::Verbose>(STR("[BBT] on_unreal_init: config loaded, registering keydown events...\n"));

        try {
            {
                Handler::ModifierKeyArray mods{};
                if (g_cfg.copyObjShift) mods[0] = ModifierKey::SHIFT;
                register_keydown_event(g_cfg.copyObjKey, mods, []() { g_reqCopyObj.store(true); });
            }
            Output::send<LogLevel::Verbose>(STR("[BBT] on_unreal_init: copy object key registered\n"));
            {
                Handler::ModifierKeyArray mods{};
                if (g_cfg.copyAngleAlt) mods[0] = ModifierKey::ALT;
                register_keydown_event(g_cfg.copyAngleKey, mods, []() { g_reqMatchAngle.store(true); });
            }
            Output::send<LogLevel::Verbose>(STR("[BBT] on_unreal_init: copyangle key registered\n"));
            {
                Handler::ModifierKeyArray mods{};
                if (g_cfg.undoShift) mods[0] = ModifierKey::SHIFT;
                register_keydown_event(g_cfg.undoKey, mods, []() { g_reqUndo.store(true); });
            }
            Output::send<LogLevel::Verbose>(STR("[BBT] on_unreal_init: undo key registered\n"));
        } catch (...) {
            Output::send<LogLevel::Error>(STR("[BBT] CRASH in keydown registration — continuing without keys\n"));
        }

        try {
            Hook::FCallbackOptions preOpts{};
            m_preTickId = Hook::RegisterEngineTickPreCallback(
                [](auto& /*info*/, UEngine* /*Engine*/, float /*Dt*/, bool /*bIdle*/) {
                    CheckBuildMode();
                    if (!g_cachedConstruct) return;
                    EnsureRotationHook();
                    if (!g_inBuildPrev) return;
                    SyncFreeBuild();
                    SyncStability();
                    SyncPlacementFreedom();
                    if (g_reqCopyObj.exchange(false)    && g_cfg.copyObjEnabled)    TryCopyObject();
                    if (g_reqMatchAngle.exchange(false) && g_cfg.copyAngleEnabled) TryMatchAngle();
                    if (g_reqUndo.exchange(false)        && g_cfg.undoEnabled)      TryUndo();
                }, preOpts);
            Output::send<LogLevel::Verbose>(STR("[BBT] on_unreal_init: pre-tick registered (id={:#X})\n"), static_cast<uint64_t>(m_preTickId));
        } catch (...) {
            Output::send<LogLevel::Error>(STR("[BBT] CRASH in pre-tick registration\n"));
        }

        try {
            Hook::FCallbackOptions postOpts{};
            m_postTickId = Hook::RegisterEngineTickPostCallback(
                [](auto& /*info*/, UEngine* /*Engine*/, float /*Dt*/, bool /*bIdle*/) {
                    if (g_cfg.copyAngleEnabled) SyncMatchAngle();
                    UpdateLookAtData();
                }, postOpts);
            Output::send<LogLevel::Verbose>(STR("[BBT] on_unreal_init: post-tick registered (id={:#X})\n"), static_cast<uint64_t>(m_postTickId));
        } catch (...) {
            Output::send<LogLevel::Error>(STR("[BBT] CRASH in post-tick registration\n"));
        }

        Output::send<LogLevel::Verbose>(
            STR("[BBT] game-thread dispatcher active — ready v0.35 (undo cooldown + safe unload + keybind bridge)\n"));
    }

    auto on_lua_start(LuaMadeSimple::Lua& lua,
                      LuaMadeSimple::Lua& /*main_lua*/,
                      LuaMadeSimple::Lua& /*async_lua*/,
                      LuaMadeSimple::Lua* /*hook_lua*/) -> void override
    {
        lua_State* L = lua.get_lua_state();
        lua_pushcfunction(L, lua_BuildingUndo_Push);
        lua_setglobal(L, "BuildingUndo_Push");
        lua_pushcfunction(L, lua_BuildingUndo_Clear);
        lua_setglobal(L, "BuildingUndo_Clear");
        lua_pushcfunction(L, lua_BBT_GetBuildStatus);
        lua_setglobal(L, "BBT_GetBuildStatus");
        lua_pushcfunction(L, lua_BBT_GetConfig);
        lua_setglobal(L, "BBT_GetConfig");
        lua_pushcfunction(L, lua_BBT_SetConfig);
        lua_setglobal(L, "BBT_SetConfig");
        lua_pushcfunction(L, lua_BBT_SaveConfig);
        lua_setglobal(L, "BBT_SaveConfig");
        Output::send<LogLevel::Verbose>(STR("[BBT] Lua globals exposed: BuildingUndo_Push, BuildingUndo_Clear, BBT_GetBuildStatus, BBT_GetConfig, BBT_SetConfig, BBT_SaveConfig\n"));
    }
};

extern "C"
{
    __declspec(dllexport) CppUserModBase* start_mod()             { return new BuildingUndoMod(); }
    __declspec(dllexport) void uninstall_mod(CppUserModBase* mod) { delete mod; }
}
