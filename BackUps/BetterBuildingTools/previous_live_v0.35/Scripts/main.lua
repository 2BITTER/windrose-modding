-- BetterBuildingTools (BBT) — Lua side
-- BStat HUD + settings menu + guide + undo tracking
local UEHelpers = require("UEHelpers")

local function log(msg) print("[BBT] " .. tostring(msg)) end

-- ─────────────────────────────────────────────────────────────────────────────
-- Undo block tracking
-- ─────────────────────────────────────────────────────────────────────────────
RegisterHook("/Script/R5.R5Ability_Building_MakeConstructCommand:OnBuildingAddedToIsland",
    function(self, BuildingBlock)
        local block = BuildingBlock:get()
        if not block or not block:IsValid() then return end
        if BuildingUndo_Push then
            BuildingUndo_Push(block:GetAddress())
        end
    end
)

-- ─────────────────────────────────────────────────────────────────────────────
-- UMG widget primitives
-- ─────────────────────────────────────────────────────────────────────────────
local function LinearColor(r, g, b, a)
    return { R = r, G = g, B = b, A = a }
end

local function SlateColor(r, g, b, a)
    return { SpecifiedColor = LinearColor(r, g, b, a), ColorUseRule = 0 }
end

local function CreateWidget(className, owner, name)
    return StaticConstructObject(StaticFindObject("/Script/UMG." .. className), owner, FName(name))
end

local function MakeTextBlock(parent, name, size, text, color, padding)
    local block     = CreateWidget("TextBlock", parent, name)
    block.Font.Size = size
    block:SetText(FText(text))
    block:SetColorAndOpacity(color)
    if parent.AddChildToVerticalBox then
        parent:AddChildToVerticalBox(block):SetPadding(padding or { Left = 0, Top = 0, Right = 0, Bottom = 0 })
    end
    return block
end

local function MakeDivider(parent, name)
    local box = CreateWidget("SizeBox", parent, name .. "_Box")
    box:SetHeightOverride(1.0)
    local line = CreateWidget("Border", box, name .. "_Line")
    line:SetBrushColor(LinearColor(1.0, 1.0, 1.0, 0.12))
    box:SetContent(line)
    parent:AddChildToVerticalBox(box):SetPadding({ Left = 0, Top = 6, Right = 0, Bottom = 6 })
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Shared color palette (all alphas 1.0 except backgrounds)
-- ─────────────────────────────────────────────────────────────────────────────
local COLOR_TEAL      = SlateColor(0.0, 1.0, 0.7, 1.0)
local COLOR_ORANGE    = SlateColor(1.0, 0.55, 0.0, 1.0)
local COLOR_GREEN     = SlateColor(0.1, 1.0, 0.45, 1.0)
local COLOR_WHITE     = SlateColor(0.9, 0.9, 0.9, 1.0)
local COLOR_DIM       = SlateColor(0.35, 0.35, 0.35, 1.0)
local COLOR_PIPE      = SlateColor(0.45, 0.45, 0.45, 1.0)
local COLOR_HINT      = SlateColor(0.5, 0.5, 0.5, 1.0)
local COLOR_BG        = LinearColor(0.02, 0.02, 0.03, 0.82)
local COLOR_SELECTED  = SlateColor(0.0, 1.0, 1.0, 1.0)
local COLOR_VALUE_ON  = SlateColor(0.1, 1.0, 0.45, 1.0)
local COLOR_VALUE_OFF = SlateColor(0.35, 0.35, 0.35, 1.0)
local COLOR_VALUE_NUM = SlateColor(0.9, 0.9, 0.5, 1.0)

-- ─────────────────────────────────────────────────────────────────────────────
-- UI construction helpers
-- ─────────────────────────────────────────────────────────────────────────────
local function MakeLabeledRow(parent, name, labelText, valueText, opts)
    opts = opts or {}
    local row = CreateWidget("HorizontalBox", parent, name .. "_Row")
    local labelBox = CreateWidget("SizeBox", row, name .. "_LB")
    labelBox:SetWidthOverride(opts.labelWidth or 100)
    local label = CreateWidget("TextBlock", labelBox, name .. "_L")
    label.Font.Size = opts.fontSize or 11
    label:SetText(FText(labelText))
    label:SetColorAndOpacity(opts.labelColor or COLOR_ORANGE)
    labelBox:SetContent(label)
    row:AddChildToHorizontalBox(labelBox)
    local value = CreateWidget("TextBlock", row, name .. "_V")
    value.Font.Size = opts.fontSize or 11
    value:SetText(FText(valueText or "---"))
    value:SetColorAndOpacity(opts.valueColor or COLOR_WHITE)
    row:AddChildToHorizontalBox(value)
    parent:AddChildToVerticalBox(row):SetPadding(opts.padding or { Left = 0, Top = 0, Right = 0, Bottom = 0 })
    return label, value
end

local function MakeHeaderRow(parent, prefix, parts)
    local row = CreateWidget("HorizontalBox", parent, prefix .. "HeaderRow")
    local texts = {}
    for i, part in ipairs(parts) do
        if i > 1 then
            local sep = CreateWidget("TextBlock", row, prefix .. "HSep" .. i)
            sep.Font.Size = 11
            sep:SetText(FText("  |  "))
            sep:SetColorAndOpacity(COLOR_PIPE)
            row:AddChildToHorizontalBox(sep)
        end
        local text = CreateWidget("TextBlock", row, prefix .. "HPart" .. i)
        text.Font.Size = part.size or 12
        text:SetText(FText(part.text))
        text:SetColorAndOpacity(part.color or COLOR_WHITE)
        row:AddChildToHorizontalBox(text)
        texts[i] = text
    end
    parent:AddChildToVerticalBox(row):SetPadding({ Left = 0, Top = 0, Right = 0, Bottom = 2 })
    return row, texts
end

