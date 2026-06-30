// ══════════════════════════════════════════════════════════════════════════════
// bbt_keystone.cpp — Keystone Research: Programmatic Block Placement
// ══════════════════════════════════════════════════════════════════════════════
//
// GOAL: Place a building block at an arbitrary transform we specify, without
// the player manually clicking. This is the "keystone capability" that unlocks
// copy/paste, blueprints, redo, and replace.
//
// ──────────────────────────────────────────────────────────────────────────────
// BUILDING COMMAND PIPELINE (from CXX header analysis, 2026-06-25)
// ──────────────────────────────────────────────────────────────────────────────
//
// Normal placement flow:
//   1. Player enters build mode
//      → UR5Ability_Building_MakeConstructCommand activates
//      → Creates a UR5AbilityTask_FindConstructTarget (subclass: _Single or _FastBuilding)
//
//   2. FindConstructTarget traces the world for a valid placement spot
//      → Calls NotifyConstructTargetFound(BuildingBrush, Transform, BuildingContainer)
//
//   3. NotifyConstructTargetFound wraps data into FR5TargetData_BuildingCommand
//      inside a FGameplayAbilityTargetDataHandle
//
//   4. MakePreConstructRequest(Handle) sends it to the execution pipeline
//
//   5. UR5Ability_Building_ExecuteCommands::GameplayEventCallback receives
//      the command via FGameplayEventData.TargetData and places the block
//
// ──────────────────────────────────────────────────────────────────────────────
// KEY STRUCTS (offsets confirmed from CXX headers)
// ──────────────────────────────────────────────────────────────────────────────
//
// UR5Ability_Building_MakeConstructCommand (size 0x460):
//   - ConstructionContext  @ 0x03D0  (UR5BuildingConstructionContext*)
//   - StrategyTask         @ 0x03D8  (UR5AbilityTask_FindConstructTarget*)
//   - AvailableBrushes     @ 0x03E0  (TArray<UR5BuildingBrush*>)
//   - MontageTask          @ 0x03F0  (UAbilityTask_PlayMontageAndWait*)
//
// UR5AbilityTask_FindConstructTarget (size 0x1E0):
//   - OnConstructTargetFound  @ 0x0080  (delegate)
//   - Context                 @ 0x00E0  (UR5BuildingConstructionContext*)
//   - TargetBuildingBrush     @ 0x00E8  (UR5BuildingBrush*)
//   - BuildingBlockContainer  @ 0x00F0  (UR5BuildingBlockContainer*)
//   - NotifyConstructTargetFound(Brush, Transform, Container) — THE function to call
//
// UR5BuildingCommand_PreConstruct (size 0xD0):
//   - Instigator              @ 0x0028  (AController*)
//   - BuildingItem            @ 0x0040  (UR5BuildingItem*)
//   - BuildingBrush           @ 0x0048  (UR5BuildingBrush*)
//   - Transform               @ 0x0050  (FTransform, size 0x60)
//   - BuildingBlockContainerId @ 0x00B0  (FR5BLRecordId, size 0x10)
//
// UR5BuildingBrush (size 0xF8):
//   - Components              @ 0x00A8  (TArray<FR5BuildingBrushComponent>)
//   - MakeBrushFromItem(Wco, Item) — creates a brush from any BuildingItem
//   - IsBlueprintBrush() — true for multi-piece structures (huts, piers)
//
// FR5BuildingBrushComponent (size 0xD0):
//   - Item                    @ 0x0000  (UR5BuildingItem*)
//   - RelativeTransform       @ 0x0010  (FTransform, size 0x60)
//   - Neighbours              @ 0x0070  (TArray<int32>) — building graph edges
//
// UR5BuildingConstructionContext (size 0x170):
//   - Brush                   @ 0x0098  (UR5BuildingBrush*)
//   - BrushQuaternion         @ 0x00E0  (FQuat, size 0x20)
//   - BrushYawOffset          @ 0x0100  (FVector, size 0x18)
//   - BaseBrushRotationQuat   @ 0x0120  (FQuat, size 0x20)
//   - RotationStep            @ 0x0140  (int32)
//   - SnappingMode            @ 0x0144  (uint8 enum)
//
// UR5BuildingBlockContainer (size 0x230):
//   - Holder                  @ 0x0028  (AActor*)
//   - BuildingGraph           @ 0x0030  (FR5BuildingGraph, size 0x110)
//
// FGameplayEventData (size 0xB0):
//   - EventTag       @ 0x0000, Instigator @ 0x0008, Target @ 0x0010
//   - TargetData     @ 0x0088  (FGameplayAbilityTargetDataHandle, size 0x28)
//
// ──────────────────────────────────────────────────────────────────────────────
// MULTI-BLOCK REFERENCE (from FModel brush exports)
// ──────────────────────────────────────────────────────────────────────────────
//
// The game already places entire structures from a single brush:
//   - Brush_CoastJungle_Hut_01: 33 components (DA_BI_Poi_* items)
//   - Brush_Pier_01: multi-component dock structure
//   - Each component has Item + RelativeTransform + Neighbours[]
//   - bGraphBuilt=true means neighbor graph is pre-computed
//
// For our blueprint system, we build the same structure at runtime:
//   1. Capture: iterate placed blocks → record Item + relative transform
//   2. Build brush: create UR5BuildingBrush with populated Components array
//   3. Place: feed brush + world transform to NotifyConstructTargetFound
//
// ══════════════════════════════════════════════════════════════════════════════
// PHASE 1: RECON DUMP (this file)
// ══════════════════════════════════════════════════════════════════════════════
//
// Shift+K while in build mode dumps runtime state to UE4SS console:
//   - StrategyTask class name (_Single vs _FastBuilding)
//   - ConstructionContext: current brush, quaternion, snapping mode
//   - FindConstructTarget: container pointer, container holder
//   - AvailableBrushes: count and first brush info
//   - FR5BLRecordId raw bytes from the container
//
// This data closes the remaining unknowns before we can attempt
// programmatic placement in Phase 2.
// ══════════════════════════════════════════════════════════════════════════════

