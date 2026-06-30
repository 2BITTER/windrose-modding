#include "bbt_common.h"
#include <Unreal/UActorComponent.hpp>
#include <Unreal/Property/FClassProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/Property/FObjectProperty.hpp>
#include <Unreal/Property/FTextProperty.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/Transform.hpp>

// ──────────────────────────────────────────────────────────────────────────────
// UMG Panel Builder (C++ ProcessEvent — Lua widget APIs are broken mid-game)
// Pattern from WindroseTextSigns reference mod (SignTextMod.cpp)
// ──────────────────────────────────────────────────────────────────────────────

static UObject* g_signPanel = nullptr;       // UserWidget root
static UObject* g_signPanelTextBlock = nullptr; // TextBlock for typed text

// Cached UClasses for widget creation
static UClass* g_clsUserWidget  = nullptr;
static UClass* g_clsWidgetTree  = nullptr;
static UClass* g_clsCanvasPanel = nullptr;
static UClass* g_clsBorder      = nullptr;
static UClass* g_clsVerticalBox = nullptr;
static UClass* g_clsTextBlock   = nullptr;

static bool EnsureWidgetClasses()
{
    auto find = [](const wchar_t* path) -> UClass* {
        return reinterpret_cast<UClass*>(UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, path));
    };
    if (!g_clsUserWidget)  g_clsUserWidget  = find(STR("/Script/UMG.UserWidget"));
    if (!g_clsWidgetTree)  g_clsWidgetTree  = find(STR("/Script/UMG.WidgetTree"));
    if (!g_clsCanvasPanel) g_clsCanvasPanel = find(STR("/Script/UMG.CanvasPanel"));
    if (!g_clsBorder)      g_clsBorder      = find(STR("/Script/UMG.Border"));
    if (!g_clsVerticalBox) g_clsVerticalBox = find(STR("/Script/UMG.VerticalBox"));
    if (!g_clsTextBlock)   g_clsTextBlock   = find(STR("/Script/UMG.TextBlock"));
    return g_clsUserWidget && g_clsWidgetTree && g_clsCanvasPanel &&
           g_clsBorder && g_clsVerticalBox && g_clsTextBlock;
}

// Find UFunction by path, cached per pointer
static UFunction* FindFn(const wchar_t* path)
{
    return reinterpret_cast<UFunction*>(UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, path));
}

// AddChild on a PanelWidget — returns the slot (UPanelSlot*)
static UObject* PanelAddChild(UObject* panel, UObject* child)
{
    static UFunction* fn = nullptr;
    if (!fn) fn = FindFn(STR("/Script/UMG.PanelWidget:AddChild"));
    if (!fn || !panel || !child) return nullptr;

    int32_t sz = fn->GetStructureSize();
    if (sz < 32) sz = 64;
    std::vector<uint8_t> params(sz, 0);

    // Set child param
    for (auto* prop : fn->ForEachProperty())
    {
        if (prop->HasAnyPropertyFlags(CPF_ReturnParm)) continue;
        if (prop->GetClass().HashObject() == FObjectProperty::StaticClass().HashObject())
        {
            *reinterpret_cast<UObject**>(params.data() + prop->GetOffset_Internal()) = child;
            break;
        }
    }
    panel->ProcessEvent(fn, params.data());

    // Extract return value (slot)
    for (auto* prop : fn->ForEachProperty())
    {
        if (prop->HasAnyPropertyFlags(CPF_ReturnParm) &&
            prop->GetClass().HashObject() == FObjectProperty::StaticClass().HashObject())
        {
            return *reinterpret_cast<UObject**>(params.data() + prop->GetOffset_Internal());
        }
    }
    return nullptr;
}

// SetContent on Border/ContentWidget
static void SetContent(UObject* container, UObject* child)
{
    static UFunction* fn = nullptr;
    if (!fn) fn = FindFn(STR("/Script/UMG.ContentWidget:SetContent"));
    if (!fn || !container || !child) return;

    int32_t sz = fn->GetStructureSize();
    if (sz < 16) sz = 32;
    std::vector<uint8_t> params(sz, 0);
    for (auto* prop : fn->ForEachProperty())
    {
        if (!prop->HasAnyPropertyFlags(CPF_ReturnParm) &&
            prop->GetClass().HashObject() == FObjectProperty::StaticClass().HashObject())
        {
            *reinterpret_cast<UObject**>(params.data() + prop->GetOffset_Internal()) = child;
            break;
        }
    }
    container->ProcessEvent(fn, params.data());
}

// Generic RGBA setter (works for SetBrushColor, SetColorAndOpacity, etc.)
static void SetRGBA(UObject* obj, const wchar_t* fnPath, float r, float g, float b, float a)
{
    UFunction* fn = FindFn(fnPath);
    if (!fn || !obj) return;

    int32_t sz = fn->GetStructureSize();
    if (sz < 32) sz = 64;
    std::vector<uint8_t> params(sz, 0);
    for (auto* prop : fn->ForEachProperty())
    {
        if (prop->HasAnyPropertyFlags(CPF_ReturnParm)) continue;
        if (prop->GetClass().HashObject() == FStructProperty::StaticClass().HashObject() && prop->GetSize() >= 16)
        {
            float* v = reinterpret_cast<float*>(params.data() + prop->GetOffset_Internal());
            v[0] = r; v[1] = g; v[2] = b; v[3] = a;
            break;
        }
    }
    obj->ProcessEvent(fn, params.data());
}

