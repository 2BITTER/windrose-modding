-- BetterBuildingTools (BBT) — Lua entry point
local UEHelpers = require("UEHelpers")
package.path = '.\\Mods\\BetterBuildingTools\\Scripts\\?.lua;' .. package.path

local H = require("ui_helpers")
local createBStat = require("ui_bstat")
local createSettings = require("ui_settings")
local TextSigns = require("ui_textsigns")

local function log(msg) print("[BBT] " .. tostring(msg)) end

-- Create UI modules
local BStat = createBStat(H)
local Settings = createSettings(H)

-- Wire cross-references
BStat.SetSettingsRef(Settings)
Settings.SetBStatRef(BStat)

-- ─────────────────────────────────────────────────────────────────────────────
-- Toast notification (brief on-screen confirmation for discrete actions)
-- ─────────────────────────────────────────────────────────────────────────────
local ToastRoot = nil
local ToastText = nil
local toastTicks = 0

local function BuildToast()
    if ToastRoot and ToastRoot:IsValid() then return end
    local gi = UEHelpers.GetGameInstance()
    if not gi or not gi:IsValid() then return end
    ToastRoot = H.CreateWidget("UserWidget", gi, "BBTToastRoot")
    ToastRoot.WidgetTree = H.CreateWidget("WidgetTree", ToastRoot, "BBTToastTree")
    local canvas = H.CreateWidget("CanvasPanel", ToastRoot.WidgetTree, "BBTToastCanvas")
    ToastRoot.WidgetTree.RootWidget = canvas
    local border = H.CreateWidget("Border", canvas, "BBTToastBorder")
    border:SetBrushColor(H.COLOR_BG)
    border:SetPadding({ Left = 16, Top = 10, Right = 16, Bottom = 10 })
    ToastText = H.CreateWidget("TextBlock", border, "BBTToastText")
    ToastText.Font.Size = 13
    ToastText:SetText(FText(""))
    ToastText:SetColorAndOpacity(H.COLOR_TEAL)
    border:SetContent(ToastText)
    local slot = canvas:AddChildToCanvas(border)
    slot:SetAutoSize(true)
    slot:SetAnchors({ Minimum = { X = 0.5, Y = 0 }, Maximum = { X = 0.5, Y = 0 } })
    slot:SetAlignment({ X = 0.5, Y = 0 })
    slot:SetPosition({ X = 0, Y = 80 })
end

local function ShowToast(msg)
    if not ToastRoot or not ToastRoot:IsValid() then BuildToast() end
    if not ToastRoot or not ToastRoot:IsValid() then return end
    if ToastText and ToastText:IsValid() then ToastText:SetText(FText(msg)) end
    ToastRoot:AddToViewport(1000)
    toastTicks = 20
end

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
    { 0x0D, function() ExecuteInGameThread(function() TextSigns.Submit() end) end },       -- Enter
    { 0x1B, function() ExecuteInGameThread(function()                                      -- Escape
        if TextSigns.Visible then TextSigns.Cancel() end
    end) end },
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
    if not TextSigns.BGRoot or not TextSigns.BGRoot:IsValid() then
        TextSigns.BuildBG()
    end
    if BBT_GetBuildStatus then
        local ts = BBT_GetBuildStatus()
        if ts.copyAngleFired then ShowToast("Angle copied") end
        if ts.copyObjFired   then ShowToast("Object copied") end
    end
    BStat.Update()
    if Settings.Open then
        if Settings.Root and Settings.Root:IsValid() then
            Settings.Refresh()
        else
            Settings.Open = false
        end
    end
    TextSigns.Update()

    if toastTicks > 0 then
        toastTicks = toastTicks - 1
        if toastTicks == 0 and ToastRoot and ToastRoot:IsValid() then
            ToastRoot:RemoveFromViewport()
        end
    end
end

LoopInGameThreadWithDelay(150, OnTick)

log("Loaded - BStat HUD + drill-down settings + guide + undo tracking active (F2 = settings)")