#include "bbt_common.h"

// Flags set by keydown (input thread → game thread)
std::atomic<bool> g_reqKeystoneDump{false};
std::atomic<bool> g_reqKeystoneReplay{false};

// ── Phase 2 stash ─────────────────────────────────────────────────────────────
// Populated by the NotifyConstructTargetFound hook each time the player places a block.
struct KeystoneStash
{
    UObject* Brush     = nullptr;
    UObject* Container = nullptr;
    double   PosX = 0, PosY = 0, PosZ = 0;
    double   QuatX = 0, QuatY = 0, QuatZ = 0, QuatW = 1;
    bool     Valid = false;
};
static KeystoneStash g_stash;

static bool g_keystoneHookRegistered = false;

// ──────────────────────────────────────────────────────────────────────────────
// Helper: dump N bytes as hex string
// ──────────────────────────────────────────────────────────────────────────────
static std::wstring HexDump(const uint8_t* data, size_t len)
{
    std::wstring result;
    result.reserve(len * 3);
    const wchar_t hex[] = STR("0123456789ABCDEF");
    for (size_t i = 0; i < len; ++i)
    {
        if (i > 0) result += L' ';
        result += hex[(data[i] >> 4) & 0xF];
        result += hex[data[i] & 0xF];
    }
    return result;
}

// ──────────────────────────────────────────────────────────────────────────────
// Helper: read a double from a byte pointer
// ──────────────────────────────────────────────────────────────────────────────
static double ReadDouble(const uint8_t* ptr) { double v; std::memcpy(&v, ptr, 8); return v; }
static float  ReadFloat(const uint8_t* ptr)  { float v;  std::memcpy(&v, ptr, 4); return v; }
static int32_t ReadInt32(const uint8_t* ptr) { int32_t v; std::memcpy(&v, ptr, 4); return v; }

