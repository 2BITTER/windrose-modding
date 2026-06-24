-- BetterBuildingTools (BBT) — Lua entry point
local UEHelpers = require("UEHelpers")
package.path = '.\\Mods\\BetterBuildingTools\\Scripts\\?.lua;' .. package.path

local H = require("ui_helpers")
local createBStat = require("ui_bstat")
local createSettings = require("ui_settings")

local function log(msg) print("[BBT] " .. tostring(msg)) end

-- Create UI modules
local BStat = createBStat(H)
local Settings = createSettings(H)

-- Wire cross-references
BStat.SetSettingsRef(Settings)
Settings.SetBStatRef(BStat)

-- ─────────────────────────────────────────────────────────────────────────────
-- Undo block tracking (deferred — UFunction is JIT-loaded)
-- ─────────────────────────────────────────────────────────────────────────────
local undoHookRegistered = false
local function TryRegisterUndoHook()
    if undoHookRegistered then return end
    local ok, _ = pcall(function()
        RegisterHook("/Script/R5.R5Ability_Building_MakeConstructCommand:OnBuildingAddedToIsland",
            function(self, BuildingBlock)
                local block = BuildingBlock:get()
                if not block or not block:IsValid() then return end
                if BuildingUndo_Push then
                    BuildingUndo_Push(block:GetAddress())
                end
            end
        )
    end)
    if ok then
        undoHookRegistered = true
        log("Undo hook registered")
    end
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Keybinds (VK codes)
-- ─────────────────────────────────────────────────────────────────────────────
local Binds = {
    { 0x71, function() ExecuteInGameThread(function() Settings.HandleF2() end) end },      -- F2
    { 0x26, function() ExecuteInGameThread(function() Settings.HandleUp() end) end },      -- Up
    { 0x28, function() ExecuteInGameThread(function() Settings.HandleDown() end) end },    -- Down
    { 0x25, function() ExecuteInGameThread(function() Settings.HandleLeft() end) end },    -- Left
    { 0x27, function() ExecuteInGameThread(function() Settings.HandleRight() end) end },   -- Right
    { 0x21, function() ExecuteInGameThread(function() Settings.HandlePgUp() end) end },    -- PgUp
    { 0x22, function() ExecuteInGameThread(function() Settings.HandlePgDn() end) end },    -- PgDn
}

for _, bind in ipairs(Binds) do
    RegisterKeyBind(bind[1], bind[2])
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Main update loop
-- ─────────────────────────────────────────────────────────────────────────────
local function OnTick()
    TryRegisterUndoHook()
    if not BStat.Root or not BStat.Root:IsValid() then
        BStat.Build()
    end
    BStat.Update()
    if Settings.Open then
        if Settings.Root and Settings.Root:IsValid() then
            Settings.Refresh()
        else
            Settings.Open = false
        end
    end
end

LoopInGameThreadWithDelay(150, OnTick)

log("Loaded - BStat HUD + drill-down settings + guide + undo tracking active (F2 = settings)")