local function MakeNavBar(parent, prefix, actions)
    local row = CreateWidget("HorizontalBox", parent, prefix .. "NavRow")
    for i, act in ipairs(actions) do
        if i > 1 then
            local sep = CreateWidget("TextBlock", row, prefix .. "NavSep" .. i)
            sep.Font.Size = 10
            sep:SetText(FText("  |  "))
            sep:SetColorAndOpacity(COLOR_PIPE)
            row:AddChildToHorizontalBox(sep)
        end
        local label = CreateWidget("TextBlock", row, prefix .. "NavL" .. i)
        label.Font.Size = 10
        label:SetText(FText(act.label .. ": "))
        label:SetColorAndOpacity(COLOR_ORANGE)
        row:AddChildToHorizontalBox(label)
        local keys = CreateWidget("TextBlock", row, prefix .. "NavK" .. i)
        keys.Font.Size = 10
        keys:SetText(FText(act.keys))
        keys:SetColorAndOpacity(COLOR_WHITE)
        row:AddChildToHorizontalBox(keys)
    end
    parent:AddChildToVerticalBox(row):SetPadding({ Left = 0, Top = 0, Right = 0, Bottom = 0 })
    return row
end

-- ─────────────────────────────────────────────────────────────────────────────
-- UI scale helpers
-- ─────────────────────────────────────────────────────────────────────────────
local function GetMenuScale()
    if BBT_GetConfig then return (BBT_GetConfig("ui_menu_scale") or 100) / 100 end
    return 1.0
end

local function GetBStatScale()
    if BBT_GetConfig then return (BBT_GetConfig("ui_bstat_scale") or 100) / 100 end
    return 1.0
end

local function MS(base)
    return math.max(1, math.floor(base * GetMenuScale()))
end

local function BS(base)
    return math.max(1, math.floor(base * GetBStatScale()))
end

-- ─────────────────────────────────────────────────────────────────────────────
-- BStat HUD — persistent overlay during build mode (left side)
-- ─────────────────────────────────────────────────────────────────────────────
local BStat = {
    Root       = nil,
    InViewport = false,
    ValueTexts = {},
}
local Settings -- forward-declare so UpdateBStat can see it

local function FormatDeg(deg)
    if not deg then return "---" end
    return string.format("%.1f", deg)
end

local function SnapLabel(mode)
    if not mode then return "---" end
    if mode == 0 then return "OFF"
    elseif mode == 2 then return "ON"
    else return tostring(mode)
    end
end

local function CleanItemName(raw)
    if not raw then return "---" end
    local name = raw
    name = name:gsub("^DA_BI_", "")
    name = name:gsub("_", " ")
    return name
end

local function UpdateBStat()
    if not BStat.ValueTexts or not BStat.ValueTexts.Yaw or not BStat.ValueTexts.Yaw:IsValid() then return end
    if not BBT_GetBuildStatus then return end

    local s = BBT_GetBuildStatus()
    if not s or not s.inBuildMode then
        if BStat.InViewport and BStat.Root and BStat.Root:IsValid() then
            BStat.Root:RemoveFromViewport()
            BStat.InViewport = false
        end
        return
    end

    if not s.bstatEnabled then
        if BStat.InViewport and BStat.Root and BStat.Root:IsValid() then
            BStat.Root:RemoveFromViewport()
            BStat.InViewport = false
        end
        return
    end

    if Settings and Settings.Open then
        if BStat.InViewport and BStat.Root and BStat.Root:IsValid() then
            BStat.Root:RemoveFromViewport()
            BStat.InViewport = false
        end
        return
    end

    if not BStat.InViewport and BStat.Root and BStat.Root:IsValid() then
        BStat.Root:AddToViewport(900)
        BStat.InViewport = true
    end

    local yawMatchesTarget = false
    if s.lookAtValid and s.rotationYaw and s.lookAtYaw then
        local diff = math.abs(s.rotationYaw - s.lookAtYaw)
        if diff > 180 then diff = 360 - diff end
        yawMatchesTarget = diff < 1.0
    end

    BStat.ValueTexts.Yaw:SetText(FText(FormatDeg(s.rotationYaw) .. "\194\176"))
    BStat.ValueTexts.Yaw:SetColorAndOpacity(yawMatchesTarget and COLOR_GREEN or COLOR_WHITE)

    BStat.ValueTexts.Step:SetText(FText(s.rotationStep and (tostring(s.rotationStep) .. "\194\176") or "---"))
    BStat.ValueTexts.Step:SetColorAndOpacity(COLOR_WHITE)

    local snapStr = SnapLabel(s.snappingMode)
    local snapColor = COLOR_WHITE
    if snapStr == "ON" then snapColor = COLOR_GREEN
    elseif snapStr == "OFF" then snapColor = COLOR_DIM end
    BStat.ValueTexts.Snap:SetText(FText(snapStr))
    BStat.ValueTexts.Snap:SetColorAndOpacity(snapColor)

    if s.lookAtValid then
        BStat.ValueTexts.Target:SetText(FText(FormatDeg(s.lookAtYaw) .. "\194\176"))
        BStat.ValueTexts.Target:SetColorAndOpacity(yawMatchesTarget and COLOR_GREEN or COLOR_WHITE)
    else
        BStat.ValueTexts.Target:SetText(FText("---"))
        BStat.ValueTexts.Target:SetColorAndOpacity(COLOR_DIM)
    end

    if s.copyAngleHeld then
        BStat.ValueTexts.Copied:SetText(FText(FormatDeg(s.copyAngleYaw) .. "\194\176"))
        BStat.ValueTexts.Copied:SetColorAndOpacity(COLOR_GREEN)
    else
        BStat.ValueTexts.Copied:SetText(FText("OFF"))
        BStat.ValueTexts.Copied:SetColorAndOpacity(COLOR_DIM)
    end

    BStat.ValueTexts.Undo:SetText(FText((s.undoCount or 0) .. "/" .. (s.undoMax or 0)))
    BStat.ValueTexts.Undo:SetColorAndOpacity(COLOR_WHITE)

    if s.lookAtValid and s.lookAtName then
        BStat.ValueTexts.LookAt:SetText(FText(CleanItemName(s.lookAtName)))
        BStat.ValueTexts.LookAt:SetColorAndOpacity(COLOR_WHITE)
    else
        BStat.ValueTexts.LookAt:SetText(FText("---"))
        BStat.ValueTexts.LookAt:SetColorAndOpacity(COLOR_DIM)
    end
end