// ──────────────────────────────────────────────────────────────────────────────
// Keystone Recon Dump — called on game thread when Shift+K pressed
// ──────────────────────────────────────────────────────────────────────────────
void KeystoneReconDump()
{
    Output::send<LogLevel::Warning>(STR("\n"));
    Output::send<LogLevel::Warning>(STR("══════════════════════════════════════════════════════\n"));
    Output::send<LogLevel::Warning>(STR("  KEYSTONE RECON DUMP — Shift+K\n"));
    Output::send<LogLevel::Warning>(STR("══════════════════════════════════════════════════════\n"));

    // ── Validate prerequisites ──────────────────────────────────────────────
    if (!IsObjectValid(g_cachedConstruct))
    {
        Output::send<LogLevel::Warning>(STR("[Keystone] ERROR: No cached construct ability. Enter build mode first.\n"));
        return;
    }

    auto* constructPtr = reinterpret_cast<uint8_t*>(g_cachedConstruct);
    Output::send<LogLevel::Warning>(STR("[Keystone] MakeConstructCommand @ {:#018X}\n"),
        reinterpret_cast<uintptr_t>(g_cachedConstruct));
    Output::send<LogLevel::Warning>(STR("[Keystone] Class: {}\n"),
        g_cachedConstruct->GetClassPrivate()->GetName());

    // ── 1. StrategyTask (@ 0x03D8) ─────────────────────────────────────────
    // This is the FindConstructTarget task. Its subclass determines how the
    // game traces for placement spots:
    //   _Single = normal block-by-block placement
    //   _FastBuilding = rapid/multi placement mode
    UObject* strategyTask = *reinterpret_cast<UObject**>(constructPtr + 0x03D8);
    if (!strategyTask)
    {
        Output::send<LogLevel::Warning>(STR("[Keystone] StrategyTask @ 0x03D8 = NULL (not in build mode?)\n"));
        return;
    }
    Output::send<LogLevel::Warning>(STR("[Keystone] StrategyTask @ {:#018X}\n"),
        reinterpret_cast<uintptr_t>(strategyTask));
    Output::send<LogLevel::Warning>(STR("[Keystone]   Class: {}\n"),
        strategyTask->GetClassPrivate()->GetName());
    Output::send<LogLevel::Warning>(STR("[Keystone]   Full path: {}\n"),
        strategyTask->GetClassPrivate()->GetFullName());

    auto* taskPtr = reinterpret_cast<uint8_t*>(strategyTask);

    // ── 1a. PreviewActor check (offset 0x0250, _Single subclass only) ──────────
    // UR5AbilityTask_FindConstructTarget base class is 0x1E0 — reading past that
    // would be out of bounds. _Single subclass has PreviewActor @ 0x0250 so it
    // extends to at least 0x0258. Only read if we're on _Single.
    {
        std::wstring taskClass = strategyTask->GetClassPrivate()->GetName();
        bool isSingle = taskClass.find(STR("Single")) != std::wstring::npos;
        Output::send<LogLevel::Warning>(STR("[Keystone]   -- PreviewActor @ 0x0250 (Single subclass only) --\n"));
        if (isSingle)
        {
            // Read raw pointer value first (safe — within _Single subclass bounds)
            uintptr_t rawVal = *reinterpret_cast<uintptr_t*>(taskPtr + 0x0250);
            Output::send<LogLevel::Warning>(STR("[Keystone]   @ 0x0250 raw: {:#018X}\n"), rawVal);
            UObject* candidate = reinterpret_cast<UObject*>(rawVal);
            if (candidate && IsObjectValid(candidate))
            {
                Output::send<LogLevel::Warning>(STR("[Keystone]   @ 0x0250: {} (class: {})\n"),
                    candidate->GetName(), candidate->GetClassPrivate()->GetName());
            }
            else
            {
                Output::send<LogLevel::Warning>(STR("[Keystone]   @ 0x0250: null/invalid (raw={:#018X})\n"), rawVal);
            }
        }
        else
        {
            Output::send<LogLevel::Warning>(STR("[Keystone]   Skipping — task is {} (not _Single, bounds unknown)\n"),
                taskClass);
            // For _FastBuilding: Previews[0] @ 0x01F8 (TArray, within base size)
            uintptr_t rawVal = *reinterpret_cast<uintptr_t*>(taskPtr + 0x01F8);
            Output::send<LogLevel::Warning>(STR("[Keystone]   _FastBuilding Previews[0] @ 0x01F8 raw: {:#018X}\n"), rawVal);
        }
    }

    // ── 1b. TargetBuildingBrush on the task (@ 0x00E8) ──────────────────────
    UObject* taskBrush = *reinterpret_cast<UObject**>(taskPtr + 0x00E8);
    if (taskBrush && IsObjectValid(taskBrush))
    {
        Output::send<LogLevel::Warning>(STR("[Keystone]   TargetBuildingBrush @ 0x00E8: {}\n"),
            taskBrush->GetName());
    }
    else
    {
        Output::send<LogLevel::Warning>(STR("[Keystone]   TargetBuildingBrush @ 0x00E8: NULL\n"));
    }

    // ── 1b. BuildingBlockContainer on the task (@ 0x00F0) ───────────────────
    // This is the "island" that new blocks join. We need to understand its
    // structure to pass it when placing programmatically.
    UObject* container = *reinterpret_cast<UObject**>(taskPtr + 0x00F0);
    if (container && IsObjectValid(container))
    {
        Output::send<LogLevel::Warning>(STR("[Keystone]   BuildingBlockContainer @ 0x00F0: {}\n"),
            container->GetName());
        Output::send<LogLevel::Warning>(STR("[Keystone]   Container class: {}\n"),
            container->GetClassPrivate()->GetName());

        auto* containerBytes = reinterpret_cast<uint8_t*>(container);

        // Holder actor (@ 0x0028)
        UObject* holder = *reinterpret_cast<UObject**>(containerBytes + 0x0028);
        if (holder && IsObjectValid(holder))
        {
            Output::send<LogLevel::Warning>(STR("[Keystone]   Container.Holder @ 0x0028: {} (class: {})\n"),
                holder->GetName(), holder->GetClassPrivate()->GetName());
        }
        else
        {
            Output::send<LogLevel::Warning>(STR("[Keystone]   Container.Holder @ 0x0028: NULL\n"));
        }

        // BuildingGraph raw dump (@ 0x0030, first 32 bytes)
        Output::send<LogLevel::Warning>(STR("[Keystone]   Container.BuildingGraph @ 0x0030 (first 32 bytes):\n"));
        Output::send<LogLevel::Warning>(STR("[Keystone]     {}\n"),
            HexDump(containerBytes + 0x0030, 32));
    }
    else
    {
        Output::send<LogLevel::Warning>(STR("[Keystone]   BuildingBlockContainer @ 0x00F0: NULL (no island targeted)\n"));
    }

    // ── 2. ConstructionContext (@ 0x03D0) ───────────────────────────────────
    // Current build-mode state: which brush is selected, rotation, snapping.
    UObject* context = *reinterpret_cast<UObject**>(constructPtr + 0x03D0);
    if (context && IsObjectValid(context))
    {
        Output::send<LogLevel::Warning>(STR("[Keystone] ConstructionContext @ {:#018X}\n"),
            reinterpret_cast<uintptr_t>(context));
        auto* ctxPtr = reinterpret_cast<uint8_t*>(context);

        // Current brush (@ 0x0098)
        UObject* brush = *reinterpret_cast<UObject**>(ctxPtr + 0x0098);
        if (brush && IsObjectValid(brush))
        {
            Output::send<LogLevel::Warning>(STR("[Keystone]   Brush @ 0x0098: {}\n"), brush->GetName());
            Output::send<LogLevel::Warning>(STR("[Keystone]   Brush class: {}\n"),
                brush->GetClassPrivate()->GetName());

            // Brush Components array (@ 0x00A8 on the brush)
            auto* brushPtr = reinterpret_cast<uint8_t*>(brush);
            auto* compArray = reinterpret_cast<FTArray*>(brushPtr + 0x00A8);
            Output::send<LogLevel::Warning>(STR("[Keystone]   Brush.Components: {} items (element size 0xD0)\n"),
                compArray->Num);

            // First component's item (if any)
            if (compArray->Num > 0 && compArray->Data)
            {
                // FR5BuildingBrushComponent: Item @ 0x0000 (UObject*)
                UObject* firstItem = *reinterpret_cast<UObject**>(compArray->Data);
                if (firstItem && IsObjectValid(firstItem))
                {
                    Output::send<LogLevel::Warning>(STR("[Keystone]   Components[0].Item: {}\n"),
                        firstItem->GetName());
                }
            }
        }
        else
        {
            Output::send<LogLevel::Warning>(STR("[Keystone]   Brush @ 0x0098: NULL\n"));
        }

        // BrushQuaternion (@ 0x00E0, FQuat = 4 doubles)
        double qx = ReadDouble(ctxPtr + 0x00E0);
        double qy = ReadDouble(ctxPtr + 0x00E8);
        double qz = ReadDouble(ctxPtr + 0x00F0);
        double qw = ReadDouble(ctxPtr + 0x00F8);
        Output::send<LogLevel::Warning>(STR("[Keystone]   BrushQuaternion @ 0x00E0: ({:.4f}, {:.4f}, {:.4f}, {:.4f})\n"),
            qx, qy, qz, qw);

        // BrushYawOffset (@ 0x0100, FVector = 3 doubles)
        double ox = ReadDouble(ctxPtr + 0x0100);
        double oy = ReadDouble(ctxPtr + 0x0108);
        double oz = ReadDouble(ctxPtr + 0x0110);
        Output::send<LogLevel::Warning>(STR("[Keystone]   BrushYawOffset @ 0x0100: ({:.2f}, {:.2f}, {:.2f})\n"),
            ox, oy, oz);

        // RotationStep (@ 0x0140, int32)
        int32_t rotStep = ReadInt32(ctxPtr + 0x0140);
        Output::send<LogLevel::Warning>(STR("[Keystone]   RotationStep @ 0x0140: {}\n"), rotStep);

        // SnappingMode (@ 0x0144, uint8 enum)
        uint8_t snapMode = *(ctxPtr + 0x0144);
        Output::send<LogLevel::Warning>(STR("[Keystone]   SnappingMode @ 0x0144: {}\n"), static_cast<int>(snapMode));
    }
    else
    {
        Output::send<LogLevel::Warning>(STR("[Keystone] ConstructionContext @ 0x03D0: NULL\n"));
    }

    // ── 3. AvailableBrushes (@ 0x03E0) ─────────────────────────────────────
    // TArray<UR5BuildingBrush*> — all brushes for the currently selected item.
    auto* brushArray = reinterpret_cast<FTArray*>(constructPtr + 0x03E0);
    Output::send<LogLevel::Warning>(STR("[Keystone] AvailableBrushes @ 0x03E0: {} entries\n"),
        brushArray->Num);
    for (int32_t i = 0; i < brushArray->Num && i < 5; ++i)
    {
        UObject* b = reinterpret_cast<UObject**>(brushArray->Data)[i];
        if (b && IsObjectValid(b))
        {
            auto* bPtr = reinterpret_cast<uint8_t*>(b);
            auto* bComp = reinterpret_cast<FTArray*>(bPtr + 0x00A8);
            Output::send<LogLevel::Warning>(STR("[Keystone]   [{}] {} — {} components\n"),
                i, b->GetName(), bComp->Num);
        }
    }

    // ── 4. FR5BLRecordId exploration ────────────────────────────────────────
    // We need to understand this 0x10-byte struct. Look for it on existing
    // blocks near the player. Read from AR5BuildingBlock's data.
    // The PreConstruct command uses it at offset 0x00B0.
    // Let's try reading from the container if available, or from blocks.
    Output::send<LogLevel::Warning>(STR("\n"));
    Output::send<LogLevel::Warning>(STR("[Keystone] ── FR5BLRecordId Exploration ──\n"));

    // Try to find a nearby building block and dump its identification data
    UObject* pc = GetPlayerController();
    if (pc && IsObjectValid(pc))
    {
        // Get player location for context
        UFunction* vpFn = pc->GetFunctionByNameInChain(STR("GetPlayerViewPoint"));
        if (vpFn)
        {
            GetViewPointParams vp{};
            pc->ProcessEvent(vpFn, &vp);
            Output::send<LogLevel::Warning>(STR("[Keystone]   Player pos: ({:.0f}, {:.0f}, {:.0f})\n"),
                vp.LocX, vp.LocY, vp.LocZ);
        }
    }

    // Look for building blocks and try to find container references on them
    std::vector<UObject*> blocks;
    UObjectGlobals::FindAllOf(STR("R5BuildingBlock"), blocks);
    int dumpCount = 0;
    for (auto* block : blocks)
    {
        if (!block || !IsObjectValid(block)) continue;
        if (dumpCount >= 3) break; // just dump a few

        auto* blockPtr = reinterpret_cast<uint8_t*>(block);
        UObject* item = *reinterpret_cast<UObject**>(blockPtr + 0x0328);

        Output::send<LogLevel::Warning>(STR("[Keystone]   Block: {} (item: {})\n"),
            block->GetName(),
            (item && IsObjectValid(item)) ? item->GetName() : STR("null"));

        // Dump bytes around the block to find container/ID references
        // R5BuildingBlock inherits from AActor (size ~0x2A8), BuildingItem @ 0x0328
        // Look for potential FR5BLRecordId or container pointers after BuildingItem
        Output::send<LogLevel::Warning>(STR("[Keystone]   Block bytes @ 0x0330 (32 bytes after BuildingItem):\n"));
        Output::send<LogLevel::Warning>(STR("[Keystone]     {}\n"),
            HexDump(blockPtr + 0x0330, 32));
        Output::send<LogLevel::Warning>(STR("[Keystone]   Block bytes @ 0x0350 (next 32):\n"));
        Output::send<LogLevel::Warning>(STR("[Keystone]     {}\n"),
            HexDump(blockPtr + 0x0350, 32));

        dumpCount++;
    }

    Output::send<LogLevel::Warning>(STR("\n"));
    Output::send<LogLevel::Warning>(STR("══════════════════════════════════════════════════════\n"));
    Output::send<LogLevel::Warning>(STR("  KEYSTONE RECON DUMP COMPLETE\n"));
    Output::send<LogLevel::Warning>(STR("══════════════════════════════════════════════════════\n"));
    Output::send<LogLevel::Warning>(STR("\n"));
}

