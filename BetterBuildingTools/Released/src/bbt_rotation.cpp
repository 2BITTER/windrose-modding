#include "bbt_common.h"

// ──────────────────────────────────────────────────────────────────────────────
// Fine rotation cycle (roadmap item 8)
// ──────────────────────────────────────────────────────────────────────────────
// Quartermaster's "Building Degrees of Freedom" merges 1/5/10 into
// R5BuildingSettings.RotationSteps via a static config-ini patch (loose pak,
// no DLL). We get the same merged cycle live, without touching the TArray at
// all: read the vanilla steps once (read-only — no array growth/allocator
// risk) and advance UR5BuildingConstructionContext::RotationStep
// (UR5BuildingConstructionContext + 0x0140) through {vanilla} merged with
// whichever of {1,5,10} are currently enabled, on the player's own keybind.
// The game's own rotation-step input/array are never written to.
//
// 1/5/10 are independently toggleable (not one switch) because the game
// snaps to a nearby block's rotation within ~3 degrees — a stray 1-degree
// step can land just inside that window and silently misalign a wall from
// its neighbor. Users who hit that can drop 1 deg (or 5) without losing the
// rest of the cycle.
static std::vector<int32_t> g_vanillaRotationSteps;
static bool                 g_vanillaRotationStepsCached = false;

static void CacheVanillaRotationSteps()
{
    if (g_vanillaRotationStepsCached) return;

    UObject* cdo = UObjectGlobals::StaticFindObject<UObject*>(
        nullptr, nullptr, STR("/Script/R5.Default__R5BuildingSettings"));
    if (!IsObjectValid(cdo)) return; // not loaded yet — retry next press

    // RotationSteps: TArray<int32> @ UR5BuildingSettings + 0x0388 (read-only).
    auto* arr  = reinterpret_cast<FTArray*>(reinterpret_cast<uint8_t*>(cdo) + 0x0388);
    auto* data = reinterpret_cast<int32_t*>(arr->Data);

    g_vanillaRotationSteps.assign(data, data + arr->Num);
    std::sort(g_vanillaRotationSteps.begin(), g_vanillaRotationSteps.end());
    g_vanillaRotationStepsCached = true;

    Output::send<LogLevel::Verbose>(
        STR("[BBT] Vanilla rotation steps cached: {}\n"), g_vanillaRotationSteps.size());
}

// Rebuilt on every keypress (cheap — a handful of ints) so toggling 1/5/10 in
// the ImGui tab takes effect immediately, same as every other BBT toggle.
std::vector<int32_t> BuildRotationCycle()
{
    std::set<int32_t> merged(g_vanillaRotationSteps.begin(), g_vanillaRotationSteps.end());
    if (g_cfg.rotation1Enabled)  merged.insert(1);
    if (g_cfg.rotation5Enabled)  merged.insert(5);
    if (g_cfg.rotation10Enabled) merged.insert(10);
    return std::vector<int32_t>(merged.begin(), merged.end());
}

// Pure lookup: given the step value the context had BEFORE this advance,
// return the next value in the merged cycle (wraps after the last/highest).
static int32_t NextInRotationCycle(const std::vector<int32_t>& cycle, int32_t current)
{
    auto found = std::find(cycle.begin(), cycle.end(), current);
    if (found != cycle.end())
    {
        size_t idx = static_cast<size_t>(std::distance(cycle.begin(), found) + 1) % cycle.size();
        return cycle[idx];
    }
    // Current value isn't an exact cycle member (shouldn't normally happen) —
    // land on the next-greater entry, or wrap to the first.
    auto gt = std::upper_bound(cycle.begin(), cycle.end(), current);
    return (gt == cycle.end()) ? cycle.front() : *gt;
}

// Prefer the reflected Set/GetRotationStep accessors (same naming pattern as
// the already-proven SetBuildingBrush) so any UI/delegate listeners stay in
// sync. Fall back to a raw field write if it turns out not to be reflected.
static void ApplyRotationStep(UObject* constructionContext, int32_t newStep)
{
    UFunction* setFn = constructionContext->GetFunctionByNameInChain(STR("SetRotationStep"));
    if (setFn)
    {
        struct { int32_t StepValue; } params{ newStep };
        constructionContext->ProcessEvent(setFn, &params);
    }
    else
    {
        *reinterpret_cast<int32_t*>(
            reinterpret_cast<uint8_t*>(constructionContext) + 0x0140) = newStep;
    }
}

