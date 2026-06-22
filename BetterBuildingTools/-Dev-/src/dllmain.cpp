#include "bbt_common.h"

// Build-mode detection (game thread). The construct ability's StrategyTask
// (UR5AbilityTask_FindConstructTarget* @ 0x03D8) exists only while build mode is
// active. When it goes null→non-null we (re)entered build mode → clear the stale
// undo stack, because the game resets its own undo history on build-mode exit.
// Only uses the CACHED construct ptr (no per-frame object-table scan).
static void CheckBuildMode()
{
    if (!IsObjectValid(g_cachedConstruct)) {
        if (g_cachedConstruct) {
            g_cachedConstruct = nullptr;
            g_cachedPC = nullptr;
            FreeBuildCleanup();
            StabilityCleanup();
            PlacementCleanup();
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

            if (BBT_AnyFineStepEnabled())
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
        g_cachedPC         = nullptr;
        g_inBuildPrev      = false;
        g_copyAngleHold.store(false);
        g_reqUndo.store(false);
        g_reqCopyObj.store(false);
        g_reqMatchAngle.store(false);
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