local function BuildBStat()
    if BStat.Root and BStat.Root:IsValid() then return end

    local gi = UEHelpers.GetGameInstance()
    if not gi or not gi:IsValid() then return end

    local root          = CreateWidget("UserWidget", gi, "BBTBStatRoot")
    root.WidgetTree     = CreateWidget("WidgetTree", root, "BBTBStatTree")
    local canvas        = CreateWidget("CanvasPanel", root.WidgetTree, "BBTBStatCanvas")
    root.WidgetTree.RootWidget = canvas

    local border        = CreateWidget("Border", canvas, "BBTBStatBorder")
    border:SetBrushColor(COLOR_BG)
    border:SetPadding({ Left = BS(16), Top = BS(12), Right = BS(16), Bottom = BS(12) })

    local vbox          = CreateWidget("VerticalBox", border, "BBTBStatBox")
    border:SetContent(vbox)

    local ver = "?"
    if BBT_GetBuildStatus then
        local s = BBT_GetBuildStatus()
        if s and s.version then ver = s.version end
    end
    MakeHeaderRow(vbox, "BStat", {
        { text = "BBT",      size = BS(12), color = COLOR_TEAL },
        { text = "BETA",     size = BS(11), color = COLOR_ORANGE },
        { text = "v" .. ver, size = BS(11), color = COLOR_WHITE },
    })

    MakeDivider(vbox, "BBTDiv1")

    local statDefs = {
        {key = "Yaw",    label = "Yaw\194\176"},
        {key = "Step",   label = "Step\194\176"},
        {key = "Snap",   label = "Snap"},
        {key = "Target", label = "Target\194\176"},
        {key = "Copied", label = "Copied\194\176"},
        {key = "Undo",   label = "Undo"},
        {key = "LookAt", label = "LookAt"},
    }
    BStat.ValueTexts = {}

    for _, def in ipairs(statDefs) do
        local _, value = MakeLabeledRow(vbox, "BStat_" .. def.key, def.label .. ":", "---",
            { labelWidth = BS(75), fontSize = BS(11), labelColor = COLOR_ORANGE, valueColor = COLOR_WHITE })
        BStat.ValueTexts[def.key] = value
    end

    MakeDivider(vbox, "BBTDiv2")

    MakeTextBlock(vbox, "BBTKeysLabel", BS(10), "Keybindings",
        COLOR_TEAL, { Left = 0, Top = 0, Right = 0, Bottom = 2 })

    local modKeys = {}
    if BBT_GetBuildStatus then
        local s = BBT_GetBuildStatus()
        modKeys = {
            {label = "Copy Object", key = s.keyCopyObject or "Shift+C"},
            {label = "Copy Angle",  key = s.keyCopyAngle or "Alt+C"},
            {label = "Undo",        key = s.keyUndo or "Shift+Z"},
        }
    else
        modKeys = {
            {label = "Copy Object", key = "Shift+C"},
            {label = "Copy Angle",  key = "Alt+C"},
            {label = "Undo",        key = "Shift+Z"},
        }
    end

    for i, kb in ipairs(modKeys) do
        MakeLabeledRow(vbox, "BStatKB" .. i, kb.label .. ":", kb.key,
            { labelWidth = BS(100), fontSize = BS(10), labelColor = COLOR_ORANGE, valueColor = COLOR_WHITE })
    end

    MakeDivider(vbox, "BBTDiv3")

    MakeTextBlock(vbox, "BBTGameLabel", BS(10), "Game Defaults",
        COLOR_TEAL, { Left = 0, Top = 0, Right = 0, Bottom = 2 })

    local gameKeys = {
        {label = "Step Cycle",    key = "L"},
        {label = "Snap Toggle",   key = "P"},
        {label = "Camera Mode",   key = "V"},
        {label = "Build Options", key = "Q"},
        {label = "Settings",      key = "F2"},
    }

    for i, kb in ipairs(gameKeys) do
        MakeLabeledRow(vbox, "BStatGK" .. i, kb.label .. ":", kb.key,
            { labelWidth = BS(100), fontSize = BS(10), labelColor = COLOR_ORANGE, valueColor = COLOR_WHITE })
    end

    local slot = canvas:AddChildToCanvas(border)
    slot:SetAutoSize(true)
    slot:SetAnchors({ Minimum = { X = 0, Y = 0.5 }, Maximum = { X = 0, Y = 0.5 } })
    slot:SetAlignment({ X = 0, Y = 0.5 })
    slot:SetPosition({ X = 20, Y = 0 })

    canvas.Visibility = 3
    border.Visibility = 3

    BStat.Root = root
    BStat.InViewport = false
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Settings menu — drill-down sections, F2 key, arrow-key nav
-- ─────────────────────────────────────────────────────────────────────────────
Settings = {
    Root         = nil,
    InViewport   = false,
    Open         = false,
    Selected     = 1,
    ViewMode     = "headers",
    DrillIndex   = 1,
    GuideSection = 1,
    Labels       = {},
    Values       = {},
    Hints        = {},
    TitleText    = nil,
    ContentText  = nil,
    PageText     = nil,
}