// SetPadding on Border
static void SetPadding(UObject* border, float l, float t, float r, float b)
{
    static UFunction* fn = nullptr;
    if (!fn) fn = FindFn(STR("/Script/UMG.Border:SetPadding"));
    if (!fn || !border) return;

    int32_t sz = fn->GetStructureSize();
    if (sz < 32) sz = 64;
    std::vector<uint8_t> params(sz, 0);
    for (auto* prop : fn->ForEachProperty())
    {
        if (prop->HasAnyPropertyFlags(CPF_ReturnParm)) continue;
        if (prop->GetClass().HashObject() == FStructProperty::StaticClass().HashObject())
        {
            if (prop->GetSize() == 16)
            {
                float* v = reinterpret_cast<float*>(params.data() + prop->GetOffset_Internal());
                v[0] = l; v[1] = t; v[2] = r; v[3] = b;
            }
            else if (prop->GetSize() == 32)
            {
                double* v = reinterpret_cast<double*>(params.data() + prop->GetOffset_Internal());
                v[0] = l; v[1] = t; v[2] = r; v[3] = b;
            }
            break;
        }
    }
    border->ProcessEvent(fn, params.data());
}

// SetText on TextBlock
static void SetBlockText(UObject* block, const std::wstring& text)
{
    static UFunction* fn = nullptr;
    if (!fn) fn = FindFn(STR("/Script/UMG.TextBlock:SetText"));
    if (!fn || !block) return;

    int32_t sz = fn->GetStructureSize();
    if (sz < 32) sz = 64;
    std::vector<uint8_t> params(sz, 0);
    for (auto* prop : fn->ForEachProperty())
    {
        if (prop->HasAnyPropertyFlags(CPF_ReturnParm)) continue;
        if (prop->GetClass().HashObject() == FTextProperty::StaticClass().HashObject())
        {
            *reinterpret_cast<FText*>(params.data() + prop->GetOffset_Internal()) = FText(text);
            break;
        }
    }
    block->ProcessEvent(fn, params.data());
}

// SetColorAndOpacity on TextBlock (takes FSlateColor: 4 floats + uint8 rule)
static void SetTextColor(UObject* block, float r, float g, float b, float a)
{
    SetRGBA(block, STR("/Script/UMG.TextBlock:SetColorAndOpacity"), r, g, b, a);
}

// Set TextBlock properties directly via memory (Justification + AutoWrapText)
static void SetTextBlockProps(UObject* block, uint8_t justify, bool autoWrap)
{
    if (!block) return;
    for (auto* prop : block->GetClassPrivate()->ForEachProperty())
    {
        auto nm = prop->GetName();
        if (nm == STR("Justification"))
            *reinterpret_cast<uint8_t*>(reinterpret_cast<uint8_t*>(block) + prop->GetOffset_Internal()) = justify;
        else if (nm == STR("AutoWrapText"))
            static_cast<FBoolProperty*>(prop)->SetPropertyValue(
                reinterpret_cast<uint8_t*>(block) + prop->GetOffset_Internal(), autoWrap);
    }
}

// Set font size on TextBlock (direct memory — Font.Size @ 0x1D0 + 0x48 per QM)
static void SetFontSize(UObject* block, int size)
{
    // UTextBlock::Font is at offset 0x1D0, FSlateFontInfo::Size at +0x48 within it
    // But this varies — use the property system instead
    auto* cls = block->GetClassPrivate();
    if (!cls) return;
    for (auto* prop : cls->ForEachProperty())
    {
        if (prop->GetName() == STR("Font"))
        {
            uint8_t* fontPtr = reinterpret_cast<uint8_t*>(block) + prop->GetOffset_Internal();
            // FSlateFontInfo::Size is an int32 near the end of the struct
            // Try offset 0x48 (QM's known offset)
            *reinterpret_cast<int32_t*>(fontPtr + 0x48) = size;
            break;
        }
    }
}

// Slot position/size (FVector2D — 2 floats or 2 doubles)
static void SetSlotVec2(UObject* slot, const wchar_t* fnPath, float x, float y)
{
    UFunction* fn = FindFn(fnPath);
    if (!fn || !slot) return;

    int32_t sz = fn->GetStructureSize();
    if (sz < 32) sz = 32;
    std::vector<uint8_t> params(sz, 0);
    for (auto* prop : fn->ForEachProperty())
    {
        if (prop->HasAnyPropertyFlags(CPF_ReturnParm)) continue;
        if (prop->GetClass().HashObject() == FStructProperty::StaticClass().HashObject())
        {
            if (prop->GetSize() == 8)
            {
                float* v = reinterpret_cast<float*>(params.data() + prop->GetOffset_Internal());
                v[0] = x; v[1] = y;
            }
            else if (prop->GetSize() == 16)
            {
                double* v = reinterpret_cast<double*>(params.data() + prop->GetOffset_Internal());
                v[0] = x; v[1] = y;
            }
            break;
        }
    }
    slot->ProcessEvent(fn, params.data());
}

// AddToViewport
static void AddToViewport(UObject* widget, int32_t zOrder)
{
    static UFunction* fn = nullptr;
    if (!fn) fn = FindFn(STR("/Script/UMG.UserWidget:AddToViewport"));
    if (!fn || !widget) return;

    int32_t sz = fn->GetStructureSize();
    if (sz < 8) sz = 8;
    std::vector<uint8_t> params(sz, 0);
    for (auto* prop : fn->ForEachProperty())
    {
        if (!prop->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            *reinterpret_cast<int32_t*>(params.data() + prop->GetOffset_Internal()) = zOrder;
            break;
        }
    }
    widget->ProcessEvent(fn, params.data());
}

// RemoveFromParent
static void RemoveFromParent(UObject* widget)
{
    if (!widget) return;
    UFunction* fn = widget->GetFunctionByNameInChain(STR("RemoveFromParent"));
    if (fn) widget->ProcessEvent(fn, nullptr);
}

