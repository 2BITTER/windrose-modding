-- BetterBuildingTools — Shared widget primitives, colors, scale functions
local H = {}

-- ─────────────────────────────────────────────────────────────────────────────
-- UMG widget primitives
-- ─────────────────────────────────────────────────────────────────────────────
function H.LinearColor(r, g, b, a)
    return { R = r, G = g, B = b, A = a }
end

function H.SlateColor(r, g, b, a)
    return { SpecifiedColor = H.LinearColor(r, g, b, a), ColorUseRule = 0 }
end

function H.CreateWidget(className, owner, name)
    return StaticConstructObject(StaticFindObject("/Script/UMG." .. className), owner, FName(name))
end

function H.MakeTextBlock(parent, name, size, text, color, padding)
    local block     = H.CreateWidget("TextBlock", parent, name)
    block.Font.Size = size
    block:SetText(FText(text))
    block:SetColorAndOpacity(color)
    if parent.AddChildToVerticalBox then
        parent:AddChildToVerticalBox(block):SetPadding(padding or { Left = 0, Top = 0, Right = 0, Bottom = 0 })
    end
    return block
end

function H.MakeDivider(parent, name)
    local box = H.CreateWidget("SizeBox", parent, name .. "_Box")
    box:SetHeightOverride(1.0)
    local line = H.CreateWidget("Border", box, name .. "_Line")
    line:SetBrushColor(H.LinearColor(1.0, 1.0, 1.0, 0.12))
    box:SetContent(line)
    parent:AddChildToVerticalBox(box):SetPadding({ Left = 0, Top = 6, Right = 0, Bottom = 6 })
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Shared color palette (all alphas 1.0 except backgrounds)
-- ─────────────────────────────────────────────────────────────────────────────
H.COLOR_TEAL      = H.SlateColor(0.0, 1.0, 0.7, 1.0)
H.COLOR_ORANGE    = H.SlateColor(1.0, 0.55, 0.0, 1.0)
H.COLOR_GREEN     = H.SlateColor(0.1, 1.0, 0.45, 1.0)
H.COLOR_WHITE     = H.SlateColor(0.9, 0.9, 0.9, 1.0)
H.COLOR_DIM       = H.SlateColor(0.35, 0.35, 0.35, 1.0)
H.COLOR_PIPE      = H.SlateColor(0.45, 0.45, 0.45, 1.0)
H.COLOR_HINT      = H.SlateColor(0.5, 0.5, 0.5, 1.0)
H.COLOR_BG        = H.LinearColor(0.02, 0.02, 0.03, 0.82)
H.COLOR_SELECTED  = H.SlateColor(0.0, 1.0, 1.0, 1.0)
H.COLOR_VALUE_ON  = H.SlateColor(0.1, 1.0, 0.45, 1.0)
H.COLOR_VALUE_OFF = H.SlateColor(0.35, 0.35, 0.35, 1.0)
H.COLOR_VALUE_NUM = H.SlateColor(0.9, 0.9, 0.5, 1.0)

-- ─────────────────────────────────────────────────────────────────────────────
-- UI construction helpers
-- ─────────────────────────────────────────────────────────────────────────────
function H.MakeLabeledRow(parent, name, labelText, valueText, opts)
    opts = opts or {}
    local row = H.CreateWidget("HorizontalBox", parent, name .. "_Row")
    local labelBox = H.CreateWidget("SizeBox", row, name .. "_LB")
    labelBox:SetWidthOverride(opts.labelWidth or 100)
    local label = H.CreateWidget("TextBlock", labelBox, name .. "_L")
    label.Font.Size = opts.fontSize or 11
    label:SetText(FText(labelText))
    label:SetColorAndOpacity(opts.labelColor or H.COLOR_ORANGE)
    labelBox:SetContent(label)
    row:AddChildToHorizontalBox(labelBox)
    local value = H.CreateWidget("TextBlock", row, name .. "_V")
    value.Font.Size = opts.fontSize or 11
    value:SetText(FText(valueText or "---"))
    value:SetColorAndOpacity(opts.valueColor or H.COLOR_WHITE)
    row:AddChildToHorizontalBox(value)
    parent:AddChildToVerticalBox(row):SetPadding(opts.padding or { Left = 0, Top = 0, Right = 0, Bottom = 0 })
    return label, value
