-- BetterBuildingTools — BStat HUD (persistent overlay during build mode)
local UEHelpers = require("UEHelpers")

local function create(H)
    local BStat = {
        Root       = nil,
        InViewport = false,
        ValueTexts = {},
        _settingsRef = nil,  -- set later by main.lua
    }

    function BStat.SetSettingsRef(ref) BStat._settingsRef = ref end

    function BStat.Update()
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

        if BStat._settingsRef and BStat._settingsRef.Open then
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

        BStat.ValueTexts.Yaw:SetText(FText(H.FormatDeg(s.rotationYaw) .. "\194\176"))
        BStat.ValueTexts.Yaw:SetColorAndOpacity(yawMatchesTarget and H.COLOR_GREEN or H.COLOR_WHITE)

        BStat.ValueTexts.Step:SetText(FText(s.rotationStep and (tostring(s.rotationStep) .. "\194\176") or "---"))
        BStat.ValueTexts.Step:SetColorAndOpacity(H.COLOR_WHITE)

        local snapStr = H.SnapLabel(s.snappingMode)
        local snapColor = H.COLOR_WHITE
        if snapStr == "ON" then snapColor = H.COLOR_GREEN
        elseif snapStr == "OFF" then snapColor = H.COLOR_DIM end
        BStat.ValueTexts.Snap:SetText(FText(snapStr))
        BStat.ValueTexts.Snap:SetColorAndOpacity(snapColor)

        if s.lookAtValid then
            BStat.ValueTexts.Target:SetText(FText(H.FormatDeg(s.lookAtYaw) .. "\194\176"))
            BStat.ValueTexts.Target:SetColorAndOpacity(yawMatchesTarget and H.COLOR_GREEN or H.COLOR_WHITE)
        else
            BStat.ValueTexts.Target:SetText(FText("---"))
            BStat.ValueTexts.Target:SetColorAndOpacity(H.COLOR_DIM)
        end

        if s.copyAngleHeld then
            BStat.ValueTexts.Copied:SetText(FText(H.FormatDeg(s.copyAngleYaw) .. "\194\176"))
            BStat.ValueTexts.Copied:SetColorAndOpacity(H.COLOR_GREEN)
        else
            BStat.ValueTexts.Copied:SetText(FText("OFF"))
            BStat.ValueTexts.Copied:SetColorAndOpacity(H.COLOR_DIM)
        end

        BStat.ValueTexts.Undo:SetText(FText((s.undoCount or 0) .. "/" .. (s.undoMax or 0)))
        BStat.ValueTexts.Undo:SetColorAndOpacity(H.COLOR_WHITE)

        if s.lookAtValid and s.lookAtName then
            BStat.ValueTexts.LookAt:SetText(FText(H.CleanItemName(s.lookAtName)))
            BStat.ValueTexts.LookAt:SetColorAndOpacity(H.COLOR_WHITE)
        else
            BStat.ValueTexts.LookAt:SetText(FText("---"))
            BStat.ValueTexts.LookAt:SetColorAndOpacity(H.COLOR_DIM)
        end
    end

    function BStat.Build()
        if BStat.Root and BStat.Root:IsValid() then return end

        local gi = UEHelpers.GetGameInstance()
        if not gi or not gi:IsValid() then return end

        local root          = H.CreateWidget("UserWidget", gi, "BBTBStatRoot")
        root.WidgetTree     = H.CreateWidget("WidgetTree", root, "BBTBStatTree")
        local canvas        = H.CreateWidget("CanvasPanel", root.WidgetTree, "BBTBStatCanvas")
        root.WidgetTree.RootWidget = canvas

        local border        = H.CreateWidget("Border", canvas, "BBTBStatBorder")
        border:SetBrushColor(H.COLOR_BG)
        border:SetPadding({ Left = H.BS(16), Top = H.BS(12), Right = H.BS(16), Bottom = H.BS(12) })

        local vbox          = H.CreateWidget("VerticalBox", border, "BBTBStatBox")
        border:SetContent(vbox)

        local ver = "?"
        if BBT_GetBuildStatus then
            local s = BBT_GetBuildStatus()
            if s and s.version then ver = s.version end
        end
        H.MakeHeaderRow(vbox, "BStat", {
            { text = "BBT",      size = H.BS(12), color = H.COLOR_TEAL },
            { text = "v" .. ver, size = H.BS(11), color = H.COLOR_WHITE },
        })

        H.MakeDivider(vbox, "BBTDiv1")

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
            local _, value = H.MakeLabeledRow(vbox, "BStat_" .. def.key, def.label .. ":", "---",
                { labelWidth = H.BS(75), fontSize = H.BS(11), labelColor = H.COLOR_ORANGE, valueColor = H.COLOR_WHITE })
            BStat.ValueTexts[def.key] = value
        end

        H.MakeDivider(vbox, "BBTDiv2")

        H.MakeTextBlock(vbox, "BBTKeysLabel", H.BS(10), "Keybindings",
            H.COLOR_TEAL, { Left = 0, Top = 0, Right = 0, Bottom = 2 })

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
            H.MakeLabeledRow(vbox, "BStatKB" .. i, kb.label .. ":", kb.key,
                { labelWidth = H.BS(100), fontSize = H.BS(10), labelColor = H.COLOR_ORANGE, valueColor = H.COLOR_WHITE })
        end

        H.MakeDivider(vbox, "BBTDiv3")

        H.MakeTextBlock(vbox, "BBTGameLabel", H.BS(10), "Game Defaults",
            H.COLOR_TEAL, { Left = 0, Top = 0, Right = 0, Bottom = 2 })

        local gameKeys = {
            {label = "Step Cycle",    key = "L"},
            {label = "Snap Toggle",   key = "P"},
            {label = "Camera Mode",   key = "V"},
            {label = "Build Options", key = "Q"},
            {label = "Settings",      key = "F2"},
        }

        for i, kb in ipairs(gameKeys) do
            H.MakeLabeledRow(vbox, "BStatGK" .. i, kb.label .. ":", kb.key,
                { labelWidth = H.BS(100), fontSize = H.BS(10), labelColor = H.COLOR_ORANGE, valueColor = H.COLOR_WHITE })
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

    return BStat
end
return create