local SectionDefs = {
    {
        label = "Guide", stype = "guide",
    },
    {
        label = "Building Tools", stype = "settings",
        items = {
            { key = "undo_enabled",         label = "Undo",                  type = "bool",  hint = "Shift+Z last action" },
            { key = "undo_max_stack",        label = "Max Stack",             type = "int", min = 1, max = 50, hint = "Blocks remembered" },
            { key = "copyobject_enabled",    label = "Copy Object",           type = "bool",  hint = "Shift+C copy piece" },
            { key = "copyangle_enabled",     label = "Copy Angle\194\176",    type = "bool",  hint = "Alt+C match rotation" },
            { key = "rotation_1deg_enabled", label = "Fine Rotation 1\194\176", type = "bool", hint = "Can misalign <3\194\176" },
            { key = "rotation_5deg_enabled", label = "Fine Rotation 5\194\176", type = "bool" },
            { key = "rotation_10deg_enabled",label = "Fine Rotation 10\194\176",type = "bool" },
            { key = "bstat_enabled",         label = "Build Status HUD",      type = "bool",  hint = "Rotation/step overlay" },
            { label = "Redo",               type = "status", status = "P" },
            { label = "Text Signs",         type = "status", status = "P" },
        },
    },
    {
        label = "Placement Freedom", stype = "settings",
        items = {
            { key = "placement_allow_under_roof", label = "Furnace/Kiln Under Roof", type = "bool", hint = "Place under roofed areas" },
            { key = "freebuild_no_stability",     label = "No Stability Req",         type = "bool", hint = "Skip structural integrity" },
            { key = "placement_no_bonfire",        label = "No Bonfire Required",      type = "status", status = "W" },
            { key = "placement_no_roof_required",  label = "No Roof Required",         type = "status", status = "W" },
            { label = "Bonfire Stacking",         type = "status", status = "P" },
        },
    },
    {
        label = "Misc", stype = "settings",
        items = {
            { label = "Camera Modes",    type = "status", status = "P" },
            { label = "Replace Object",  type = "status", status = "P" },
            { label = "Selection Mode",  type = "status", status = "P" },
            { label = "Save Blueprint",  type = "status", status = "P" },
            { label = "Build Blueprint", type = "status", status = "P" },
        },
    },
    {
        label = "UI Settings", stype = "settings",
        items = {
            { key = "ui_menu_scale",  label = "Menu Size",  type = "int", min = 70, max = 150, step = 10, hint = "Settings panel %" },
            { key = "ui_bstat_scale", label = "BStat Size",  type = "int", min = 70, max = 150, step = 10, hint = "HUD overlay %" },
        },
    },
    {
        label = "Cheats", stype = "settings", cheat = true,
        items = {
            { key = "freebuild_no_cost", label = "No Build Cost",  type = "bool", hint = "No resources needed" },
            { label = "Instant Craft",   type = "status", status = "P" },
            { label = "No Craft Cost",   type = "status", status = "P" },
        },
    },
    {
        label = "Notes", stype = "notes",
    },
}

-- Minimum content height (pixels at 100% scale) — fits ~10 rows so the
-- panel stays stable when drilling between sections of different lengths.
local MIN_CONTENT_HEIGHT = 400

-- ─────────────────────────────────────────────────────────────────────────────
-- Guide sections — in-game feature reference (displayed inside Guide drill-in)
-- ─────────────────────────────────────────────────────────────────────────────
local GuideSections = {
    {
        title = "OVERVIEW",
        text = table.concat({
            "BetterBuildingTools adds precision building",
            "tools that the base game doesn't provide.",
            "",
            "Every feature can be toggled on or off in",
            "the Settings menu (F2). All settings apply",
            "immediately - no restart needed for toggles.",
            "",
            "Keybindings can be customized in config.txt.",
            "Restart the game after changing keybinds.",
            "",
            "This mod is in BETA. If something breaks,",
            "crashes, or behaves unexpectedly, please",
            "report it on the mod page!",
        }, "\n"),
    },
    {
        title = "COPY OBJECT",
        text = table.concat({
            "Look at any placed object and press the",
            "Copy Object key to instantly switch your",
            "build selection to that piece.",
            "",
            "No more scrolling through menus to find",
            "the block you need - just look at one",
            "that's already placed and grab it.",
            "",
            "Default key: Shift+C",
        }, "\n"),
    },
    {
        title = "COPY ANGLE",
        text = table.concat({
            "Look at a placed block and press Copy Angle",
            "to copy its exact rotation to your build",
            "preview. Great for matching wall angles,",
            "roof pitches, or any non-standard rotation.",
            "",
            "Default key: Alt+C",
            "",
            "IMPORTANT: The ghost preview does NOT",
            "visually rotate when you use Copy Angle.",
            "This is a known limitation, not a bug.",
            "The block WILL place at the correct angle.",
            "",
            "Workaround to refresh the ghost visual:",
            "  - Toggle destroy mode and back, or",
            "  - Exit and re-enter build mode.",
        }, "\n"),
    },
    {
        title = "FINE ROTATION",
        text = table.concat({
            "Adds 1\194\176, 5\194\176, and 10\194\176 steps to the game's",
            "built-in rotation cycle.",
            "",
            "Uses your existing Rotation Step key",
            "(default: L) - no new key needed. The",
            "extra steps are added to the cycle that",
            "L already scrolls through.",
            "",
            "Each increment can be enabled or disabled",
            "independently in Settings (F2).",
            "",
            "Note: the game auto-snaps blocks within",
            "~3\194\176 of a neighbor. The 1\194\176 step can cause",
            "subtle misalignment near existing pieces.",
            "Disable it in Settings if that bothers you.",
        }, "\n"),
    },
    {
        title = "UNDO",
        text = table.concat({
            "Misplaced a block? Press Undo to remove",
            "your last placed block - no need to hunt",
            "for it with the delete tool.",
            "",
            "Remembers up to 10 actions by default.",
            "You can increase this up to 50 in the",
            "Settings menu (F2) under Undo Max Stack.",
            "",
            "Default key: Shift+Z",
            "",
            "Undo tracks blocks placed in the current",
            "session. The stack resets when you exit",
            "the game.",
        }, "\n"),
    },
    {
        title = "BUILD STATUS HUD",
        text = table.concat({
            "The BStat overlay shows your current",
            "building state at a glance:",
            "",
            "  Yaw\194\176:     Your current rotation angle",
            "  Step\194\176:    Active rotation step size",
            "  Snap:     Snap mode (on/off)",
            "  Target\194\176:  Looked-at block angle",
            "  Copied\194\176:  Copy Angle lock status",
            "  Undo:     Stack count (used/max)",
            "  LookAt:   Block you are aiming at",
            "",
            "Values turn green when they match",
            "(e.g. Yaw\194\176 matches Target\194\176).",
            "",
            "The keybind reference at the bottom shows",
            "your active BBT and game keybinds.",
            "",
            "Can be toggled off in Settings (F2).",
        }, "\n"),
    },
    {
        title = "FREE BUILD",
        text = table.concat({
            "Optional cheats for creative building.",
            "All disabled by default.",
            "",
            "  - No Build Cost:",
            "    Build without consuming resources.",
            "",
            "  - No Stability:",
            "    Disables the structural integrity",
            "    check. Build floating structures.",
        }, "\n"),
    },
    {
        title = "PLACEMENT FREEDOM",
        text = table.concat({
            "Remove shelter and proximity restrictions",
            "for specific building items. Unlike Free",
            "Build, these don't affect resource cost.",
            "",
            "  - Allow Under Roof:",
            "    Place furnace and kiln under a roof.",
            "    Normally they require open sky above.",
            "",
            "  - No Roof Required:",
            "    Place workstations and beds without a",
            "    roof overhead.",
            "",
            "  - No Bonfire Required:",
            "    Build without needing a bonfire nearby.",
            "    Structures AND decorations.",
            "",
            "All toggles are fully reversible. Turn them",
            "off and the original restrictions return.",
            "",
            "Credit: inspired by zeeCameLsnake's",
            "Ultimate Base Freedom PAK mod.",
        }, "\n"),
    },
    {
        title = "KEYBINDINGS",
        text_fn = function()
            local co, ca, un = "Shift+C", "Alt+C", "Shift+Z"
            if BBT_GetBuildStatus then
                local s = BBT_GetBuildStatus()
                co = s.keyCopyObject or co
                ca = s.keyCopyAngle or ca
                un = s.keyUndo or un
            end
            return table.concat({
                "BBT Keybinds:",
                "  Copy Object: " .. co,
                "  Copy Angle: " .. ca,
                "  Undo: " .. un,
                "  Settings / Guide: F2",
                "",
                "Game Keybinds (defaults):",
                "  Rotation Step: L",
                "  Snap Toggle: P",
                "  Camera Mode: V",
                "  Build Options: Q",
                "",
                "BBT keybinds can be changed in config.txt:",
                "  Mods/BetterBuildingTools/config.txt",
                "Restart the game after editing keybinds.",
                "",
                "Game keybinds shown are defaults. If you",
                "rebound them in-game, they won't update here.",
            }, "\n")
        end,
    },
    {
        title = "KNOWN ISSUES",
        text = table.concat({
            "  - Ghost preview does not rotate with",
            "    Copy Angle\194\176. The block still places",
            "    correctly - the visual doesn't update.",
            "",
            "  - \"Restart All Mods\" may cause fatal",
            "    errors. Full game restart is recommended",
            "    for reliable reloading.",
            "",
            "  - 1\194\176 rotation can cause subtle misalignment",
            "    near existing structures due to the",
            "    game's auto-snap behavior (~3\194\176 range).",
        }, "\n"),
    },
}