end

function H.MakeHeaderRow(parent, prefix, parts)
    local row = H.CreateWidget("HorizontalBox", parent, prefix .. "HeaderRow")
    local texts = {}
    for i, part in ipairs(parts) do
        if i > 1 then
            local sep = H.CreateWidget("TextBlock", row, prefix .. "HSep" .. i)
            sep.Font.Size = 11
            sep:SetText(FText("  |  "))
            sep:SetColorAndOpacity(H.COLOR_PIPE)
            row:AddChildToHorizontalBox(sep)
        end
        local text = H.CreateWidget("TextBlock", row, prefix .. "HPart" .. i)
        text.Font.Size = part.size or 12
        text:SetText(FText(part.text))
        text:SetColorAndOpacity(part.color or H.COLOR_WHITE)
        row:AddChildToHorizontalBox(text)
        texts[i] = text
    end
    parent:AddChildToVerticalBox(row):SetPadding({ Left = 0, Top = 0, Right = 0, Bottom = 2 })
    return row, texts
end

function H.MakeNavBar(parent, prefix, actions)
    local row = H.CreateWidget("HorizontalBox", parent, prefix .. "NavRow")
    for i, act in ipairs(actions) do
        if i > 1 then
            local sep = H.CreateWidget("TextBlock", row, prefix .. "NavSep" .. i)
            sep.Font.Size = 10
            sep:SetText(FText("  |  "))
            sep:SetColorAndOpacity(H.COLOR_PIPE)
            row:AddChildToHorizontalBox(sep)
        end
        local label = H.CreateWidget("TextBlock", row, prefix .. "NavL" .. i)
        label.Font.Size = 10
        label:SetText(FText(act.label .. ": "))
        label:SetColorAndOpacity(H.COLOR_ORANGE)
        row:AddChildToHorizontalBox(label)
        local keys = H.CreateWidget("TextBlock", row, prefix .. "NavK" .. i)
        keys.Font.Size = 10
        keys:SetText(FText(act.keys))
        keys:SetColorAndOpacity(H.COLOR_WHITE)
        row:AddChildToHorizontalBox(keys)
    end
    parent:AddChildToVerticalBox(row):SetPadding({ Left = 0, Top = 0, Right = 0, Bottom = 0 })
    return row
end

-- ─────────────────────────────────────────────────────────────────────────────
-- UI scale helpers
-- ─────────────────────────────────────────────────────────────────────────────
function H.GetMenuScale()
    if BBT_GetConfig then return (BBT_GetConfig("ui_menu_scale") or 100) / 100 end
    return 1.0
end

function H.GetBStatScale()
    if BBT_GetConfig then return (BBT_GetConfig("ui_bstat_scale") or 100) / 100 end
    return 1.0
end

function H.MS(base)
    return math.max(1, math.floor(base * H.GetMenuScale()))
end

function H.BS(base)
    return math.max(1, math.floor(base * H.GetBStatScale()))
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Display format helpers
-- ─────────────────────────────────────────────────────────────────────────────
function H.FormatDeg(deg)
    if not deg then return "---" end
    return string.format("%.1f", deg)
end

function H.SnapLabel(mode)
    if not mode then return "---" end
    if mode == 0 then return "OFF"
    elseif mode == 2 then return "ON"
    else return tostring(mode)
    end
end

function H.CleanItemName(raw)
    if not raw then return "---" end
    local name = raw
    name = name:gsub("^DA_BI_", "")
    name = name:gsub("_", " ")
    return name
end

return H