// ══════════════════════════════════════════════════════════════════════════════
// PHASE 2: Hook NotifyConstructTargetFound — intercept and replay placements
// ══════════════════════════════════════════════════════════════════════════════

// Pre-hook: fires on game thread every time the player places a block normally.
// Reads brush, container, and transform from the call and stashes them for replay.
static void OnNotifyConstructPre(UnrealScriptFunctionCallableContext& ctx, void*)
{
    UObject* task = ctx.Context;
    if (!task || !IsObjectValid(task)) return;

    // Brush and container are stored on the task at known offsets
    auto* taskPtr  = reinterpret_cast<uint8_t*>(task);
    UObject* brush = *reinterpret_cast<UObject**>(taskPtr + 0x00E8);
    UObject* ctr   = *reinterpret_cast<UObject**>(taskPtr + 0x00F0);

    // Transform comes in as a function parameter — read it from the locals buffer
    double px = 0, py = 0, pz = 0;
    double qx = 0, qy = 0, qz = 0, qw = 1;
    UFunction* fn = ctx.TheStack.CurrentNativeFunction();
    if (fn)
    {
        for (auto* prop : fn->ForEachProperty())
        {
            if (prop->GetName() != STR("Transform")) continue;
            auto* t = reinterpret_cast<uint8_t*>(ctx.TheStack.Locals()) + prop->GetOffset_Internal();
            // FTransform layout (UE5 doubles):
            //   Rotation (FQuat):  X@+0x00  Y@+0x08  Z@+0x10  W@+0x18
            //   Translation:      X@+0x20  Y@+0x28  Z@+0x30
            //   Scale3D:          X@+0x38  Y@+0x40  Z@+0x48
            qx = *reinterpret_cast<double*>(t + 0x00);
            qy = *reinterpret_cast<double*>(t + 0x08);
            qz = *reinterpret_cast<double*>(t + 0x10);
            qw = *reinterpret_cast<double*>(t + 0x18);
            px = *reinterpret_cast<double*>(t + 0x20);
            py = *reinterpret_cast<double*>(t + 0x28);
            pz = *reinterpret_cast<double*>(t + 0x30);
            break;
        }
    }

    if (brush && IsObjectValid(brush))     g_stash.Brush     = brush;
    if (ctr   && IsObjectValid(ctr))       g_stash.Container = ctr;
    g_stash.PosX = px; g_stash.PosY = py; g_stash.PosZ = pz;
    g_stash.QuatX = qx; g_stash.QuatY = qy; g_stash.QuatZ = qz; g_stash.QuatW = qw;
    g_stash.Valid = true;

    Output::send<LogLevel::Warning>(
        STR("[Keystone] CAPTURED: brush={} pos=({:.0f},{:.0f},{:.0f}) quat=({:.3f},{:.3f},{:.3f},{:.3f})\n"),
        brush ? brush->GetName() : STR("null"),
        px, py, pz, qx, qy, qz, qw);
}