// ──────────────────────────────────────────────────────────────────────────────
// Build the sign edit panel — all in C++
// ──────────────────────────────────────────────────────────────────────────────
void BuildSignPanel()
{
    if (g_signPanel && IsObjectValid(g_signPanel)) return; // already built

    if (!EnsureWidgetClasses())
    {
        Output::send<LogLevel::Warning>(STR("[TextSigns] Widget classes not found\n"));
        return;
    }

    UObject* pc = GetPlayerController();
    if (!pc) return;

    // Create UserWidget owned by PlayerController (same as reference mod)
    auto* widget = UObjectGlobals::NewObject<UObject>(pc, g_clsUserWidget, NAME_None, RF_Transient);
    if (!widget) { Output::send<LogLevel::Warning>(STR("[TextSigns] Failed to create UserWidget\n")); return; }

    // Create WidgetTree
    auto* tree = UObjectGlobals::NewObject<UObject>(widget, g_clsWidgetTree,
        FName(STR("BBTSignTree"), FNAME_Add), RF_Transient);
    if (!tree) { Output::send<LogLevel::Warning>(STR("[TextSigns] Failed to create WidgetTree\n")); return; }

    // Set WidgetTree property on UserWidget
    for (auto* prop : widget->GetClassPrivate()->ForEachProperty())
    {
        if (prop->GetName() == STR("WidgetTree"))
        {
            *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(widget) + prop->GetOffset_Internal()) = tree;
            break;
        }
    }

    // Create widgets
    auto* canvas = UObjectGlobals::NewObject<UObject>(tree, g_clsCanvasPanel, FName(STR("BBTSignCanvas"), FNAME_Add), RF_Transient);
    auto* border = UObjectGlobals::NewObject<UObject>(tree, g_clsBorder, FName(STR("BBTSignBorder"), FNAME_Add), RF_Transient);
    auto* vbox   = UObjectGlobals::NewObject<UObject>(tree, g_clsVerticalBox, FName(STR("BBTSignVBox"), FNAME_Add), RF_Transient);
    auto* title  = UObjectGlobals::NewObject<UObject>(tree, g_clsTextBlock, FName(STR("BBTSignTitle"), FNAME_Add), RF_Transient);
    auto* text   = UObjectGlobals::NewObject<UObject>(tree, g_clsTextBlock, FName(STR("BBTSignText"), FNAME_Add), RF_Transient);
    auto* hint   = UObjectGlobals::NewObject<UObject>(tree, g_clsTextBlock, FName(STR("BBTSignHint"), FNAME_Add), RF_Transient);

    if (!canvas || !border || !vbox || !title || !text || !hint)
    {
        Output::send<LogLevel::Warning>(STR("[TextSigns] Failed to create widget children\n"));
        return;
    }

    // Set root widget
    for (auto* prop : tree->GetClassPrivate()->ForEachProperty())
    {
        if (prop->GetName() == STR("RootWidget"))
        {
            *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(tree) + prop->GetOffset_Internal()) = canvas;
            break;
        }
    }

    // Add Border to Canvas — returns slot for positioning
    auto* slot = PanelAddChild(canvas, border);

    // Get viewport size for dynamic scaling
    int32_t vpW = 1920, vpH = 1080;
    UFunction* vpFn = pc->GetFunctionByNameInChain(STR("GetViewportSize"));
    if (vpFn)
    {
        struct { int32_t X, Y; } vpSize{};
        pc->ProcessEvent(vpFn, &vpSize);
        if (vpSize.X > 0) vpW = vpSize.X;
        if (vpSize.Y > 0) vpH = vpSize.Y;
    }
    float scale = static_cast<float>(vpW) / 1920.0f;

    // Scale factor and auto-size
    if (slot)
    {
        UFunction* autoSizeFn = slot->GetFunctionByNameInChain(STR("SetAutoSize"));
        if (autoSizeFn)
        {
            bool autoSize = true;
            slot->ProcessEvent(autoSizeFn, &autoSize);
        }
        // Center on screen
        SetSlotVec2(slot, STR("/Script/UMG.CanvasPanelSlot:SetPosition"),
            static_cast<float>(vpW) / 2.0f - 300.0f * scale,
            static_cast<float>(vpH) / 2.0f - 50.0f * scale);
    }

    // Style the border
    SetRGBA(border, STR("/Script/UMG.Border:SetBrushColor"), 0.02f, 0.02f, 0.03f, 0.92f);
    SetPadding(border, 10.0f, 8.0f, 10.0f, 8.0f);

    // Border → VBox
    SetContent(border, vbox);

    // Add TextBlocks to VBox — set HAlign to Fill via property so centering works
    auto setSlotFill = [](UObject* slot) {
        if (!slot) return;
        for (auto* prop : slot->GetClassPrivate()->ForEachProperty())
        {
            if (prop->GetName() == STR("HorizontalAlignment"))
            {
                *reinterpret_cast<uint8_t*>(reinterpret_cast<uint8_t*>(slot) + prop->GetOffset_Internal()) = 0; // Fill
                break;
            }
        }
    };
    setSlotFill(PanelAddChild(vbox, title));
    setSlotFill(PanelAddChild(vbox, text));
    setSlotFill(PanelAddChild(vbox, hint));

    // Scale entire panel as one unit for resolution
    SetSlotVec2(border, STR("/Script/UMG.Widget:SetRenderScale"), scale * 0.35f, scale * 0.35f);

    SetBlockText(title, STR("EDIT SIGN TEXT"));
    SetTextColor(title, 0.4f, 0.8f, 1.0f, 1.0f);
    SetSlotVec2(title, STR("/Script/UMG.Widget:SetRenderScale"), 0.85f, 0.85f);
    SetTextBlockProps(title, 1, true);

    SetBlockText(text, STR("_"));
    SetTextColor(text, 1.0f, 1.0f, 1.0f, 1.0f);
    SetTextBlockProps(text, 1, true);

    SetBlockText(hint, STR("Enter = Apply  |  Esc = Cancel  |  Backspace = Delete"));
    SetTextColor(hint, 0.5f, 0.5f, 0.5f, 1.0f);
    SetSlotVec2(hint, STR("/Script/UMG.Widget:SetRenderScale"), 0.65f, 0.65f);
    SetTextBlockProps(hint, 1, true);

    // Show
    AddToViewport(widget, 50);

    g_signPanel = widget;
    g_signPanelTextBlock = text;
    Output::send<LogLevel::Verbose>(STR("[TextSigns] C++ sign panel built + shown\n"));
}