local NotesText = table.concat({
    "  - Copy Angle\194\176 is a work in progress: placement",
    "    works but the ghost preview doesn't rotate",
    "    live. Toggle destroy/build mode to refresh.",
    "",
    "  - Keybindings can be changed in config.txt:",
    "    Mods/BetterBuildingTools/config.txt",
    "    Restart the game after editing keybinds.",
    "",
    "  - Build Status HUD may take a moment to",
    "    disappear after leaving build mode.",
    "",
    "  - \"Restart All Mods\" may cause fatal errors.",
    "    Full game restart is recommended for",
    "    reliable reloading.",
}, "\n")

-- ─────────────────────────────────────────────────────────────────────────────
-- Settings widget — build / refresh / navigate / drill
-- ─────────────────────────────────────────────────────────────────────────────

local function BuildSettings()
    if Settings.Root and Settings.Root:IsValid() then return end

    local gi = UEHelpers.GetGameInstance()
    if not gi or not gi:IsValid() then return end

    local root      = CreateWidget("UserWidget", gi, "BBTSettingsRoot")
    root.WidgetTree = CreateWidget("WidgetTree", root, "BBTSettingsTree")
    local canvas    = CreateWidget("CanvasPanel", root.WidgetTree, "BBTSettingsCanvas")
    root.WidgetTree.RootWidget = canvas

    local border    = CreateWidget("Border", canvas, "BBTSettingsBorder")
    border:SetBrushColor(COLOR_BG)
    border:SetPadding({ Left = MS(28), Top = MS(20), Right = MS(28), Bottom = MS(20) })

    local vbox      = CreateWidget("VerticalBox", border, "BBTSettingsBox")
    border:SetContent(vbox)

    local ver = "?"
    if BBT_GetBuildStatus then
        local s = BBT_GetBuildStatus()
        if s and s.version then ver = s.version end
    end

    local scaleLabel = ""
    local scaleVal = BBT_GetConfig and BBT_GetConfig("ui_menu_scale") or 100
    if scaleVal ~= 100 then scaleLabel = tostring(scaleVal) .. "%" end

    local headerParts = {
        { text = "BBT SETTINGS", size = MS(14), color = COLOR_TEAL },
        { text = "BETA",         size = MS(12), color = COLOR_ORANGE },
        { text = "v" .. ver,     size = MS(12), color = COLOR_WHITE },
    }
    if scaleLabel ~= "" then
        headerParts[#headerParts + 1] = { text = scaleLabel, size = MS(10), color = COLOR_DIM }
    end
    MakeHeaderRow(vbox, "Settings", headerParts)
    MakeDivider(vbox, "SettingsDiv1")

    Settings.Labels      = {}
    Settings.Values      = {}
    Settings.Hints       = {}
    Settings.TitleText   = nil
    Settings.ContentText = nil
    Settings.PageText    = nil

    -- Content area with minimum height for visual stability
    local contentSize = CreateWidget("SizeBox", vbox, "ContentSize")
    contentSize:SetMinDesiredWidth(MS(500))
    contentSize:SetMinDesiredHeight(MS(MIN_CONTENT_HEIGHT))
    local contentBox = CreateWidget("VerticalBox", contentSize, "ContentBox")
    contentSize:SetContent(contentBox)
    vbox:AddChildToVerticalBox(contentSize)

    -- ── HEADERS VIEW ──
    if Settings.ViewMode == "headers" then
        for i, sec in ipairs(SectionDefs) do
            local row = CreateWidget("HorizontalBox", contentBox, "SHdr" .. i)

            local labelBox = CreateWidget("SizeBox", row, "SHdrLB" .. i)
            labelBox:SetWidthOverride(MS(220))
            local label = CreateWidget("TextBlock", labelBox, "SHdrL" .. i)
            label.Font.Size = MS(13)
            label:SetText(FText("  " .. sec.label))
            label:SetColorAndOpacity(COLOR_WHITE)
            labelBox:SetContent(label)
            row:AddChildToHorizontalBox(labelBox)

            local arrow = CreateWidget("TextBlock", row, "SHdrA" .. i)
            arrow.Font.Size = MS(13)
            arrow:SetText(FText(">>"))
            arrow:SetColorAndOpacity(COLOR_DIM)
            row:AddChildToHorizontalBox(arrow)

            contentBox:AddChildToVerticalBox(row):SetPadding({ Left = 0, Top = MS(4), Right = 0, Bottom = MS(4) })

            Settings.Labels[i] = label
            Settings.Values[i] = arrow
        end

    -- ── SECTION VIEW (settings items) ──
    elseif Settings.ViewMode == "section" then
        local sec = SectionDefs[Settings.DrillIndex]

        local titleText = CreateWidget("TextBlock", contentBox, "SBackTitle")
        titleText.Font.Size = MS(13)
        titleText:SetText(FText("<< " .. sec.label:upper()))
        titleText:SetColorAndOpacity(sec.cheat and COLOR_ORANGE or COLOR_TEAL)
        contentBox:AddChildToVerticalBox(titleText):SetPadding({ Left = 0, Top = 0, Right = 0, Bottom = MS(2) })
        MakeDivider(contentBox, "SSectionDiv")

        if sec.items then
            for i, item in ipairs(sec.items) do
                local row = CreateWidget("HorizontalBox", contentBox, "SRow" .. i)

                local labelBox = CreateWidget("SizeBox", row, "SLabelBox" .. i)
                labelBox:SetWidthOverride(MS(220))
                local labelTxt = CreateWidget("TextBlock", labelBox, "SLabel" .. i)
                labelTxt.Font.Size = MS(12)
                labelTxt:SetText(FText("  " .. item.label))
                labelTxt:SetColorAndOpacity(COLOR_WHITE)
                labelBox:SetContent(labelTxt)
                row:AddChildToHorizontalBox(labelBox)

                local valBox = CreateWidget("SizeBox", row, "SValBox" .. i)
                valBox:SetWidthOverride(MS(55))
                local valTxt = CreateWidget("TextBlock", valBox, "SVal" .. i)
                valTxt.Font.Size = MS(12)
                valTxt:SetText(FText("---"))
                valTxt:SetColorAndOpacity(COLOR_VALUE_OFF)
                valBox:SetContent(valTxt)
                row:AddChildToHorizontalBox(valBox)

                local hintTxt = CreateWidget("TextBlock", row, "SHint" .. i)
                hintTxt.Font.Size = MS(10)
                hintTxt:SetText(FText(item.hint or ""))
                hintTxt:SetColorAndOpacity(COLOR_HINT)
                row:AddChildToHorizontalBox(hintTxt)

                contentBox:AddChildToVerticalBox(row):SetPadding({ Left = 0, Top = MS(3), Right = 0, Bottom = MS(3) })

                Settings.Labels[i] = labelTxt
                Settings.Values[i] = valTxt
                Settings.Hints[i]  = hintTxt
            end
        end

    -- ── GUIDE VIEW ──
    elseif Settings.ViewMode == "guide" then
        local titleRow = CreateWidget("HorizontalBox", contentBox, "GTitle")

        local titleBox = CreateWidget("SizeBox", titleRow, "GTitleBox")
        titleBox:SetWidthOverride(MS(200))
        local backLabel = CreateWidget("TextBlock", titleBox, "GBackLabel")
        backLabel.Font.Size = MS(13)
        backLabel:SetText(FText("<< GUIDE"))
        backLabel:SetColorAndOpacity(COLOR_TEAL)
        titleBox:SetContent(backLabel)
        titleRow:AddChildToHorizontalBox(titleBox)

        local pageText = CreateWidget("TextBlock", titleRow, "GPage")
        pageText.Font.Size = MS(12)
        pageText:SetText(FText("PAGE 1/" .. #GuideSections))
        pageText:SetColorAndOpacity(COLOR_HINT)
        titleRow:AddChildToHorizontalBox(pageText)
        Settings.PageText = pageText

        contentBox:AddChildToVerticalBox(titleRow):SetPadding({ Left = 0, Top = 0, Right = 0, Bottom = MS(2) })
        MakeDivider(contentBox, "GDiv")

        local sectionTitle = CreateWidget("TextBlock", contentBox, "GSectionTitle")
        sectionTitle.Font.Size = MS(13)
        sectionTitle:SetText(FText(GuideSections[1].title))
        sectionTitle:SetColorAndOpacity(COLOR_TEAL)
        contentBox:AddChildToVerticalBox(sectionTitle):SetPadding({ Left = 0, Top = 0, Right = 0, Bottom = MS(6) })
        Settings.TitleText = sectionTitle

        local gs = GuideSections[Settings.GuideSection]
        local contentBody = gs.text
        if gs.text_fn then contentBody = gs.text_fn() end
        local contentText = CreateWidget("TextBlock", contentBox, "GContent")
        contentText.Font.Size = MS(10)
        contentText:SetText(FText(contentBody or ""))
        contentText:SetColorAndOpacity(COLOR_WHITE)
        contentBox:AddChildToVerticalBox(contentText):SetPadding({ Left = MS(4), Top = 0, Right = 0, Bottom = MS(8) })
        Settings.ContentText = contentText

    -- ── NOTES VIEW ──
    elseif Settings.ViewMode == "notes" then
        local titleText = CreateWidget("TextBlock", contentBox, "NBackTitle")
        titleText.Font.Size = MS(13)
        titleText:SetText(FText("<< NOTES"))
        titleText:SetColorAndOpacity(COLOR_TEAL)
        contentBox:AddChildToVerticalBox(titleText):SetPadding({ Left = 0, Top = 0, Right = 0, Bottom = MS(2) })
        MakeDivider(contentBox, "NDiv")

        local notesBody = CreateWidget("TextBlock", contentBox, "NBody")
        notesBody.Font.Size = MS(9)
        notesBody:SetText(FText(NotesText))
        notesBody:SetColorAndOpacity(COLOR_HINT)
        contentBox:AddChildToVerticalBox(notesBody):SetPadding({ Left = MS(4), Top = 0, Right = 0, Bottom = MS(8) })
    end

    -- ── BOTTOM — legend + nav bar ──

    if Settings.ViewMode == "headers" or Settings.ViewMode == "section" then
        MakeDivider(vbox, "SLegendDiv")
        MakeTextBlock(vbox, "SLegend", MS(9),
            "W = Work in Progress  |  P = Planned  |  D = Disabled",
            COLOR_DIM, { Left = MS(4), Top = 0, Right = 0, Bottom = MS(4) })
    elseif Settings.ViewMode == "guide" then
        MakeDivider(vbox, "GLegendDiv")
        MakeTextBlock(vbox, "GLegend", MS(9),
            "Guide  |  10 pages  |  BetterBuildingTools",
            COLOR_DIM, { Left = MS(4), Top = 0, Right = 0, Bottom = MS(4) })
    elseif Settings.ViewMode == "notes" then
        MakeDivider(vbox, "NLegendDiv")
        MakeTextBlock(vbox, "NLegend", MS(9),
            "Last updated: v0.35",
            COLOR_DIM, { Left = MS(4), Top = 0, Right = 0, Bottom = MS(4) })
    end

    MakeDivider(vbox, "SettingsDivBot")

    if Settings.ViewMode == "headers" then
        MakeNavBar(vbox, "Settings", {
            { label = "Select", keys = "Up/Down" },
            { label = "Open",   keys = "Right" },
            { label = "Close",  keys = "F2" },
            { label = "Scale",  keys = "PgUp/Dn" },
        })
    elseif Settings.ViewMode == "section" then
        MakeNavBar(vbox, "Settings", {
            { label = "Select", keys = "Up/Down" },
            { label = "Change", keys = "Left/Right" },
            { label = "Back",   keys = "F2" },
            { label = "Scale",  keys = "PgUp/Dn" },
        })
    elseif Settings.ViewMode == "guide" then
        MakeNavBar(vbox, "Settings", {
            { label = "Page", keys = "Left/Right" },
            { label = "Back", keys = "F2" },
            { label = "Scale", keys = "PgUp/Dn" },
        })
    else
        MakeNavBar(vbox, "Settings", {
            { label = "Back", keys = "F2" },
            { label = "Scale", keys = "PgUp/Dn" },
        })
    end

    local slot = canvas:AddChildToCanvas(border)
    slot:SetAutoSize(true)
    slot:SetAnchors({ Minimum = { X = 0, Y = 0.5 }, Maximum = { X = 0, Y = 0.5 } })
    slot:SetAlignment({ X = 0, Y = 0.5 })
    slot:SetPosition({ X = 210, Y = 0 })

    canvas.Visibility = 3
    border.Visibility = 3

    Settings.Root = root
    Settings.InViewport = false
end

local function RefreshSettings()
    if Settings.ViewMode == "headers" then
        for i = 1, #SectionDefs do
            local lbl = Settings.Labels[i]
            local arr = Settings.Values[i]
            local sel = (i == Settings.Selected)
            if lbl and lbl:IsValid() then
                local prefix = sel and "\194\187 " or "  "
                lbl:SetText(FText(prefix .. SectionDefs[i].label))
                if sel then
                    lbl:SetColorAndOpacity(COLOR_SELECTED)
                elseif SectionDefs[i].cheat then
                    lbl:SetColorAndOpacity(COLOR_ORANGE)
                else
                    lbl:SetColorAndOpacity(COLOR_WHITE)
                end
            end
            if arr and arr:IsValid() then
                arr:SetColorAndOpacity(sel and COLOR_SELECTED or COLOR_DIM)
            end
        end

    elseif Settings.ViewMode == "section" then
        local sec = SectionDefs[Settings.DrillIndex]
        if not sec or not sec.items then return end
        for i, item in ipairs(sec.items) do
            local lbl  = Settings.Labels[i]
            local vlbl = Settings.Values[i]
            local sel  = (i == Settings.Selected)
            if lbl and lbl:IsValid() then
                local prefix = sel and "\194\187 " or "  "
                lbl:SetText(FText(prefix .. item.label))
                if item.type == "status" then
                    lbl:SetColorAndOpacity(sel and COLOR_SELECTED or COLOR_DIM)
                elseif sec.cheat and not sel then
                    lbl:SetColorAndOpacity(COLOR_ORANGE)
                elseif sel then
                    lbl:SetColorAndOpacity(COLOR_SELECTED)
                else
                    lbl:SetColorAndOpacity(COLOR_WHITE)
                end
            end
            if vlbl and vlbl:IsValid() then
                if item.type == "status" then
                    vlbl:SetText(FText(item.status))
                    vlbl:SetColorAndOpacity(COLOR_DIM)
                elseif item.type == "bool" then
                    local val = item.key and BBT_GetConfig and BBT_GetConfig(item.key)
                    vlbl:SetText(FText(val and "ON" or "OFF"))
                    vlbl:SetColorAndOpacity(val and COLOR_VALUE_ON or COLOR_VALUE_OFF)
                elseif item.type == "int" then
                    local val = item.key and BBT_GetConfig and BBT_GetConfig(item.key)
                    vlbl:SetText(FText(tostring(val or 0)))
                    vlbl:SetColorAndOpacity(COLOR_VALUE_NUM)
                end
            end
        end

    elseif Settings.ViewMode == "guide" then
        if not Settings.TitleText or not Settings.TitleText:IsValid() then return end
        local s = GuideSections[Settings.GuideSection]
        Settings.TitleText:SetText(FText(s.title))
        Settings.PageText:SetText(FText("PAGE " .. Settings.GuideSection .. "/" .. #GuideSections))
        local text = s.text
        if s.text_fn then text = s.text_fn() end
        Settings.ContentText:SetText(FText(text or ""))
    end
end

local function RebuildSettingsWidget()
    if Settings.InViewport and Settings.Root and Settings.Root:IsValid() then
        Settings.Root:RemoveFromViewport()
    end
    Settings.Root       = nil
    Settings.InViewport = false
    Settings.Labels     = {}
    Settings.Values     = {}
    Settings.Hints      = {}
    BuildSettings()
    if Settings.Open and Settings.Root and Settings.Root:IsValid() then
        Settings.Root:AddToViewport(950)
        Settings.InViewport = true
    end
    RefreshSettings()
end

local function DrillIn()
    if Settings.ViewMode ~= "headers" then return end
    local sec = SectionDefs[Settings.Selected]
    if not sec then return end
    Settings.DrillIndex = Settings.Selected
    if sec.stype == "guide" then
        Settings.ViewMode = "guide"
        Settings.GuideSection = 1
    elseif sec.stype == "notes" then
        Settings.ViewMode = "notes"
    else
        Settings.ViewMode = "section"
        Settings.Selected = 1
    end
    RebuildSettingsWidget()
end

local function DrillOut()
    if Settings.ViewMode == "headers" then return end
    local wasAt = Settings.DrillIndex
    Settings.ViewMode = "headers"
    Settings.Selected = wasAt
    RebuildSettingsWidget()
end

local function SettingsNav(delta)
    if not Settings.Open then return end
    if Settings.ViewMode == "headers" then
        local count = #SectionDefs
        Settings.Selected = ((Settings.Selected - 1 + delta) % count) + 1
        RefreshSettings()
    elseif Settings.ViewMode == "section" then
        local sec = SectionDefs[Settings.DrillIndex]
        if not sec or not sec.items or #sec.items == 0 then return end
        local count = #sec.items
        Settings.Selected = ((Settings.Selected - 1 + delta) % count) + 1
        RefreshSettings()
    elseif Settings.ViewMode == "guide" then
        local count = #GuideSections
        Settings.GuideSection = ((Settings.GuideSection - 1 + delta) % count) + 1
        RefreshSettings()
    end
end

local function SettingsChange(delta)
    if not Settings.Open then return end
    if Settings.ViewMode == "headers" then
        if delta > 0 then DrillIn() end
        return
    end
    if Settings.ViewMode == "guide" then
        local count = #GuideSections
        Settings.GuideSection = ((Settings.GuideSection - 1 + delta) % count) + 1
        RefreshSettings()
        return
    end
    if Settings.ViewMode ~= "section" then return end
    local sec = SectionDefs[Settings.DrillIndex]
    if not sec or not sec.items then return end
    local item = sec.items[Settings.Selected]
    if not item or item.type == "status" then return end
    if not BBT_GetConfig or not BBT_SetConfig then return end
    local val = BBT_GetConfig(item.key)
    if item.type == "bool" then
        BBT_SetConfig(item.key, not val)
    elseif item.type == "int" then
        local step = item.step or 1
        local n = (val or 0) + delta * step
        if item.min and n < item.min then n = item.min end
        if item.max and n > item.max then n = item.max end
        BBT_SetConfig(item.key, n)
    end
    if BBT_SaveConfig then BBT_SaveConfig() end

    if item.key == "ui_menu_scale" then
        RebuildSettingsWidget()
        return
    end
    if item.key == "ui_bstat_scale" then
        if BStat.InViewport and BStat.Root and BStat.Root:IsValid() then
            BStat.Root:RemoveFromViewport()
            BStat.InViewport = false
        end
        BStat.Root = nil
    end

    RefreshSettings()
end

local function ToggleSettings()
    Settings.Open = not Settings.Open
    if Settings.Open then
        if not Settings.Root or not Settings.Root:IsValid() then
            Settings.InViewport = false
            BuildSettings()
        end
        if Settings.Root and Settings.Root:IsValid() and not Settings.InViewport then
            Settings.Root:AddToViewport(950)
            Settings.InViewport = true
        end
        RefreshSettings()
    elseif Settings.InViewport and Settings.Root and Settings.Root:IsValid() then
        Settings.Root:RemoveFromViewport()
        Settings.InViewport = false
    end
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Keybinds (VK codes)
-- ─────────────────────────────────────────────────────────────────────────────
local function HandleF2()
    if Settings.Open and Settings.ViewMode ~= "headers" then
        DrillOut()
    else
        ToggleSettings()
    end
end

local function HandleUp()    SettingsNav(-1) end
local function HandleDown()  SettingsNav(1) end
local function HandleLeft()  SettingsChange(-1) end
local function HandleRight() SettingsChange(1) end

local function HandlePgUp()
    if not Settings.Open then return end
    local scale = (BBT_GetConfig and BBT_GetConfig("ui_menu_scale")) or 100
    scale = math.min(150, scale + 10)
    if BBT_SetConfig then BBT_SetConfig("ui_menu_scale", scale) end
    if BBT_SaveConfig then BBT_SaveConfig() end
    RebuildSettingsWidget()
end

local function HandlePgDn()
    if not Settings.Open then return end
    local scale = (BBT_GetConfig and BBT_GetConfig("ui_menu_scale")) or 100
    scale = math.max(70, scale - 10)
    if BBT_SetConfig then BBT_SetConfig("ui_menu_scale", scale) end
    if BBT_SaveConfig then BBT_SaveConfig() end
    RebuildSettingsWidget()
end

local Binds = {
    { 0x71, function() ExecuteInGameThread(HandleF2) end },      -- F2
    { 0x26, function() ExecuteInGameThread(HandleUp) end },      -- Up
    { 0x28, function() ExecuteInGameThread(HandleDown) end },    -- Down
    { 0x25, function() ExecuteInGameThread(HandleLeft) end },    -- Left
    { 0x27, function() ExecuteInGameThread(HandleRight) end },   -- Right
    { 0x21, function() ExecuteInGameThread(HandlePgUp) end },    -- PgUp
    { 0x22, function() ExecuteInGameThread(HandlePgDn) end },    -- PgDn
}

for _, bind in ipairs(Binds) do
    RegisterKeyBind(bind[1], bind[2])
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Main update loop
-- ─────────────────────────────────────────────────────────────────────────────
local function OnTick()
    if not BStat.Root or not BStat.Root:IsValid() then
        BuildBStat()
    end
    UpdateBStat()
    if Settings.Open then
        RefreshSettings()
    end
end

LoopInGameThreadWithDelay(150, OnTick)

log("Loaded - BStat HUD + drill-down settings + guide + undo tracking active (F2 = settings)")