// Post-hook: fires on game thread AFTER the StrategyTask has positioned the PreviewActor.
// This is the correct time to override the ghost rotation — it runs during the actor tick
// phase, before the render thread captures transforms (unlike post-engine-tick which is too late).
static void OnNotifyConstructPost(UnrealScriptFunctionCallableContext& ctx, void*)
{
    if (!g_copyAngleHold) return;

    UObject* task = ctx.Context;
    if (!task || !IsObjectValid(task)) return;

    // Only apply on _Single subclass — PreviewActor@0x0250 is only valid there
    std::wstring className = task->GetClassPrivate()->GetName();
    if (className.find(STR("Single")) == std::wstring::npos) return;

    auto* taskPtr = reinterpret_cast<uint8_t*>(task);
    UObject* previewActor = *reinterpret_cast<UObject**>(taskPtr + 0x0250);
    if (!previewActor || !IsObjectValid(previewActor)) return;

    std::wstring previewClass = previewActor->GetClassPrivate()->GetName();
    if (previewClass.find(STR("R5BuildingConstructTargetPreview")) == std::wstring::npos) return;

    UFunction* setRotFn = previewActor->GetFunctionByNameInChain(STR("K2_SetActorRotation"));
    if (!setRotFn) return;

    struct { double Pitch, Yaw, Roll; bool bTeleport; } rotParams{
        g_copyAngleTarget.RotPitch,
        g_copyAngleTarget.RotYaw,
        g_copyAngleTarget.RotRoll,
        true
    };
    previewActor->ProcessEvent(setRotFn, &rotParams);
}

