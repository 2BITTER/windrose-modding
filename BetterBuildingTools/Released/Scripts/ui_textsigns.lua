-- ui_textsigns.lua — Sign edit panel
-- Built at startup (like F2 menu), shown/hidden with AddToViewport/RemoveFromViewport
-- Keyboard input handled by C++ WH_KEYBOARD_LL hook

local UEHelpers = require("UEHelpers")
local H = require("ui_helpers")
local function log(msg) print("[BBT-TextSigns] " .. tostring(msg)) end

local M = {}
M.BGRoot = nil
M.Showing = false

local textLabel = nil

function M.BuildBG()
    if M.BGRoot and M.BGRoot:IsValid() then return end

    local gi = UEHelpers.GetGameInstance()
    if not gi or not gi:IsValid() then return end

    -- Exact F2/BStat pattern
    local root      = H.CreateWidget("UserWidget", gi, "BBTSignPanelRoot")
    root.WidgetTree  = H.CreateWidget("WidgetTree", root, "BBTSignPanelTree")
    local canvas     = H.CreateWidget("CanvasPanel", root.WidgetTree, "BBTSignPanelCanvas")
    root.WidgetTree.RootWidget = canvas

    local border     = H.CreateWidget("Border", canvas, "BBTSignPanelBorder")
    border:SetBrushColor(H.COLOR_BG)
    border:SetPadding({ Left = H.BS(16), Top = H.BS(10), Right = H.BS(16), Bottom = H.BS(10) })

    local vbox       = H.CreateWidget("VerticalBox", border, "BBTSignPanelVBox")
    border:SetContent(vbox)

    -- Title row (same style as F2 header)
    H.MakeTextBlock(vbox, "BBTSignPanelTitle", H.BS(13), "EDIT SIGN TEXT",
        H.SlateColor(0.4, 0.8, 1.0, 1.0), { Left = 0, Top = 0, Right = 0, Bottom = H.BS(2) })

    -- Divider
    H.MakeDivider(vbox, "BBTSignPanelDiv1")

    -- Text display (what the user is typing)
    textLabel = H.MakeTextBlock(vbox, "BBTSignPanelText", H.BS(16), "_",
        H.SlateColor(1.0, 1.0, 1.0, 1.0), { Left = H.BS(4), Top = H.BS(2), Right = 0, Bottom = H.BS(2) })

    -- Divider
    H.MakeDivider(vbox, "BBTSignPanelDiv2")

    -- Hint (small, dimmed, same style as F2 footer)
    H.MakeTextBlock(vbox, "BBTSignPanelHint", H.BS(9), "Enter = Apply  |  Esc = Cancel  |  Backspace = Delete",
        H.SlateColor(0.5, 0.5, 0.5, 1.0), { Left = 0, Top = 0, Right = 0, Bottom = 0 })

    -- DON'T add to viewport yet — just keep the reference
    M.BGRoot = root
    log("Sign edit panel built (startup)")
end

function M.Show()
    if M.Showing then return end
    if not M.BGRoot or not M.BGRoot:IsValid() then return end
    M.BGRoot:AddToViewport(950)
    M.Showing = true
end

function M.Hide()
    if not M.Showing then return end
    if M.BGRoot and M.BGRoot:IsValid() then
        M.BGRoot:RemoveFromViewport()
    end
    M.Showing = false
end

function M.Update()
    if not BBT_GetSignState then return end
    local state = BBT_GetSignState()

    if state.editing and not M.Showing then
        M.Show()
    elseif state.editing and M.Showing then
        -- Update typed text
        if textLabel and textLabel:IsValid() then
            local display = state.currentText
            if display == "" then display = "" end
            pcall(function() textLabel:SetText(FText(display .. "_")) end)
        end
    elseif not state.editing and M.Showing then
        M.Hide()
    end
end

function M.Submit() end
function M.Cancel() end

return M