void DestroySignPanel()
{
    if (g_signPanel && IsObjectValid(g_signPanel))
    {
        RemoveFromParent(g_signPanel);
    }
    g_signPanel = nullptr;
    g_signPanelTextBlock = nullptr;
}

// UpdateSignPanelText defined below (needs g_signTextBuf)

// ──────────────────────────────────────────────────────────────────────────────
// TextSigns — custom text on wooden label signs
// ──────────────────────────────────────────────────────────────────────────────

// Sign data saved to JSON
struct SignEntry {
    double x, y, z;
    std::string text;
};
static std::vector<SignEntry> g_savedSigns;
static std::mutex g_signMutex;

// Edit state (non-static — accessed by bridge)
std::atomic<bool> g_reqEditSign{false};
std::atomic<bool> g_reqConfirmSign{false};
std::atomic<bool> g_reqCancelSign{false};
static UObject* g_signEditActor  = nullptr;
bool     g_signEditActive = false;
static constexpr int SIGN_MAX_CHARS = 20;
char     g_signTextBuf[SIGN_MAX_CHARS + 1] = {};
std::string g_signEditStatus;

void UpdateSignPanelText()
{
    if (!g_signPanelTextBlock || !IsObjectValid(g_signPanelTextBlock)) return;
    std::wstring display(g_signTextBuf, g_signTextBuf + std::strlen(g_signTextBuf));
    display += L"_";
    SetBlockText(g_signPanelTextBlock, display);
}

// ──────────────────────────────────────────────────────────────────────────────
// In-game keyboard capture (WH_KEYBOARD_LL)
// ──────────────────────────────────────────────────────────────────────────────
static HHOOK g_keyboardHook = nullptr;
static std::atomic<bool> g_keyboardCaptureActive{false};

static LRESULT CALLBACK SignKeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code >= 0 && g_keyboardCaptureActive.load() && g_signEditActive)
    {
        auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
        {
            DWORD vk = kb->vkCode;

            // Enter = confirm (flag only — processed on game thread)
            if (vk == VK_RETURN)
            {
                g_reqConfirmSign.store(true);
                return 1; // consume
            }
            // Escape = cancel (flag only — processed on game thread)
            if (vk == VK_ESCAPE)
            {
                g_reqCancelSign.store(true);
                return 1;
            }
            // Backspace
            if (vk == VK_BACK)
            {
                size_t len = std::strlen(g_signTextBuf);
                if (len > 0) g_signTextBuf[len - 1] = '\0';
                return 1;
            }
            // Character input: A-Z, 0-9, space, common punctuation
            char ch = 0;
            if (vk >= 'A' && vk <= 'Z')
            {
                bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                ch = shift ? static_cast<char>(vk) : static_cast<char>(vk + 32);
            }
            else if (vk >= '0' && vk <= '9')
            {
                bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                if (!shift) ch = static_cast<char>(vk);
                else
                {
                    const char shifted[] = ")!@#$%^&*(";
                    ch = shifted[vk - '0'];
                }
            }
            else if (vk == VK_SPACE) ch = ' ';
            else if (vk == VK_OEM_MINUS) ch = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? '_' : '-';
            else if (vk == VK_OEM_PERIOD) ch = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? '>' : '.';
            else if (vk == VK_OEM_COMMA) ch = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? '<' : ',';
            else if (vk == VK_OEM_1) ch = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? ':' : ';';
            else if (vk == VK_OEM_7) ch = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? '"' : '\'';
            else if (vk == VK_OEM_2) ch = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? '?' : '/';
            else if (vk == VK_OEM_PLUS) ch = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? '+' : '=';

            if (ch != 0)
            {
                size_t len = std::strlen(g_signTextBuf);
                if (len < SIGN_MAX_CHARS)
                {
                    g_signTextBuf[len] = ch;
                    g_signTextBuf[len + 1] = '\0';
                }
                return 1; // consume — don't pass to game
            }
        }
    }
    return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
}

static std::thread g_hookThread;
static std::atomic<bool> g_hookThreadStop{false};

void InstallSignKeyboardHook()
{
    if (g_keyboardHook) return;
    g_hookThreadStop.store(false);
    g_hookThread = std::thread([]() {
        g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, SignKeyboardProc, nullptr, 0);
        if (!g_keyboardHook)
        {
            Output::send<LogLevel::Warning>(STR("[TextSigns] Failed to install keyboard hook\n"));
            return;
        }
        Output::send<LogLevel::Verbose>(STR("[TextSigns] Keyboard hook installed\n"));
        MSG msg;
        while (!g_hookThreadStop.load())
        {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            Sleep(1);
        }
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
        Output::send<LogLevel::Verbose>(STR("[TextSigns] Keyboard hook uninstalled\n"));
    });
}

