#include "bbt_common.h"

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

int lua_BBT_GetBuildStatus(lua_State* L)
{
    lua_newtable(L);

    lua_pushboolean(L, g_inBuildPrev);
    lua_setfield(L, -2, "inBuildMode");

    lua_pushboolean(L, g_inBuildVisual.load());
    lua_setfield(L, -2, "inBuildModeVisual");

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

    lua_pushstring(L, "0.36");
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
int lua_BBT_GetConfig(lua_State* L)
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
    else if (!strcmp(key, "placement_no_bell_shore"))   lua_pushboolean(L, g_cfg.placementNoBellShore.load());
    else if (!strcmp(key, "ui_menu_scale"))             lua_pushinteger(L, g_cfg.uiMenuScale.load());
    else if (!strcmp(key, "ui_bstat_scale"))            lua_pushinteger(L, g_cfg.uiBStatScale.load());
    else lua_pushnil(L);
    return 1;
}

// BBT_SetConfig(key, value)
int lua_BBT_SetConfig(lua_State* L)
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
    else if (!strcmp(key, "placement_no_bell_shore"))   g_cfg.placementNoBellShore    = lua_toboolean(L, 2) != 0;
    else if (!strcmp(key, "ui_menu_scale"))  { int n = static_cast<int>(lua_tointeger(L, 2)); if (n >= 70 && n <= 150) g_cfg.uiMenuScale  = n; }
    else if (!strcmp(key, "ui_bstat_scale")) { int n = static_cast<int>(lua_tointeger(L, 2)); if (n >= 70 && n <= 150) g_cfg.uiBStatScale = n; }
    return 0;
}

int lua_BBT_SaveConfig(lua_State* /*L*/)
{
    BBT_SaveConfig();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────────────
// Lua bridge — undo stack
// ──────────────────────────────────────────────────────────────────────────────
int lua_BuildingUndo_Push(lua_State* L)
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
int lua_BuildingUndo_Clear(lua_State* /*L*/)
{
    std::lock_guard<std::mutex> lock(g_StackMutex);
    if (!g_UndoStack.empty())
    {
        g_UndoStack.clear();
        Output::send<LogLevel::Verbose>(STR("[BBT] Undo stack reset (build mode opened)\n"));
    }
    return 0;
}