// Called from pre-tick. Registers the hook once the StrategyTask exists.
// UFunctions are per-class so one registration covers all future instances.
void EnsureKeystoneHook()
{
    if (g_keystoneHookRegistered) return;
    if (!IsObjectValid(g_cachedConstruct)) return;

    UObject* strategyTask = *reinterpret_cast<UObject**>(
        reinterpret_cast<uint8_t*>(g_cachedConstruct) + 0x03D8);
    if (!strategyTask || !IsObjectValid(strategyTask)) return;

    UFunction* fn = strategyTask->GetFunctionByNameInChain(STR("NotifyConstructTargetFound"));
    if (!fn) return;

    UObjectGlobals::RegisterHook(fn, OnNotifyConstructPre, OnNotifyConstructPost, nullptr);
    g_keystoneHookRegistered = true;
    Output::send<LogLevel::Verbose>(STR("[Keystone] Hook registered on {}\n"),
        strategyTask->GetClassPrivate()->GetName());
}

// Shift+P: fire NotifyConstructTargetFound ourselves using current context state.
// No capture step needed — reads brush from ConstructionContext, position from player.
// Container is NULL (test without island attachment first).
// If a block appears near the player → keystone confirmed.
void KeystoneReplayTest()
{
    if (!IsObjectValid(g_cachedConstruct))
    {
        Output::send<LogLevel::Warning>(STR("[Keystone] Replay: enter build mode first\n"));
        return;
    }

    auto* constructPtr = reinterpret_cast<uint8_t*>(g_cachedConstruct);

    // Brush from ConstructionContext @ 0x03D0 → Brush @ 0x0098
    UObject* context = *reinterpret_cast<UObject**>(constructPtr + 0x03D0);
    if (!context || !IsObjectValid(context))
    {
        Output::send<LogLevel::Warning>(STR("[Keystone] Replay: no ConstructionContext\n"));
        return;
    }
    UObject* brush = *reinterpret_cast<UObject**>(
        reinterpret_cast<uint8_t*>(context) + 0x0098);
    if (!brush || !IsObjectValid(brush))
    {
        Output::send<LogLevel::Warning>(STR("[Keystone] Replay: no brush — select a build item first\n"));
        return;
    }

    UObject* strategyTask = *reinterpret_cast<UObject**>(constructPtr + 0x03D8);
    if (!strategyTask || !IsObjectValid(strategyTask))
    {
        Output::send<LogLevel::Warning>(STR("[Keystone] Replay: no StrategyTask\n"));
        return;
    }

    UFunction* fn = strategyTask->GetFunctionByNameInChain(STR("NotifyConstructTargetFound"));
    if (!fn)
    {
        Output::send<LogLevel::Warning>(STR("[Keystone] Replay: NotifyConstructTargetFound not found\n"));
        return;
    }

    // Place target 300 units in front of the player pawn (camera yaw direction).
    // GetPlayerViewPoint returns CAMERA position (behind+above the pawn in 3rd-person),
    // so we use the VIEW TARGET (pawn) as origin to get the correct forward offset.
    double px = 0, py = 0, pz = 0;
    UObject* pc = GetPlayerController();
    if (pc && IsObjectValid(pc))
    {
        // Camera yaw for facing direction
        UFunction* vpFn = pc->GetFunctionByNameInChain(STR("GetPlayerViewPoint"));
        double yawRad = 0;
        if (vpFn)
        {
            GetViewPointParams vp{};
            pc->ProcessEvent(vpFn, &vp);
            yawRad = vp.RotYaw * EYE_PI / 180.0;
        }

        // Pawn position as origin
        UFunction* vtFn = pc->GetFunctionByNameInChain(STR("GetViewTarget"));
        if (vtFn)
        {
            struct { UObject* ReturnValue; } vtParams{};
            pc->ProcessEvent(vtFn, &vtParams);
            UObject* pawn = vtParams.ReturnValue;
            if (pawn && IsObjectValid(pawn))
            {
                UFunction* locFn = pawn->GetFunctionByNameInChain(STR("K2_GetActorLocation"));
                if (locFn)
                {
                    struct { double X, Y, Z; } locParams{};
                    pawn->ProcessEvent(locFn, &locParams);
                    px = locParams.X + std::cos(yawRad) * 300.0;
                    py = locParams.Y + std::sin(yawRad) * 300.0;
                    pz = locParams.Z + 100.0; // pawn center is ~waist height, +100 puts us above ground
                }
            }
        }
    }

    int32_t sz = fn->GetStructureSize();
    if (sz < 64) sz = 256;
    std::vector<uint8_t> params(sz, 0);

    for (auto* prop : fn->ForEachProperty())
    {
        auto nm  = prop->GetName();
        auto off = prop->GetOffset_Internal();

        if (nm == STR("BuildingBrush"))
            *reinterpret_cast<UObject**>(params.data() + off) = brush;
        else if (nm == STR("BuildingContainer"))
        {
            // Pass the real container from the StrategyTask — NULL was likely why
            // blocks didn't appear in previous tests. Container confirmed valid at 0x00F0.
            UObject* ctr = *reinterpret_cast<UObject**>(
                reinterpret_cast<uint8_t*>(strategyTask) + 0x00F0);
            *reinterpret_cast<UObject**>(params.data() + off) = ctr;
        }
        else if (nm == STR("Transform"))
        {
            auto* t = params.data() + off;
            *reinterpret_cast<double*>(t + 0x00) = 0.0; // QuatX
            *reinterpret_cast<double*>(t + 0x08) = 0.0; // QuatY
            *reinterpret_cast<double*>(t + 0x10) = 0.0; // QuatZ
            *reinterpret_cast<double*>(t + 0x18) = 1.0; // QuatW (identity)
            *reinterpret_cast<double*>(t + 0x20) = px;
            *reinterpret_cast<double*>(t + 0x28) = py;
            *reinterpret_cast<double*>(t + 0x30) = pz;
            *reinterpret_cast<double*>(t + 0x38) = 1.0;
            *reinterpret_cast<double*>(t + 0x40) = 1.0;
            *reinterpret_cast<double*>(t + 0x48) = 1.0;
        }
    }

    UObject* ctrForLog = *reinterpret_cast<UObject**>(
        reinterpret_cast<uint8_t*>(strategyTask) + 0x00F0);
    Output::send<LogLevel::Warning>(
        STR("[Keystone] REPLAY firing: brush={} pos=({:.0f},{:.0f},{:.0f}) container={}\n"),
        brush->GetName(), px, py, pz,
        (ctrForLog && IsObjectValid(ctrForLog)) ? ctrForLog->GetName() : STR("NULL"));

    strategyTask->ProcessEvent(fn, params.data());

    Output::send<LogLevel::Warning>(STR("[Keystone] REPLAY complete — check if a block appeared\n"));
}

// ──────────────────────────────────────────────────────────────────────────────
// ImGui panel for keystone research (in UE4SS BBT tab)
// ──────────────────────────────────────────────────────────────────────────────
void KeystoneImGui()
{
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Keystone Research");

    ImGui::Text("Hook: %s", g_keystoneHookRegistered ? "REGISTERED" : "waiting (enter build mode)");

    if (g_stash.Valid)
    {
        ImGui::Text("Stash: %s @ (%.0f, %.0f, %.0f)",
            g_stash.Brush ? "brush OK" : "no brush",
            g_stash.PosX, g_stash.PosY, g_stash.PosZ);
    }
    else
    {
        ImGui::TextDisabled("Stash: empty — place a block to capture");
    }

    if (ImGui::Button("Shift+K: Recon Dump"))  g_reqKeystoneDump.store(true);
    ImGui::SameLine();
    if (ImGui::Button("Shift+P: Replay +200X")) g_reqKeystoneReplay.store(true);
}