void UninstallSignKeyboardHook()
{
    g_hookThreadStop.store(true);
    if (g_hookThread.joinable()) g_hookThread.join();
}

// Cached UClass/UFunction (resolved once)
static UClass*    g_textRenderClass   = nullptr;
static UFunction* g_addComponentFn    = nullptr;
static UFunction* g_setTextFn         = nullptr;
static UFunction* g_setWorldSizeFn    = nullptr;
static UFunction* g_setHAlignFn       = nullptr;
static UFunction* g_setVAlignFn       = nullptr;
static UFunction* g_setColorFn        = nullptr;
static UFunction* g_setHiddenFn       = nullptr;

// ──────────────────────────────────────────────────────────────────────────────
// Cache resolution
// ──────────────────────────────────────────────────────────────────────────────
static bool EnsureTextSignCaches()
{
    if (!g_textRenderClass)
    {
        g_textRenderClass = reinterpret_cast<UClass*>(UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/Engine.TextRenderComponent")));
    }
    if (!g_textRenderClass) return false;

    if (!g_setTextFn)
        g_setTextFn = reinterpret_cast<UFunction*>(UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/Engine.TextRenderComponent:K2_SetText")));
    if (!g_setTextFn)
        g_setTextFn = reinterpret_cast<UFunction*>(UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/Engine.TextRenderComponent:SetText")));

    if (!g_setWorldSizeFn)
        g_setWorldSizeFn = reinterpret_cast<UFunction*>(UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/Engine.TextRenderComponent:SetWorldSize")));

    if (!g_setHAlignFn)
        g_setHAlignFn = reinterpret_cast<UFunction*>(UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/Engine.TextRenderComponent:SetHorizontalAlignment")));

    if (!g_setVAlignFn)
        g_setVAlignFn = reinterpret_cast<UFunction*>(UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/Engine.TextRenderComponent:SetVerticalAlignment")));

    if (!g_setColorFn)
        g_setColorFn = reinterpret_cast<UFunction*>(UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/Engine.TextRenderComponent:SetTextRenderColor")));

    return g_setTextFn != nullptr;
}

// ──────────────────────────────────────────────────────────────────────────────
// Sign detection
// ──────────────────────────────────────────────────────────────────────────────
static bool IsSignItem(UObject* item)
{
    if (!item) return false;
    auto name = item->GetName();
    return name.find(STR("Lables_Wooden")) != std::wstring::npos;
}

// Get actor world position (K2_GetActorLocation)
static bool GetActorLocation(UObject* actor, double& outX, double& outY, double& outZ)
{
    static UFunction* fn = nullptr;
    if (!fn)
    {
        fn = reinterpret_cast<UFunction*>(UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/Engine.Actor:K2_GetActorLocation")));
    }
    if (!fn) return false;

    struct { double X, Y, Z; } result{};
    actor->ProcessEvent(fn, &result);
    outX = result.X; outY = result.Y; outZ = result.Z;
    return true;
}

// Distance-based sign matching (tolerance for floating-point drift between sessions)
static constexpr double SIGN_MATCH_DIST = 10.0;
static int FindSignEntry(double x, double y, double z)
{
    for (size_t i = 0; i < g_savedSigns.size(); ++i)
    {
        double dx = g_savedSigns[i].x - x;
        double dy = g_savedSigns[i].y - y;
        double dz = g_savedSigns[i].z - z;
        if (dx*dx + dy*dy + dz*dz < SIGN_MATCH_DIST * SIGN_MATCH_DIST)
            return static_cast<int>(i);
    }
    return -1;
}

// ──────────────────────────────────────────────────────────────────────────────
// Find existing TextRenderComponent on an actor (our previously created one)
// ──────────────────────────────────────────────────────────────────────────────
static UObject* FindTextComponent(UObject* actor)
{
    auto* a = static_cast<AActor*>(actor);
    auto components = a->GetComponentsByClass(UActorComponent::StaticClass());
    for (int32_t i = 0; i < components.Num(); ++i)
    {
        auto* comp = components[i];
        if (!comp || !IsObjectValid(comp)) continue;
        auto className = comp->GetClassPrivate()->GetName();
        if (className.find(STR("TextRenderComponent")) != std::wstring::npos)
            return comp;
    }
    return nullptr;
}

// ──────────────────────────────────────────────────────────────────────────────
// Hide the decal plane (icon image) on a sign actor
// The sign has 2 mesh components: the wooden frame and a Plane with the icon.
// We hide any StaticMeshComponent whose mesh name contains "Plane".
// ──────────────────────────────────────────────────────────────────────────────
static void HideSignDecal(UObject* actor)
{
    static UFunction* setVisibleFn = nullptr;
    if (!setVisibleFn)
        setVisibleFn = reinterpret_cast<UFunction*>(UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/Engine.SceneComponent:SetVisibility")));
    if (!setVisibleFn) return;

    auto* a = static_cast<AActor*>(actor);
    auto components = a->GetComponentsByClass(UActorComponent::StaticClass());

    int meshIndex = 0;
    for (int32_t i = 0; i < components.Num(); ++i)
    {
        auto* comp = components[i];
        if (!comp || !IsObjectValid(comp)) continue;
        auto className = comp->GetClassPrivate()->GetName();
        if (className.find(STR("StaticMeshComponent")) == std::wstring::npos) continue;

        auto compName = comp->GetName();
        Output::send<LogLevel::Verbose>(STR("[TextSigns] Mesh component [{}]: {}\n"),
            meshIndex, compName);

        // The decal plane component is named "Plane" — hide it
        if (compName.find(STR("Plane")) != std::wstring::npos)
        {
            struct { bool visible; bool propagate; } params{false, true};
            comp->ProcessEvent(setVisibleFn, &params);
            Output::send<LogLevel::Verbose>(STR("[TextSigns] Hidden decal plane: {}\n"), compName);
        }
        meshIndex++;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Create TextRenderComponent on a sign actor via AddComponentByClass
// ──────────────────────────────────────────────────────────────────────────────
static UObject* CreateTextComponent(UObject* actor)
{
    if (!EnsureTextSignCaches()) return nullptr;

    if (!g_addComponentFn)
    {
        g_addComponentFn = reinterpret_cast<UFunction*>(UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/Engine.Actor:AddComponentByClass")));
    }
    if (!g_addComponentFn)
    {
        Output::send<LogLevel::Warning>(STR("[TextSigns] AddComponentByClass not found\n"));
        return nullptr;
    }

    int32_t paramSize = g_addComponentFn->GetStructureSize();
    if (paramSize < 64) paramSize = 256;
    std::vector<uint8_t> params(paramSize, 0);

    for (auto* prop : g_addComponentFn->ForEachProperty())
    {
        auto nm = prop->GetName();
        int32_t off = prop->GetOffset_Internal();

        if (prop->GetClass().HashObject() == FClassProperty::StaticClass().HashObject())
        {
            *reinterpret_cast<UClass**>(params.data() + off) = g_textRenderClass;
        }
        else if (prop->GetClass().HashObject() == FBoolProperty::StaticClass().HashObject())
        {
            auto nmLower = nm;
            std::transform(nmLower.begin(), nmLower.end(), nmLower.begin(), ::towlower);
            if (nmLower.find(STR("manualattachment")) != std::wstring::npos ||
                nmLower.find(STR("deferredfinish")) != std::wstring::npos)
            {
                auto* bp = static_cast<FBoolProperty*>(prop);
                bp->SetPropertyValue(params.data() + off, false);
            }
        }
        else if (prop->GetClass().HashObject() == FStructProperty::StaticClass().HashObject())
        {
            auto nmLower = nm;
            std::transform(nmLower.begin(), nmLower.end(), nmLower.begin(), ::towlower);
            if (nmLower.find(STR("relativetransform")) != std::wstring::npos &&
                prop->GetSize() >= static_cast<int32_t>(sizeof(FTransform)))
            {
                auto* t = reinterpret_cast<FTransform*>(params.data() + off);
                *t = FTransform(
                    FQuat(0.0, 0.0, 0.0, 1.0),
                    FVector(12.0, 0.0, 1.5),
                    FVector(1.0, 1.0, 1.0));
            }
        }
    }

    actor->ProcessEvent(g_addComponentFn, params.data());

    UObject* created = nullptr;
    for (auto* prop : g_addComponentFn->ForEachProperty())
    {
        if (prop->HasAnyPropertyFlags(CPF_ReturnParm) &&
            prop->GetClass().HashObject() == FObjectProperty::StaticClass().HashObject())
        {
            created = *reinterpret_cast<UObject**>(params.data() + prop->GetOffset_Internal());
            break;
        }
    }

    if (!created)
    {
        Output::send<LogLevel::Warning>(STR("[TextSigns] AddComponentByClass returned null\n"));
        return nullptr;
    }

    // Set relative location explicitly after creation
    static UFunction* setLocFn = nullptr;
    if (!setLocFn)
        setLocFn = reinterpret_cast<UFunction*>(UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/Engine.SceneComponent:K2_SetRelativeLocation")));
    if (setLocFn)
    {
        struct { double X, Y, Z; bool sweep; } locParams{12.0, 0.0, 1.5, false};
        created->ProcessEvent(setLocFn, &locParams);
    }

    // Configure: center-aligned, reasonable size
    if (g_setWorldSizeFn)
    {
        float worldSize = 14.0f;
        created->ProcessEvent(g_setWorldSizeFn, &worldSize);
    }
    if (g_setHAlignFn)
    {
        uint8_t align = 1; // Center
        created->ProcessEvent(g_setHAlignFn, &align);
    }
    if (g_setVAlignFn)
    {
        uint8_t align = 1; // Center
        created->ProcessEvent(g_setVAlignFn, &align);
    }
    if (g_setColorFn)
    {
        struct { float R, G, B, A; } color{0.92f, 0.89f, 0.84f, 1.0f};
        created->ProcessEvent(g_setColorFn, &color);
    }

    Output::send<LogLevel::Verbose>(STR("[TextSigns] TextRenderComponent created on {}\n"), actor->GetName());
    return created;
}

// ──────────────────────────────────────────────────────────────────────────────
// Apply text to a TextRenderComponent
// ──────────────────────────────────────────────────────────────────────────────
static bool SetComponentText(UObject* comp, const std::string& text)
{
    if (!comp || !g_setTextFn) return false;

    // K2_SetText takes FText param
    // Build param buffer, find the FText property and set it
    int32_t paramSize = g_setTextFn->GetStructureSize();
    if (paramSize < 64) paramSize = 256;
    std::vector<uint8_t> params(paramSize, 0);

    for (auto* prop : g_setTextFn->ForEachProperty())
    {
        if (prop->HasAnyPropertyFlags(CPF_Parm) && !prop->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            // Convert text to wide string for FText
            std::wstring wide(text.begin(), text.end());
            auto* textPtr = reinterpret_cast<FText*>(params.data() + prop->GetOffset_Internal());
            *textPtr = FText(wide);
            break;
        }
    }

    comp->ProcessEvent(g_setTextFn, params.data());
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// JSON save/load (simple manual format — no library dependency)
// ──────────────────────────────────────────────────────────────────────────────
static std::wstring SignsFilePath()
{
    return BBT_ModFolder() + L"\\signs.txt";
}

void TextSignsSave()
{
    std::lock_guard<std::mutex> lock(g_signMutex);
    std::ofstream out(SignsFilePath());
    if (!out.good()) return;

    out << "# BBT TextSigns data — do not edit while game is running\n";
    for (auto& s : g_savedSigns)
    {
        // Escape newlines in text
        std::string escaped = s.text;
        for (size_t i = 0; i < escaped.size(); ++i)
        {
            if (escaped[i] == '\n') { escaped.replace(i, 1, "\\n"); ++i; }
            else if (escaped[i] == '\\') { escaped.replace(i, 1, "\\\\"); ++i; }
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f,%.1f,%.1f", s.x, s.y, s.z);
        out << "sign=" << buf << "|" << escaped << "\n";
    }
    Output::send<LogLevel::Verbose>(STR("[TextSigns] Saved {} sign(s)\n"), g_savedSigns.size());
}

void TextSignsLoad()
{
    std::lock_guard<std::mutex> lock(g_signMutex);
    g_savedSigns.clear();

    std::ifstream in(SignsFilePath());
    if (!in.good()) return;

    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("sign=", 0) != 0) continue;

        std::string data = line.substr(5);
        size_t pipe = data.find('|');
        if (pipe == std::string::npos) continue;

        std::string coords = data.substr(0, pipe);
        std::string text = data.substr(pipe + 1);

        // Unescape
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '\\' && i + 1 < text.size())
            {
                if (text[i + 1] == 'n') { text.replace(i, 2, "\n"); }
                else if (text[i + 1] == '\\') { text.replace(i, 2, "\\"); }
            }
        }

        SignEntry e{};
        if (std::sscanf(coords.c_str(), "%lf,%lf,%lf", &e.x, &e.y, &e.z) == 3)
        {
            e.text = text;
            g_savedSigns.push_back(e);
        }
    }
    Output::send<LogLevel::Verbose>(STR("[TextSigns] Loaded {} sign(s)\n"), g_savedSigns.size());
}

// ──────────────────────────────────────────────────────────────────────────────
// Apply text to a specific sign actor (create or update TextRenderComponent)
// ──────────────────────────────────────────────────────────────────────────────
static bool ApplyTextToSign(UObject* actor, const std::string& text)
{
    if (!actor || !IsObjectValid(actor)) return false;
    if (!EnsureTextSignCaches()) return false;

    // Hide the icon decal so text is visible
    HideSignDecal(actor);

    UObject* textComp = FindTextComponent(actor);
    if (!textComp)
        textComp = CreateTextComponent(actor);
    if (!textComp) return false;

    SetComponentText(textComp, text);

    // Make sure it's visible
    if (!g_setHiddenFn)
        g_setHiddenFn = reinterpret_cast<UFunction*>(UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/Engine.SceneComponent:SetHiddenInGame")));
    if (g_setHiddenFn)
    {
        bool hidden = false;
        textComp->ProcessEvent(g_setHiddenFn, &hidden);
    }

    Output::send<LogLevel::Verbose>(STR("[TextSigns] Applied text to {}\n"), actor->GetName());
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Apply all saved signs to world (call once after world load)
// ──────────────────────────────────────────────────────────────────────────────
void TextSignsApplyAll()
{
    std::lock_guard<std::mutex> lock(g_signMutex);
    if (g_savedSigns.empty()) return;
    if (!EnsureTextSignCaches()) return;

    UClass* blockClass = GetBlockClass();
    if (!blockClass) return;

    std::vector<UObject*> blocks;
    UObjectGlobals::FindAllOf(STR("R5BuildingBlock"), blocks);

    int applied = 0;
    for (auto* block : blocks)
    {
        if (!block || !IsObjectValid(block)) continue;

        UObject* item = *reinterpret_cast<UObject**>(
            reinterpret_cast<uint8_t*>(block) + 0x0328);
        if (!IsSignItem(item)) continue;

        double bx, by, bz;
        if (!GetActorLocation(block, bx, by, bz)) continue;

        int idx = FindSignEntry(bx, by, bz);
        if (idx >= 0)
        {
            if (ApplyTextToSign(block, g_savedSigns[idx].text))
                applied++;
        }
    }
    Output::send<LogLevel::Verbose>(STR("[TextSigns] Applied {}/{} saved signs to world\n"), applied, g_savedSigns.size());
}

// ──────────────────────────────────────────────────────────────────────────────
// Save or update a sign entry
// ──────────────────────────────────────────────────────────────────────────────
static void SaveSignEntry(double x, double y, double z, const std::string& text)
{
    std::lock_guard<std::mutex> lock(g_signMutex);

    // Remove ALL matching entries first (cleans up old duplicates)
    auto matchPos = [&](const SignEntry& e) {
        double dx = e.x - x, dy = e.y - y, dz = e.z - z;
        return dx*dx + dy*dy + dz*dz < SIGN_MATCH_DIST * SIGN_MATCH_DIST;
    };
    g_savedSigns.erase(
        std::remove_if(g_savedSigns.begin(), g_savedSigns.end(), matchPos),
        g_savedSigns.end());

    // Add new entry (unless clearing text)
    if (!text.empty())
        g_savedSigns.push_back({x, y, z, text});
}

// ──────────────────────────────────────────────────────────────────────────────
// Main edit trigger — called from game thread when Shift+T pressed
// ──────────────────────────────────────────────────────────────────────────────
void TryEditSign()
{
    UObject* pc = GetPlayerController();
    if (!IsObjectValid(pc)) return;

    UFunction* vpFn = pc->GetFunctionByNameInChain(STR("GetPlayerViewPoint"));
    if (!vpFn) return;

    GetViewPointParams vp{};
    pc->ProcessEvent(vpFn, &vp);

    double pitch = vp.RotPitch * EYE_PI / 180.0;
    double yaw   = vp.RotYaw   * EYE_PI / 180.0;
    double cp    = std::cos(pitch);
    double dirX  = cp * std::cos(yaw);
    double dirY  = cp * std::sin(yaw);
    double dirZ  = std::sin(pitch);

    UObject* block = TraceForBlock(pc, vp.LocX, vp.LocY, vp.LocZ, dirX, dirY, dirZ);
    if (!block)
    {
        g_signEditStatus = "No block under crosshair";
        Output::send<LogLevel::Verbose>(STR("[TextSigns] No block hit\n"));
        return;
    }

    UObject* item = *reinterpret_cast<UObject**>(
        reinterpret_cast<uint8_t*>(block) + 0x0328);
    if (!IsSignItem(item))
    {
        g_signEditStatus = "Not a sign block";
        Output::send<LogLevel::Verbose>(STR("[TextSigns] Hit block is not a sign: {}\n"),
            item ? item->GetName() : STR("null"));
        return;
    }

    InstallSignKeyboardHook(); // lazy install on first use
    g_signEditActor  = block;
    g_signEditActive = true;
    g_signTextBuf[0] = '\0';

    // Pre-fill with existing text if we have one saved
    double bx, by, bz;
    if (GetActorLocation(block, bx, by, bz))
    {
        std::lock_guard<std::mutex> lock(g_signMutex);
        int idx = FindSignEntry(bx, by, bz);
        if (idx >= 0)
            std::strncpy(g_signTextBuf, g_savedSigns[idx].text.c_str(), sizeof(g_signTextBuf) - 1);
    }

    g_keyboardCaptureActive.store(true);
    BuildSignPanel();
    g_signEditStatus = "Editing sign — type then press Enter";
    Output::send<LogLevel::Verbose>(STR("[TextSigns] Sign edit started on {} — keyboard capture active\n"), block->GetName());
}

// ──────────────────────────────────────────────────────────────────────────────
// Confirm edit — apply text + save
// ──────────────────────────────────────────────────────────────────────────────
static void ConfirmSignEdit()
{
    if (!g_signEditActor || !IsObjectValid(g_signEditActor))
    {
        g_signEditStatus = "Sign no longer valid";
        g_signEditActive = false;
        return;
    }

    std::string text(g_signTextBuf);

    double bx, by, bz;
    if (!GetActorLocation(g_signEditActor, bx, by, bz))
    {
        g_signEditStatus = "Could not get sign position";
        g_signEditActive = false;
        return;
    }

    if (ApplyTextToSign(g_signEditActor, text))
    {
        SaveSignEntry(bx, by, bz, text);
        TextSignsSave();
        g_signEditStatus = text.empty() ? "Sign text cleared" : "Sign text applied!";
    }
    else
    {
        g_signEditStatus = "Failed to apply text";
    }

    g_signEditActor  = nullptr;
    g_signEditActive = false;
    g_keyboardCaptureActive.store(false);
    DestroySignPanel();
}

void ConfirmSignEditFromLua()
{
    ConfirmSignEdit();
}

// Called from game thread tick to process keyboard hook flags safely
void TextSignsProcessFlags()
{
    if (g_reqConfirmSign.exchange(false))
    {
        ConfirmSignEdit();
    }
    if (g_reqCancelSign.exchange(false))
    {
        g_signEditActor  = nullptr;
        g_signEditActive = false;
        g_keyboardCaptureActive.store(false);
        g_signEditStatus = "Cancelled";
        DestroySignPanel();
    }
    if (g_signEditActive)
    {
        UpdateSignPanelText();
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// ImGui rendering (called from register_tab callback in dllmain)
// ──────────────────────────────────────────────────────────────────────────────
void TextSignsImGui()
{
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "TextSigns");

    if (!g_signEditActive)
    {
        ImGui::TextWrapped("Aim at a wooden label sign and press Shift+T to edit its text.");
        if (!g_signEditStatus.empty())
            ImGui::TextDisabled("%s", g_signEditStatus.c_str());

        {
            std::lock_guard<std::mutex> lock(g_signMutex);
            ImGui::Text("Saved signs: %d", static_cast<int>(g_savedSigns.size()));
        }
    }
    else
    {
        ImGui::TextWrapped("Editing sign — type your text below:");
        ImGui::SetNextItemWidth(-1);
        bool enter = ImGui::InputText("##signtext", g_signTextBuf, sizeof(g_signTextBuf),
                                       ImGuiInputTextFlags_EnterReturnsTrue);
        int len = static_cast<int>(std::strlen(g_signTextBuf));
        ImGui::Text("%d / %d", len, SIGN_MAX_CHARS);
        if (enter || ImGui::Button("Apply"))
        {
            ConfirmSignEdit();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            g_signEditActive = false;
            g_signEditActor  = nullptr;
            g_signEditStatus = "Cancelled";
            g_keyboardCaptureActive.store(false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Sign"))
        {
            g_signTextBuf[0] = '\0';
            ConfirmSignEdit();
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Cleanup on unload
// ──────────────────────────────────────────────────────────────────────────────
void TextSignsCleanup()
{
    g_keyboardCaptureActive.store(false);
    UninstallSignKeyboardHook();
    g_signEditActor  = nullptr;
    g_signEditActive = false;
    g_signTextBuf[0] = '\0';
    g_signEditStatus.clear();
    {
        std::lock_guard<std::mutex> lock(g_signMutex);
        g_savedSigns.clear();
    }
}
