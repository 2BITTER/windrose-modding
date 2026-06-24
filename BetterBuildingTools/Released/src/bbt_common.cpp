#include "bbt_common.h"

// ──────────────────────────────────────────────────────────────────────────────
// Global variable definitions (declared extern in bbt_common.h)
// ──────────────────────────────────────────────────────────────────────────────

// Config (read from config.txt next to the mod, at load)
// Toggle fields are std::atomic: the ImGui tab callback runs on UE4SS's render
// thread while the tick dispatcher reads/applies them on the game thread.
BBTConfig g_cfg;

// Keydown callbacks run on the UE4SS input thread, NOT the game thread.
// Touching game objects / the BL async queue off-thread corrupts state
// ("worker thread stopped unexpectedly"). So keys only set a request flag;
// the actual work runs in the engine-tick callback (game thread).
std::atomic<bool> g_reqUndo{false};
std::atomic<bool> g_reqCopyObj{false};
std::atomic<bool> g_reqMatchAngle{false};

// Match-angle hold state — set on Alt+C, cleared when player manually rotates.
BBTQuat           g_copyAngleTarget{0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0};
std::atomic<bool> g_copyAngleHold{false};

// Cached lookups — these scan the whole UObject table, so we do them once and
// reuse. Re-find only if the cached pointer goes stale (world reload, etc.).
UObject* g_cachedPC        = nullptr;
UObject* g_cachedConstruct = nullptr;

// Build-mode detection (game thread).
bool g_inBuildPrev = false;
std::atomic<bool> g_inBuildVisual{false};

// Undo stack
std::deque<UObject*> g_UndoStack;
std::mutex           g_StackMutex;
// Max undo depth is configurable via g_cfg.undoMaxStack.

// Looked-at block rotation cache (throttled trace from PostTick)
LookAtData g_lookAtData;

// ──────────────────────────────────────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────────────────────────────────────
bool IsObjectValid(UObject* obj)
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

// Cached UClass for AR5BuildingBlock (IsA covers subclasses: doors, torches, etc.)
static UClass* g_blockClass = nullptr;
UClass* GetBlockClass()
{
    if (!g_blockClass)
        g_blockClass = reinterpret_cast<UClass*>(UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/R5.R5BuildingBlock")));
    return g_blockClass;
}

UObject* GetPlayerController()
{
    if (!IsObjectValid(g_cachedPC))
    {
        g_cachedPC = UObjectGlobals::FindFirstOf(STR("R5PlayerController"));
        if (!IsObjectValid(g_cachedPC))
            g_cachedPC = UObjectGlobals::FindFirstOf(STR("PlayerController"));
    }
    return g_cachedPC;
}

UObject* GetConstructAbility()
{
    if (!IsObjectValid(g_cachedConstruct))
        g_cachedConstruct = UObjectGlobals::FindFirstOf(STR("R5Ability_Building_MakeConstructCommand"));
    return g_cachedConstruct;
}

UObject* GetCurrentBrushItem()
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

// Folder containing this mod (…\Mods\BetterBuildingTools), derived from our DLL
// path so it's portable on any user's machine (no hardcoded paths).
std::wstring BBT_ModFolder()
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

char BBT_KeyChar(Key k)
{
    char c = static_cast<char>(k);
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
    return '?';
}

std::string KeyToString(Key k, bool shift, bool alt)
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

// ──────────────────────────────────────────────────────────────────────────────
// Config I/O helpers (file-local)
// ──────────────────────────────────────────────────────────────────────────────
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
        "# no_bell_shore: bells can be placed anywhere (not just near shoreline).\n"
        "placement_allow_under_roof = false\n"
        "placement_no_roof_required = false\n"
        "placement_no_bonfire       = false\n"
        "placement_no_bell_shore    = false\n";
}

void BBT_LoadConfig()
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
        else if (k == "placement_no_bell_shore")      g_cfg.placementNoBellShore    = BBT_ParseBool(v);
        else if (k == "ui_menu_scale")  { int n = std::atoi(v.c_str()); if (n >= 70 && n <= 150) g_cfg.uiMenuScale  = n; }
        else if (k == "ui_bstat_scale") { int n = std::atoi(v.c_str()); if (n >= 70 && n <= 150) g_cfg.uiBStatScale = n; }
    }
    Output::send<LogLevel::Verbose>(STR("[BBT] Config loaded (copyobj={}, undo={}, maxStack={})\n"),
        g_cfg.copyObjEnabled.load(), g_cfg.undoEnabled.load(), g_cfg.undoMaxStack.load());
}

void BBT_SaveConfig()
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
        "placement_no_bell_shore    = " << b(g_cfg.placementNoBellShore) << "\n"
        "\n"
        "ui_menu_scale  = " << g_cfg.uiMenuScale.load() << "\n"
        "ui_bstat_scale = " << g_cfg.uiBStatScale.load() << "\n";
    Output::send<LogLevel::Verbose>(STR("[BBT] Config saved\n"));
}