// ── Hook the game's OWN rotation-step input handler ────────────────────────
// Rather than give BBT a separate keybind, hook
// UR5Ability_Building_MakeConstructCommand::OnChangeRotationStep — the real
// UFUNCTION the engine's Enhanced Input system calls when the player presses
// WHATEVER key they have bound to "Rotation Step" in the game's own
// keybindings menu. That way there's exactly one key to remember, and
// rebinding it in-game just works with no separate config to keep in sync.
//
// RegisterHook always still runs the original function (there's no "cancel"),
// so: the PRE callback only READS+stashes RotationStep (before vanilla's own
// array-only cycling overwrites it); the POST callback then computes the true
// next value from OUR merged cycle (based on the stashed pre-value) and
// overwrites whatever vanilla just set. Net effect: the player's own key
// drives our merged cycle instead of the vanilla-only one.
//
// Threading: this fires from the engine's native Enhanced Input processing on
// the GAME THREAD (NOT UE4SS's separate input thread that register_keydown_event
// uses) — so touching live UObjects/ProcessEvent directly here is safe, same
// as any other native UFunction hook.
static int32_t g_rotationStepPreValue   = 0;
static bool    g_rotationHookRegistered = false;

bool BBT_AnyFineStepEnabled()
{
    return g_cfg.rotation1Enabled || g_cfg.rotation5Enabled || g_cfg.rotation10Enabled;
}

static void BBT_RotationStepPre(UnrealScriptFunctionCallableContext& ctx, void* /*customData*/)
{
    if (!BBT_AnyFineStepEnabled()) return;
    UObject* ability = ctx.Context;
    if (!IsObjectValid(ability)) return;
    UObject* context = *reinterpret_cast<UObject**>(
        reinterpret_cast<uint8_t*>(ability) + 0x03D0); // ConstructionContext
    if (!IsObjectValid(context)) return;
    g_rotationStepPreValue = *reinterpret_cast<int32_t*>(
        reinterpret_cast<uint8_t*>(context) + 0x0140); // RotationStep, before vanilla runs
}

static void BBT_RotationStepPost(UnrealScriptFunctionCallableContext& ctx, void* /*customData*/)
{
    // All three off — leave vanilla's own result untouched rather than
    // reimplementing its cycling and risk a redundant/mismatched overwrite.
    if (!BBT_AnyFineStepEnabled()) return;
    UObject* ability = ctx.Context;
    if (!IsObjectValid(ability)) return;
    UObject* context = *reinterpret_cast<UObject**>(
        reinterpret_cast<uint8_t*>(ability) + 0x03D0);
    if (!IsObjectValid(context)) return;

    CacheVanillaRotationSteps();
    if (g_vanillaRotationSteps.empty()) return;

    auto    cycle = BuildRotationCycle();
    int32_t next  = NextInRotationCycle(cycle, g_rotationStepPreValue);
    ApplyRotationStep(context, next);
    g_copyAngleHold = false; // player manually rotated — release the match-angle lock
    Output::send<LogLevel::Verbose>(
        STR("[BBT] Rotation step (game key): {} -> {}\n"), g_rotationStepPreValue, next);
}

// Registers the hook once OnChangeRotationStep is resolvable (same class as
// the already-proven SetBuildingBrush lookup, so it's safe once the construct
// ability instance exists — see the JIT-loading caveat in project notes:
// game-class UFunctions aren't guaranteed loaded until an instance spawns).
void EnsureRotationHook()
{
    if (g_rotationHookRegistered) return;
    UObject* construct = GetConstructAbility();
    if (!IsObjectValid(construct)) return; // not loaded yet — retry next tick

    UFunction* fn = construct->GetFunctionByNameInChain(STR("OnChangeRotationStep"));
    if (!fn) return; // not resolvable yet — retry next tick

    UObjectGlobals::RegisterHook(fn, BBT_RotationStepPre, BBT_RotationStepPost, nullptr);
    g_rotationHookRegistered = true;
    Output::send<LogLevel::Verbose>(STR("[BBT] Rotation-step hook registered\n"));
}
