-- BetterBuildingTools — Settings menu (drill-down sections, F2 key, arrow-key nav)
local UEHelpers = require("UEHelpers")

local function create(H)
    local Settings = {
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
        _bstatRef    = nil,  -- set later by main.lua
    }

    function Settings.SetBStatRef(ref) Settings._bstatRef = ref end

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
                { label = "Redo",               type = "status", status = "P" },
                { label = "Text Signs",         type = "status", status = "P" },
            },
        },
        {
            label = "Placement Freedom", stype = "settings",
            items = {
                { key = "placement_allow_under_roof", label = "Furnace/Kiln Under Roof", type = "bool", hint = "Place under roofed areas" },
                { key = "freebuild_no_stability",     label = "No Stability Req",         type = "bool", hint = "Skip structural integrity" },
                { key = "placement_no_bonfire",        label = "No Bonfire Required",      type = "bool", hint = "Build + craft anywhere" },
                { key = "placement_no_roof_required",  label = "No Roof Required",         type = "bool", hint = "Beds/stations no roof" },
                { key = "placement_no_bell_shore",    label = "No Shore Required",        type = "bool", hint = "Bells/docks place anywhere" },
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
                { key = "bstat_enabled",  label = "Build Status HUD", type = "bool", hint = "Enable/disable (overrides F3)" },
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
                "This mod is in active development.",
                "If something breaks or behaves unexpectedly,",
                "please report it on the mod page!",
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
        "  !! BStat HUD is now toggled with F3 !!",
        "  It no longer appears automatically in",
        "  build mode. Press F3 to show/hide.",
        "  F2 master toggle still works.",
        "",
        "  - No Bonfire + No Shore are new in v0.36.",
        "    Wharfs placed too far from the water",
        "    won't be able to dock ships.",
        "",
        "  - Copy Angle\194\176 is a work in progress:",
        "    placement works but the ghost preview",
        "    doesn't rotate live. Toggle destroy/build",
        "    mode to refresh.",
        "",
        "  - Keybindings can be changed in config.txt:",
        "    Mods/BetterBuildingTools/config.txt",
        "    Restart the game after editing keybinds.",
        "",
        "  - \"Restart All Mods\" may cause fatal errors.",
        "    Full game restart recommended.",
    }, "\n")

    -- ─────────────────────────────────────────────────────────────────────────────
    -- Settings widget — build / refresh / navigate / drill
    -- ─────────────────────────────────────────────────────────────────────────────

    function Settings.Build()
        if Settings.Root and Settings.Root:IsValid() then return end

        local gi = UEHelpers.GetGameInstance()
        if not gi or not gi:IsValid() then return end

        local root      = H.CreateWidget("UserWidget", gi, "BBTSettingsRoot")
        root.WidgetTree = H.CreateWidget("WidgetTree", root, "BBTSettingsTree")
        local canvas    = H.CreateWidget("CanvasPanel", root.WidgetTree, "BBTSettingsCanvas")
        root.WidgetTree.RootWidget = canvas

        local border    = H.CreateWidget("Border", canvas, "BBTSettingsBorder")
        border:SetBrushColor(H.COLOR_BG)
        border:SetPadding({ Left = H.MS(28), Top = H.MS(20), Right = H.MS(28), Bottom = H.MS(20) })

        local vbox      = H.CreateWidget("VerticalBox", border, "BBTSettingsBox")
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
            { text = "BBT SETTINGS", size = H.MS(14), color = H.COLOR_TEAL },
            { text = "v" .. ver,     size = H.MS(12), color = H.COLOR_WHITE },
        }
        if scaleLabel ~= "" then
            headerParts[#headerParts + 1] = { text = scaleLabel, size = H.MS(10), color = H.COLOR_DIM }
        end
        H.MakeHeaderRow(vbox, "Settings", headerParts)
        H.MakeDivider(vbox, "SettingsDiv1")

        Settings.Labels      = {}
        Settings.Values      = {}
        Settings.Hints       = {}
        Settings.TitleText   = nil
        Settings.ContentText = nil
        Settings.PageText    = nil

        -- Content area with minimum height for visual stability
        local contentSize = H.CreateWidget("SizeBox", vbox, "ContentSize")
        contentSize:SetMinDesiredWidth(H.MS(500))
        contentSize:SetMinDesiredHeight(H.MS(MIN_CONTENT_HEIGHT))
        local contentBox = H.CreateWidget("VerticalBox", contentSize, "ContentBox")
        contentSize:SetContent(contentBox)
        vbox:AddChildToVerticalBox(contentSize)

        -- ── HEADERS VIEW ──
        if Settings.ViewMode == "headers" then
            for i, sec in ipairs(SectionDefs) do
                local row = H.CreateWidget("HorizontalBox", contentBox, "SHdr" .. i)

                local labelBox = H.CreateWidget("SizeBox", row, "SHdrLB" .. i)
                labelBox:SetWidthOverride(H.MS(220))
                local label = H.CreateWidget("TextBlock", labelBox, "SHdrL" .. i)
                label.Font.Size = H.MS(13)
                label:SetText(FText("  " .. sec.label))
                label:SetColorAndOpacity(H.COLOR_WHITE)
                labelBox:SetContent(label)
                row:AddChildToHorizontalBox(labelBox)

                local arrow = H.CreateWidget("TextBlock", row, "SHdrA" .. i)
                arrow.Font.Size = H.MS(13)
                arrow:SetText(FText(">>"))
                arrow:SetColorAndOpacity(H.COLOR_DIM)
                row:AddChildToHorizontalBox(arrow)

                contentBox:AddChildToVerticalBox(row):SetPadding({ Left = 0, Top = H.MS(4), Right = 0, Bottom = H.MS(4) })

                Settings.Labels[i] = label
                Settings.Values[i] = arrow
            end

        -- ── SECTION VIEW (settings items) ──
        elseif Settings.ViewMode == "section" then
            local sec = SectionDefs[Settings.DrillIndex]

            local titleText = H.CreateWidget("TextBlock", contentBox, "SBackTitle")
            titleText.Font.Size = H.MS(13)
            titleText:SetText(FText("<< " .. sec.label:upper()))
            titleText:SetColorAndOpacity(sec.cheat and H.COLOR_ORANGE or H.COLOR_TEAL)
            contentBox:AddChildToVerticalBox(titleText):SetPadding({ Left = 0, Top = 0, Right = 0, Bottom = H.MS(2) })
            H.MakeDivider(contentBox, "SSectionDiv")

            if sec.items then
                for i, item in ipairs(sec.items) do
                    local row = H.CreateWidget("HorizontalBox", contentBox, "SRow" .. i)

                    local labelBox = H.CreateWidget("SizeBox", row, "SLabelBox" .. i)
                    labelBox:SetWidthOverride(H.MS(220))
                    local labelTxt = H.CreateWidget("TextBlock", labelBox, "SLabel" .. i)
                    labelTxt.Font.Size = H.MS(12)
                    labelTxt:SetText(FText("  " .. item.label))
                    labelTxt:SetColorAndOpacity(H.COLOR_WHITE)
                    labelBox:SetContent(labelTxt)
                    row:AddChildToHorizontalBox(labelBox)

                    local valBox = H.CreateWidget("SizeBox", row, "SValBox" .. i)
                    valBox:SetWidthOverride(H.MS(55))
                    local valTxt = H.CreateWidget("TextBlock", valBox, "SVal" .. i)
                    valTxt.Font.Size = H.MS(12)
                    valTxt:SetText(FText("---"))
                    valTxt:SetColorAndOpacity(H.COLOR_VALUE_OFF)
                    valBox:SetContent(valTxt)
                    row:AddChildToHorizontalBox(valBox)

                    local hintTxt = H.CreateWidget("TextBlock", row, "SHint" .. i)
                    hintTxt.Font.Size = H.MS(10)
                    hintTxt:SetText(FText(item.hint or ""))
                    hintTxt:SetColorAndOpacity(H.COLOR_HINT)
                    row:AddChildToHorizontalBox(hintTxt)

                    contentBox:AddChildToVerticalBox(row):SetPadding({ Left = 0, Top = H.MS(3), Right = 0, Bottom = H.MS(3) })

                    Settings.Labels[i] = labelTxt
                    Settings.Values[i] = valTxt
                    Settings.Hints[i]  = hintTxt
                end
            end

        -- ── GUIDE VIEW ──
        elseif Settings.ViewMode == "guide" then
            local titleRow = H.CreateWidget("HorizontalBox", contentBox, "GTitle")

            local titleBox = H.CreateWidget("SizeBox", titleRow, "GTitleBox")
            titleBox:SetWidthOverride(H.MS(200))
            local backLabel = H.CreateWidget("TextBlock", titleBox, "GBackLabel")
            backLabel.Font.Size = H.MS(13)
            backLabel:SetText(FText("<< GUIDE"))
            backLabel:SetColorAndOpacity(H.COLOR_TEAL)
            titleBox:SetContent(backLabel)
            titleRow:AddChildToHorizontalBox(titleBox)

            local pageText = H.CreateWidget("TextBlock", titleRow, "GPage")
            pageText.Font.Size = H.MS(12)
            pageText:SetText(FText("PAGE 1/" .. #GuideSections))
            pageText:SetColorAndOpacity(H.COLOR_HINT)
            titleRow:AddChildToHorizontalBox(pageText)
            Settings.PageText = pageText

            contentBox:AddChildToVerticalBox(titleRow):SetPadding({ Left = 0, Top = 0, Right = 0, Bottom = H.MS(2) })
            H.MakeDivider(contentBox, "GDiv")

            local sectionTitle = H.CreateWidget("TextBlock", contentBox, "GSectionTitle")
            sectionTitle.Font.Size = H.MS(13)
            sectionTitle:SetText(FText(GuideSections[1].title))
            sectionTitle:SetColorAndOpacity(H.COLOR_TEAL)
            contentBox:AddChildToVerticalBox(sectionTitle):SetPadding({ Left = 0, Top = 0, Right = 0, Bottom = H.MS(6) })
            Settings.TitleText = sectionTitle

            local gs = GuideSections[Settings.GuideSection]
            local contentBody = gs.text
            if gs.text_fn then contentBody = gs.text_fn() end
            local contentText = H.CreateWidget("TextBlock", contentBox, "GContent")
            contentText.Font.Size = H.MS(10)
            contentText:SetText(FText(contentBody or ""))
            contentText:SetColorAndOpacity(H.COLOR_WHITE)
            contentBox:AddChildToVerticalBox(contentText):SetPadding({ Left = H.MS(4), Top = 0, Right = 0, Bottom = H.MS(8) })
            Settings.ContentText = contentText

        -- ── NOTES VIEW ──
        elseif Settings.ViewMode == "notes" then
            local titleText = H.CreateWidget("TextBlock", contentBox, "NBackTitle")
            titleText.Font.Size = H.MS(13)
            titleText:SetText(FText("<< NOTES"))
            titleText:SetColorAndOpacity(H.COLOR_TEAL)
            contentBox:AddChildToVerticalBox(titleText):SetPadding({ Left = 0, Top = 0, Right = 0, Bottom = H.MS(2) })
            H.MakeDivider(contentBox, "NDiv")

            local notesBody = H.CreateWidget("TextBlock", contentBox, "NBody")
            notesBody.Font.Size = H.MS(9)
            notesBody:SetText(FText(NotesText))
            notesBody:SetColorAndOpacity(H.COLOR_HINT)
            contentBox:AddChildToVerticalBox(notesBody):SetPadding({ Left = H.MS(4), Top = 0, Right = 0, Bottom = H.MS(8) })
        end

        -- ── BOTTOM — legend + nav bar ──

        if Settings.ViewMode == "headers" or Settings.ViewMode == "section" then
            H.MakeDivider(vbox, "SLegendDiv")
            H.MakeTextBlock(vbox, "SLegend", H.MS(9),
                "W = Work in Progress  |  P = Planned  |  D = Disabled",
                H.COLOR_DIM, { Left = H.MS(4), Top = 0, Right = 0, Bottom = H.MS(4) })
        elseif Settings.ViewMode == "guide" then
            H.MakeDivider(vbox, "GLegendDiv")
            H.MakeTextBlock(vbox, "GLegend", H.MS(9),
                "Guide  |  10 pages  |  BetterBuildingTools",
                H.COLOR_DIM, { Left = H.MS(4), Top = 0, Right = 0, Bottom = H.MS(4) })
        elseif Settings.ViewMode == "notes" then
            H.MakeDivider(vbox, "NLegendDiv")
            H.MakeTextBlock(vbox, "NLegend", H.MS(9),
                "Last updated: v0.36",
                H.COLOR_DIM, { Left = H.MS(4), Top = 0, Right = 0, Bottom = H.MS(4) })
        end

        H.MakeDivider(vbox, "SettingsDivBot")

        if Settings.ViewMode == "headers" then
            H.MakeNavBar(vbox, "Settings", {
                { label = "Select", keys = "Up/Down" },
                { label = "Open",   keys = "Right" },
                { label = "Close",  keys = "F2" },
                { label = "Scale",  keys = "PgUp/Dn" },
            })
        elseif Settings.ViewMode == "section" then
            H.MakeNavBar(vbox, "Settings", {
                { label = "Select", keys = "Up/Down" },
                { label = "Change", keys = "Left/Right" },
                { label = "Back",   keys = "F2" },
                { label = "Scale",  keys = "PgUp/Dn" },
            })
        elseif Settings.ViewMode == "guide" then
            H.MakeNavBar(vbox, "Settings", {
                { label = "Page", keys = "Left/Right" },
                { label = "Back", keys = "F2" },
                { label = "Scale", keys = "PgUp/Dn" },
            })
        else
            H.MakeNavBar(vbox, "Settings", {
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

    local function safeWidget(w, fn)
        if not w or not w:IsValid() then return false end
        local ok, _ = pcall(fn, w)
        return ok
    end

    function Settings.Refresh()
        if Settings.ViewMode == "headers" then
            for i = 1, #SectionDefs do
                local lbl = Settings.Labels[i]
                local arr = Settings.Values[i]
                local sel = (i == Settings.Selected)
                safeWidget(lbl, function(w)
                    local prefix = sel and "\194\187 " or "  "
                    w:SetText(FText(prefix .. SectionDefs[i].label))
                    if sel then
                        w:SetColorAndOpacity(H.COLOR_SELECTED)
                    elseif SectionDefs[i].cheat then
                        w:SetColorAndOpacity(H.COLOR_ORANGE)
                    else
                        w:SetColorAndOpacity(H.COLOR_WHITE)
                    end
                end)
                safeWidget(arr, function(w)
                    w:SetColorAndOpacity(sel and H.COLOR_SELECTED or H.COLOR_DIM)
                end)
            end

        elseif Settings.ViewMode == "section" then
            local sec = SectionDefs[Settings.DrillIndex]
            if not sec or not sec.items then return end
            for i, item in ipairs(sec.items) do
                local lbl  = Settings.Labels[i]
                local vlbl = Settings.Values[i]
                local sel  = (i == Settings.Selected)
                safeWidget(lbl, function(w)
                    local prefix = sel and "\194\187 " or "  "
                    w:SetText(FText(prefix .. item.label))
                    if item.type == "status" then
                        w:SetColorAndOpacity(sel and H.COLOR_SELECTED or H.COLOR_DIM)
                    elseif sec.cheat and not sel then
                        w:SetColorAndOpacity(H.COLOR_ORANGE)
                    elseif sel then
                        w:SetColorAndOpacity(H.COLOR_SELECTED)
                    else
                        w:SetColorAndOpacity(H.COLOR_WHITE)
                    end
                end)
                safeWidget(vlbl, function(w)
                    if item.type == "status" then
                        w:SetText(FText(item.status))
                        w:SetColorAndOpacity(H.COLOR_DIM)
                    elseif item.type == "bool" then
                        local val = item.key and BBT_GetConfig and BBT_GetConfig(item.key)
                        w:SetText(FText(val and "ON" or "OFF"))
                        w:SetColorAndOpacity(val and H.COLOR_VALUE_ON or H.COLOR_VALUE_OFF)
                    elseif item.type == "int" then
                        local val = item.key and BBT_GetConfig and BBT_GetConfig(item.key)
                        w:SetText(FText(tostring(val or 0)))
                        w:SetColorAndOpacity(H.COLOR_VALUE_NUM)
                    end
                end)
            end

        elseif Settings.ViewMode == "guide" then
            if not safeWidget(Settings.TitleText, function(w)
                local s = GuideSections[Settings.GuideSection]
                w:SetText(FText(s.title))
            end) then return end
            safeWidget(Settings.PageText, function(w)
                w:SetText(FText("PAGE " .. Settings.GuideSection .. "/" .. #GuideSections))
            end)
            safeWidget(Settings.ContentText, function(w)
                local s = GuideSections[Settings.GuideSection]
                local text = s.text
                if s.text_fn then text = s.text_fn() end
                w:SetText(FText(text or ""))
            end)
        end
    end

    function Settings.Rebuild()
        if Settings.InViewport and Settings.Root and Settings.Root:IsValid() then
            Settings.Root:RemoveFromViewport()
        end
        Settings.Root       = nil
        Settings.InViewport = false
        Settings.Labels     = {}
        Settings.Values     = {}
        Settings.Hints      = {}
        Settings.Build()
        if Settings.Open and Settings.Root and Settings.Root:IsValid() then
            Settings.Root:AddToViewport(950)
            Settings.InViewport = true
        end
        Settings.Refresh()
    end

    function Settings.DrillIn()
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
        Settings.Rebuild()
    end

    function Settings.DrillOut()
        if Settings.ViewMode == "headers" then return end
        local wasAt = Settings.DrillIndex
        Settings.ViewMode = "headers"
        Settings.Selected = wasAt
        Settings.Rebuild()
    end

    function Settings.Nav(delta)
        if not Settings.Open then return end
        if Settings.ViewMode == "headers" then
            local count = #SectionDefs
            Settings.Selected = ((Settings.Selected - 1 + delta) % count) + 1
            Settings.Refresh()
        elseif Settings.ViewMode == "section" then
            local sec = SectionDefs[Settings.DrillIndex]
            if not sec or not sec.items or #sec.items == 0 then return end
            local count = #sec.items
            Settings.Selected = ((Settings.Selected - 1 + delta) % count) + 1
            Settings.Refresh()
        elseif Settings.ViewMode == "guide" then
            local count = #GuideSections
            Settings.GuideSection = ((Settings.GuideSection - 1 + delta) % count) + 1
            Settings.Refresh()
        end
    end

    function Settings.Change(delta)
        if not Settings.Open then return end
        if Settings.ViewMode == "headers" then
            if delta > 0 then Settings.DrillIn() end
            return
        end
        if Settings.ViewMode == "guide" then
            local count = #GuideSections
            Settings.GuideSection = ((Settings.GuideSection - 1 + delta) % count) + 1
            Settings.Refresh()
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
            Settings.Rebuild()
            return
        end
        if item.key == "ui_bstat_scale" then
            local bstat = Settings._bstatRef
            if bstat and bstat.InViewport and bstat.Root and bstat.Root:IsValid() then
                bstat.Root:RemoveFromViewport()
                bstat.InViewport = false
            end
            if bstat then bstat.Root = nil end
        end

        Settings.Refresh()
    end

    function Settings.Toggle()
        Settings.Open = not Settings.Open
        if Settings.Open then
            if not Settings.Root or not Settings.Root:IsValid() then
                Settings.InViewport = false
                Settings.Build()
            end
            if Settings.Root and Settings.Root:IsValid() and not Settings.InViewport then
                Settings.Root:AddToViewport(950)
                Settings.InViewport = true
            end
            Settings.Refresh()
        elseif Settings.InViewport and Settings.Root and Settings.Root:IsValid() then
            Settings.Root:RemoveFromViewport()
            Settings.InViewport = false
        end
    end

    -- ─────────────────────────────────────────────────────────────────────────────
    -- Key handlers
    -- ─────────────────────────────────────────────────────────────────────────────
    function Settings.HandleF2()
        if Settings.Open and Settings.ViewMode ~= "headers" then
            Settings.DrillOut()
        else
            Settings.Toggle()
        end
    end

    function Settings.HandleUp()    Settings.Nav(-1) end
    function Settings.HandleDown()  Settings.Nav(1) end
    function Settings.HandleLeft()  Settings.Change(-1) end
    function Settings.HandleRight() Settings.Change(1) end

    function Settings.HandlePgUp()
        if not Settings.Open then return end
        local scale = (BBT_GetConfig and BBT_GetConfig("ui_menu_scale")) or 100
        scale = math.min(150, scale + 10)
        if BBT_SetConfig then BBT_SetConfig("ui_menu_scale", scale) end
        if BBT_SaveConfig then BBT_SaveConfig() end
        Settings.Rebuild()
    end

    function Settings.HandlePgDn()
        if not Settings.Open then return end
        local scale = (BBT_GetConfig and BBT_GetConfig("ui_menu_scale")) or 100
        scale = math.max(70, scale - 10)
        if BBT_SetConfig then BBT_SetConfig("ui_menu_scale", scale) end
        if BBT_SaveConfig then BBT_SaveConfig() end
        Settings.Rebuild()
    end

    return Settings
end
return create
