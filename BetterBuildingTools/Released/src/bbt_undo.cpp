#include "bbt_common.h"

// ──────────────────────────────────────────────────────────────────────────────
// Undo system — file-local state
// ──────────────────────────────────────────────────────────────────────────────

// FGameplayTag = FName wrapper (8 bytes in shipping).
// FGameplayEventData is 0xB0 bytes (from GameplayAbilities.hpp dump):
//   EventTag(0x00) Instigator(0x08) Target(0x10) OptionalObject(0x18)
//   OptionalObject2(0x20) ContextHandle(0x28) InstigatorTags(0x40)
//   TargetTags(0x60) EventMagnitude(0x80) TargetData(0x88)  Size: 0xB0
//
// SendGameplayEventToActor(AActor* Actor, FGameplayTag EventTag, FGameplayEventData Payload):
//   params layout:  Actor(0x00) EventTag(0x08) Payload(0x10 .. 0xC0)
#pragma pack(push, 1)
struct SendGameplayEventParams
{
    AActor*  Actor;            // 0x00
    FName    EventTag;         // 0x08 (FName = 8 bytes)
    uint8_t  Payload[0xB0];    // 0x10 — full FGameplayEventData, zeroed
};
#pragma pack(pop)

// Cached undo infrastructure (expensive lookups done once, reused)
static UObject*    g_cachedASC      = nullptr;
static AActor*     g_cachedASCOwner = nullptr;
static UFunction*  g_cachedSendFn   = nullptr;

// Undo cooldown + in-flight guard
static std::chrono::steady_clock::time_point g_lastUndoTime{};
static constexpr int64_t UNDO_COOLDOWN_MS = 500;
static std::atomic<bool> g_undoInFlight{false};

// ──────────────────────────────────────────────────────────────────────────────
// TryUndo — called on Shift+Z
// ──────────────────────────────────────────────────────────────────────────────
void EnsureUndoInfrastructure()
{
    if (!g_cachedSendFn)
    {
        UObject* fnObj = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr,
            STR("/Script/GameplayAbilities.AbilitySystemBlueprintLibrary:SendGameplayEventToActor"));
        g_cachedSendFn = reinterpret_cast<UFunction*>(fnObj);
    }

    if (!g_cachedASC || !IsObjectValid(g_cachedASC))
    {
        g_cachedASC = nullptr;
        g_cachedASCOwner = nullptr;
        std::vector<UObject*> ascs;
        UObjectGlobals::FindAllOf(STR("R5AbilitySystemComponent"), ascs);

        UObject* fallback = nullptr;
        AActor*  fallbackOwner = nullptr;
        for (UObject* a : ascs)
        {
            if (!IsObjectValid(a)) continue;
            AActor* o = a->GetTypedOuter<AActor>();
            if (!o) continue;
            if (!fallback) { fallback = a; fallbackOwner = o; }
            if (o->GetName().find(STR("PlayerState")) != File::StringType::npos)
            {
                g_cachedASC = a;
                g_cachedASCOwner = o;
                break;
            }
        }
        if (!g_cachedASC) { g_cachedASC = fallback; g_cachedASCOwner = fallbackOwner; }
    }
}

void TryUndo()
{
    // Cooldown
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastUndoTime).count();
    if (elapsed < UNDO_COOLDOWN_MS) return;

    // In-flight guard — only one undo at a time
    bool expected = false;
    if (!g_undoInFlight.compare_exchange_strong(expected, true)) return;

    g_lastUndoTime = now;

    // Pop stale entries
    {
        std::lock_guard<std::mutex> lock(g_StackMutex);
        while (!g_UndoStack.empty() && !IsObjectValid(g_UndoStack.back()))
            g_UndoStack.pop_back();

        if (g_UndoStack.empty())
        {
            g_undoInFlight.store(false);
            return;
        }
    }

    EnsureUndoInfrastructure();

    if (!g_cachedASC || !IsObjectValid(g_cachedASC) || !g_cachedASCOwner || !g_cachedSendFn)
    {
        Output::send<LogLevel::Verbose>(STR("[BBT] Undo: missing ASC or SendFn\n"));
        g_cachedASC = nullptr;
        g_undoInFlight.store(false);
        return;
    }

    UObject* block = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_StackMutex);
        // Re-validate after infrastructure lookup
        while (!g_UndoStack.empty() && !IsObjectValid(g_UndoStack.back()))
            g_UndoStack.pop_back();
        if (g_UndoStack.empty())
        {
            g_undoInFlight.store(false);
            return;
        }
        block = g_UndoStack.back();
        g_UndoStack.pop_back();
    }

    if (!block || !IsObjectValid(block))
    {
        Output::send<LogLevel::Verbose>(STR("[BBT] Undo: block became invalid\n"));
        g_undoInFlight.store(false);
        return;
    }

    Output::send<LogLevel::Verbose>(STR("[BBT] Undoing: {}\n"), block->GetName());

    FName undoTag{STR("GAS.GameplayEvent.Building.Undo"), FNAME_Add};
    SendGameplayEventParams params{};
    params.Actor    = g_cachedASCOwner;
    params.EventTag = undoTag;
    *reinterpret_cast<FName*>(&params.Payload[0x00])   = undoTag;
    *reinterpret_cast<AActor**>(&params.Payload[0x08]) = g_cachedASCOwner;
    *reinterpret_cast<AActor**>(&params.Payload[0x10]) = g_cachedASCOwner;

    g_cachedASC->ProcessEvent(g_cachedSendFn, &params);
    Output::send<LogLevel::Verbose>(STR("[BBT] Undo event sent\n"));

    g_undoInFlight.store(false);
}
