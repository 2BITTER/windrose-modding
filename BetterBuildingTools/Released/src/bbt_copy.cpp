#include "bbt_common.h"

// ──────────────────────────────────────────────────────────────────────────────
// Copy Object + Copy Angle + Line Trace
// ──────────────────────────────────────────────────────────────────────────────

// Class caches local to copy (currently unused but kept for future use)
static UClass* g_singleTaskClass   = nullptr;
static UClass* g_fastBuildTaskClass = nullptr;

static UClass* GetSingleTaskClass()
{
    if (!g_singleTaskClass)
        g_singleTaskClass = reinterpret_cast<UClass*>(UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/R5.R5AbilityTask_FindConstructTarget_Single")));
    return g_singleTaskClass;
}

static UClass* GetFastBuildTaskClass()
{
    if (!g_fastBuildTaskClass)
        g_fastBuildTaskClass = reinterpret_cast<UClass*>(UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/R5.R5AbilityTask_FindConstructTarget_FastBuilding")));
    return g_fastBuildTaskClass;
}

// Physics line trace via UKismetSystemLibrary::LineTraceSingle.
// Params are written at their REAL runtime offsets (looked up from the UFunction),
// so there's no hand-alignment risk. Returns the hit building-block actor or null.
UObject* TraceForBlock(UObject* worldCtx,
                              double cx, double cy, double cz,
                              double dx, double dy, double dz)
{
    // Cache the UFunction + param offsets once — they never change, and the
    // path lookup + property scan are too costly to repeat every keypress.
    static UFunction* fn = nullptr;
    static int32_t oWctx=-1, oStart=-1, oEnd=-1, oChannel=-1,
                   oComplex=-1, oIgnoreSelf=-1, oDraw=-1, oOutHit=-1, oRet=-1;
    if (!fn)
    {
        UObject* fnObj = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:LineTraceSingle"));
        fn = reinterpret_cast<UFunction*>(fnObj);
        if (!fn)
        {
            Output::send<LogLevel::Verbose>(STR("[CopyObject] LineTraceSingle UFunction not found\n"));
            return nullptr;
        }
        for (auto* prop : fn->ForEachProperty())
        {
            auto nm = prop->GetName(); int32_t o = prop->GetOffset_Internal();
            if      (nm == STR("WorldContextObject")) oWctx = o;
            else if (nm == STR("Start"))         oStart = o;
            else if (nm == STR("End"))           oEnd = o;
            else if (nm == STR("TraceChannel"))  oChannel = o;
            else if (nm == STR("bTraceComplex")) oComplex = o;
            else if (nm == STR("bIgnoreSelf"))   oIgnoreSelf = o;
            else if (nm == STR("DrawDebugType")) oDraw = o;
            else if (nm == STR("OutHit"))        oOutHit = o;
            else if (nm == STR("ReturnValue"))   oRet = o;
        }
    }
    if (oOutHit < 0 || oStart < 0 || oEnd < 0 || oWctx < 0)
    {
        Output::send<LogLevel::Verbose>(STR("[CopyObject] trace: param offsets not resolved\n"));
        return nullptr;
    }

    constexpr double REACH = 5000.0; // 50m
    double ex = cx + dx * REACH, ey = cy + dy * REACH, ez = cz + dz * REACH;

    alignas(16) uint8_t buf[0x400] = {};
    *reinterpret_cast<UObject**>(buf + oWctx) = worldCtx;
    { auto* v = reinterpret_cast<double*>(buf + oStart); v[0]=cx; v[1]=cy; v[2]=cz; }
    { auto* v = reinterpret_cast<double*>(buf + oEnd);   v[0]=ex; v[1]=ey; v[2]=ez; }
    buf[oChannel] = 0;                          // ETraceTypeQuery1 (Visibility)
    if (oComplex    >= 0) buf[oComplex]    = 0;
    if (oIgnoreSelf >= 0) buf[oIgnoreSelf] = 1;
    if (oDraw       >= 0) buf[oDraw]       = 0; // EDrawDebugTrace::None

    worldCtx->ProcessEvent(fn, buf);

    bool hit = (oRet >= 0) && buf[oRet] != 0;
    if (!hit) { Output::send<LogLevel::Verbose>(STR("[CopyObject] trace: nothing hit\n")); return nullptr; }

    uint8_t* hr = buf + oOutHit;
    // FHitResult: Component (TWeakObjectPtr<UPrimitiveComponent>) @ +0xD8
    UObject* comp  = reinterpret_cast<FWeakObjectPtr*>(hr + 0xD8)->Get();
    UObject* actor = nullptr;
    if (comp && IsObjectValid(comp))
        actor = comp->GetTypedOuter<AActor>();
    if (!actor)  // fallback: HitObjectHandle.ReferenceObject @ +0xB8
        actor = reinterpret_cast<FWeakObjectPtr*>(hr + 0xB8)->Get();
    if (!actor || !IsObjectValid(actor))
    {
        Output::send<LogLevel::Verbose>(STR("[CopyObject] trace: hit but no actor resolved\n"));
        return nullptr;
    }

    UClass* blockClass = GetBlockClass();
    if (blockClass && !actor->IsA(blockClass))
    {
        Output::send<LogLevel::Verbose>(
            STR("[CopyObject] trace hit non-block: {}\n"), actor->GetName());
        return nullptr;
    }
    Output::send<LogLevel::Verbose>(STR("[CopyObject] TRACE hit block: {}\n"), actor->GetName());
    return actor;
}

void TryCopyObject()
{
    // 0. Need the construct ability (cached). Absent only before any build session.
    UObject* construct = GetConstructAbility();
    if (!IsObjectValid(construct))
        return; // silent no-op

    // 1. Local player controller (cached) → camera view point
    UObject* pc = GetPlayerController();
    if (!IsObjectValid(pc))
    {
        Output::send<LogLevel::Verbose>(STR("[CopyObject] No PlayerController found\n"));
        return;
    }

    UFunction* vpFn = pc->GetFunctionByNameInChain(STR("GetPlayerViewPoint"));
    if (!vpFn)
    {
        Output::send<LogLevel::Verbose>(STR("[CopyObject] GetPlayerViewPoint not found\n"));
        return;
    }

    GetViewPointParams vp{};
    pc->ProcessEvent(vpFn, &vp);

    // Camera forward from rotation (UE: Z-up, degrees)
    double pitch = vp.RotPitch * EYE_PI / 180.0;
    double yaw   = vp.RotYaw   * EYE_PI / 180.0;
    double cp    = std::cos(pitch);
    double dirX  = cp * std::cos(yaw);
    double dirY  = cp * std::sin(yaw);
    double dirZ  = std::sin(pitch);

    Output::send<LogLevel::Verbose>(
        STR("[CopyObject] cam=({:.0f},{:.0f},{:.0f}) dir=({:.2f},{:.2f},{:.2f})\n"),
        vp.LocX, vp.LocY, vp.LocZ, dirX, dirY, dirZ);

    // 2. Physics line trace (pixel-accurate, hits the actual mesh — single call).
    UObject* best = TraceForBlock(pc, vp.LocX, vp.LocY, vp.LocZ, dirX, dirY, dirZ);
    if (!best)
    {
        Output::send<LogLevel::Verbose>(STR("[CopyObject] No block under crosshair\n"));
        return;
    }

    // BuildingItem at offset 0x0328 on AR5BuildingBlock
    UObject* item = *reinterpret_cast<UObject**>(
        reinterpret_cast<uint8_t*>(best) + 0x0328);
    if (!item || !IsObjectValid(item))
    {
        Output::send<LogLevel::Verbose>(STR("[CopyObject]   BuildingItem: <null>\n"));
        return;
    }
    Output::send<LogLevel::Verbose>(STR("[CopyObject]   BuildingItem: {}\n"), item->GetName());

    // ── Phase 2: swap the active build selection to this item's brush ─────────
    // (construct ability already validated at the top — we're in build mode)
    // AvailableBrushes: TArray<UR5BuildingBrush*> @ 0x03E0
    auto* brushArr = reinterpret_cast<FTArray*>(
        reinterpret_cast<uint8_t*>(construct) + 0x03E0);
    UObject** brushes  = reinterpret_cast<UObject**>(brushArr->Data);
    int32_t   brushNum = brushArr->Num;
    Output::send<LogLevel::Verbose>(STR("[CopyObject] AvailableBrushes={}\n"), brushNum);
    if (!brushes || brushNum <= 0) return;

    // Find the brush whose component Item matches the looked-at block's BuildingItem.
    // (DataAssets are singletons, so pointer equality holds.)
    UObject* matchBrush = nullptr;
    for (int32_t i = 0; i < brushNum && !matchBrush; ++i)
    {
        UObject* brush = brushes[i];
        if (!IsObjectValid(brush)) continue;
        // Components: TArray<FR5BuildingBrushComponent> @ 0x00A8, element 0xD0, Item @ +0x00
        auto*    compArr  = reinterpret_cast<FTArray*>(
            reinterpret_cast<uint8_t*>(brush) + 0x00A8);
        uint8_t* compData = compArr->Data;
        if (!compData) continue;
        for (int32_t c = 0; c < compArr->Num; ++c)
        {
            UObject* compItem = *reinterpret_cast<UObject**>(compData + c * 0xD0);
            if (compItem == item) { matchBrush = brush; break; }
        }
    }

    if (!matchBrush)
    {
        Output::send<LogLevel::Verbose>(
            STR("[CopyObject] No brush matches this item (not in your build list?)\n"));
        return;
    }

    // Call SetBuildingBrush(UR5BuildingBrush*) — single pointer param.
    UFunction* setFn = construct->GetFunctionByNameInChain(STR("SetBuildingBrush"));
    if (!setFn)
    {
        Output::send<LogLevel::Verbose>(STR("[CopyObject] SetBuildingBrush UFunction not found\n"));
        return;
    }
    struct { UObject* Brush; } setParams{ matchBrush };
    construct->ProcessEvent(setFn, &setParams);
    Output::send<LogLevel::Verbose>(
        STR("[CopyObject] >>> Swapped build selection to {} <<<\n"), item->GetName());
}

// ──────────────────────────────────────────────────────────────────────────────
// TryMatchAngle — called on Alt+C (roadmap item 7)
// ──────────────────────────────────────────────────────────────────────────────
// Separate keybind from copy object on purpose (user call): Alt+C, not folded
// into Shift+C. Reuses copy object's physics trace to find whatever block
// is under the crosshair, reads its world rotation via the reflected, BlueprintPure
// AActor::K2_GetActorRotation (confirmed in Engine.hpp), converts Euler->quaternion
// the same way the engine does (RC::Unreal::FRotator::Quaternion()), and writes the
// result straight into the construction context's live brush rotation via the
// reflected RotateBrush(const FQuat&) setter — the same accessor pattern already
// proven for SetBuildingBrush/SetRotationStep.
// RotateBrush(const FQuat&) is a DELTA operation: it computes
//   BrushQuaternion = current × param  (Hamilton product, confirmed from readback logs).
// To SET an absolute target we must pass delta = conjugate(current) × target so that
//   current × delta = current × (conjugate(current) × target) = target.
// conjugate of unit quat (cx,cy,cz,cw) = (-cx,-cy,-cz,cw).
static void ApplyBrushQuaternion(UObject* constructionContext, double tx, double ty, double tz, double tw, bool log)
{
    // Read current BrushQuaternion (@ +0x00E0, 4 doubles XYZW).
    auto* qField = reinterpret_cast<double*>(
        reinterpret_cast<uint8_t*>(constructionContext) + 0x00E0);
    double cx = qField[0], cy = qField[1], cz = qField[2], cw = qField[3];

    if (std::isnan(cx) || std::isnan(cy) || std::isnan(cz) || std::isnan(cw))
    {
        qField[0] = tx; qField[1] = ty; qField[2] = tz; qField[3] = tw;
        if (log) Output::send<LogLevel::Verbose>(STR("[CopyAngle] NaN recovery — raw write to ({:.3f},{:.3f},{:.3f},{:.3f})\n"), tx, ty, tz, tw);
        return;
    }

    // conjugate(current) = (-cx,-cy,-cz,cw)
    // delta = conjugate(current) * target  (Hamilton product)
    double ix = -cx, iy = -cy, iz = -cz, iw = cw;
    double dx = iw*tx + ix*tw + iy*tz - iz*ty;
    double dy = iw*ty + iy*tw + iz*tx - ix*tz;
    double dz = iw*tz + iz*tw + ix*ty - iy*tx;
    double dw = iw*tw - ix*tx - iy*ty - iz*tz;

    UFunction* rotFn = constructionContext->GetFunctionByNameInChain(STR("RotateBrush"));
    if (rotFn)
    {
        struct { double X, Y, Z, W; } params{ dx, dy, dz, dw };
        constructionContext->ProcessEvent(rotFn, &params);
        if (log) Output::send<LogLevel::Verbose>(
            STR("[CopyAngle] RotateBrush: current=({:.3f},{:.3f},{:.3f},{:.3f}) target=({:.3f},{:.3f},{:.3f},{:.3f}) result=({:.3f},{:.3f},{:.3f},{:.3f})\n"),
            cx, cy, cz, cw, tx, ty, tz, tw, qField[0], qField[1], qField[2], qField[3]);
    }
    else
    {
        // RotateBrush not reflected — raw absolute write directly into BrushQuaternion.
        qField[0] = tx; qField[1] = ty; qField[2] = tz; qField[3] = tw;
        if (log) Output::send<LogLevel::Verbose>(
            STR("[CopyAngle] RotateBrush NOT FOUND, raw write to ({:.3f},{:.3f},{:.3f},{:.3f})\n"),
            tx, ty, tz, tw);
    }
}

// Reapplied every Post-tick while g_copyAngleHold is true.
// Keeps BrushQuaternion at the target so placement lands at the copied angle.
// Ghost visual does NOT update live (known limitation — toggle destroy/build
// mode to force re-init). See project notes for full history of attempts.
void SyncMatchAngle()
{
    if (!g_copyAngleHold) return;
    UObject* construct = g_cachedConstruct;
    if (!IsObjectValid(construct)) { g_copyAngleHold = false; return; }
    UObject* context = *reinterpret_cast<UObject**>(
        reinterpret_cast<uint8_t*>(construct) + 0x03D0);
    if (!IsObjectValid(context))   { g_copyAngleHold = false; return; }

    ApplyBrushQuaternion(context,
        g_copyAngleTarget.X, g_copyAngleTarget.Y,
        g_copyAngleTarget.Z, g_copyAngleTarget.W, false);
}

void TryMatchAngle()
{
    UObject* construct = GetConstructAbility();
    if (!IsObjectValid(construct))
        return; // silent no-op, not in build mode

    UObject* pc = GetPlayerController();
    if (!IsObjectValid(pc))
    {
        Output::send<LogLevel::Verbose>(STR("[CopyAngle] No PlayerController found\n"));
        return;
    }

    UFunction* vpFn = pc->GetFunctionByNameInChain(STR("GetPlayerViewPoint"));
    if (!vpFn)
    {
        Output::send<LogLevel::Verbose>(STR("[CopyAngle] GetPlayerViewPoint not found\n"));
        return;
    }
    GetViewPointParams vp{};
    pc->ProcessEvent(vpFn, &vp);

    double pitch = vp.RotPitch * EYE_PI / 180.0;
    double yaw   = vp.RotYaw   * EYE_PI / 180.0;
    double cp    = std::cos(pitch);
    double dirX  = cp * std::cos(yaw);
    double dirY  = cp * std::sin(yaw);
    double dirZ  = std::sin(pitch);

    UObject* best = TraceForBlock(pc, vp.LocX, vp.LocY, vp.LocZ, dirX, dirY, dirZ);
    if (!best)
    {
        Output::send<LogLevel::Verbose>(STR("[CopyAngle] No block under crosshair\n"));
        return;
    }

    UFunction* rotFn = best->GetFunctionByNameInChain(STR("K2_GetActorRotation"));
    if (!rotFn)
    {
        Output::send<LogLevel::Verbose>(STR("[CopyAngle] K2_GetActorRotation not found\n"));
        return;
    }
    struct { double Pitch, Yaw, Roll; } rot{};
    best->ProcessEvent(rotFn, &rot);

    FRotator rotator(rot.Pitch, rot.Yaw, rot.Roll);
    FQuat    quat = rotator.Quaternion();

    // ConstructionContext @ UR5Ability_Building_MakeConstructCommand + 0x03D0
    // (same field the rotation-step hook reads).
    UObject* context = *reinterpret_cast<UObject**>(
        reinterpret_cast<uint8_t*>(construct) + 0x03D0);
    if (!IsObjectValid(context))
    {
        Output::send<LogLevel::Verbose>(STR("[CopyAngle] No ConstructionContext\n"));
        return;
    }

    g_copyAngleTarget = { quat.GetX(), quat.GetY(), quat.GetZ(), quat.GetW(), rot.Pitch, rot.Yaw, rot.Roll };
    g_copyAngleHold   = true;
    Output::send<LogLevel::Verbose>(
        STR("[CopyAngle] Locking rotation to {} (pitch={:.1f} yaw={:.1f} roll={:.1f}) quat=({:.3f},{:.3f},{:.3f},{:.3f})\n"),
        best->GetName(), rot.Pitch, rot.Yaw, rot.Roll,
        g_copyAngleTarget.X, g_copyAngleTarget.Y, g_copyAngleTarget.Z, g_copyAngleTarget.W);
    // First immediate application with diagnostic logging (before/after readback).
    ApplyBrushQuaternion(context, g_copyAngleTarget.X, g_copyAngleTarget.Y,
                         g_copyAngleTarget.Z, g_copyAngleTarget.W, true);
}

// ──────────────────────────────────────────────────────────────────────────────
// Looked-at block rotation cache (throttled trace from PostTick)
// ──────────────────────────────────────────────────────────────────────────────

static UObject* TraceForBlockQuiet(UObject* worldCtx,
                                   double cx, double cy, double cz,
                                   double dx, double dy, double dz)
{
    static UFunction* fn = nullptr;
    static int32_t oWctx=-1, oStart=-1, oEnd=-1, oChannel=-1,
                   oComplex=-1, oIgnoreSelf=-1, oDraw=-1, oOutHit=-1, oRet=-1;
    if (!fn)
    {
        UObject* fnObj = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:LineTraceSingle"));
        fn = reinterpret_cast<UFunction*>(fnObj);
        if (!fn) return nullptr;
        for (auto* prop : fn->ForEachProperty())
        {
            auto nm = prop->GetName(); int32_t o = prop->GetOffset_Internal();
            if      (nm == STR("WorldContextObject")) oWctx = o;
            else if (nm == STR("Start"))         oStart = o;
            else if (nm == STR("End"))           oEnd = o;
            else if (nm == STR("TraceChannel"))  oChannel = o;
            else if (nm == STR("bTraceComplex")) oComplex = o;
            else if (nm == STR("bIgnoreSelf"))   oIgnoreSelf = o;
            else if (nm == STR("DrawDebugType")) oDraw = o;
            else if (nm == STR("OutHit"))        oOutHit = o;
            else if (nm == STR("ReturnValue"))   oRet = o;
        }
    }
    if (oOutHit < 0 || oStart < 0 || oEnd < 0 || oWctx < 0) return nullptr;

    constexpr double REACH = 5000.0;
    double ex = cx + dx * REACH, ey = cy + dy * REACH, ez = cz + dz * REACH;

    alignas(16) uint8_t buf[0x400] = {};
    *reinterpret_cast<UObject**>(buf + oWctx) = worldCtx;
    { auto* v = reinterpret_cast<double*>(buf + oStart); v[0]=cx; v[1]=cy; v[2]=cz; }
    { auto* v = reinterpret_cast<double*>(buf + oEnd);   v[0]=ex; v[1]=ey; v[2]=ez; }
    buf[oChannel] = 0;
    if (oComplex    >= 0) buf[oComplex]    = 0;
    if (oIgnoreSelf >= 0) buf[oIgnoreSelf] = 1;
    if (oDraw       >= 0) buf[oDraw]       = 0;

    worldCtx->ProcessEvent(fn, buf);

    bool hit = (oRet >= 0) && buf[oRet] != 0;
    if (!hit) return nullptr;

    uint8_t* hr = buf + oOutHit;
    UObject* comp  = reinterpret_cast<FWeakObjectPtr*>(hr + 0xD8)->Get();
    UObject* actor = nullptr;
    if (comp && IsObjectValid(comp))
        actor = comp->GetTypedOuter<AActor>();
    if (!actor)
        actor = reinterpret_cast<FWeakObjectPtr*>(hr + 0xB8)->Get();
    if (!actor || !IsObjectValid(actor)) return nullptr;

    UClass* blockClass = GetBlockClass();
    if (blockClass && !actor->IsA(blockClass)) return nullptr;
    return actor;
}

void UpdateLookAtData()
{
    if (!g_inBuildVisual.load() || !g_cfg.bstatEnabled) { g_lookAtData.valid = false; return; }
    if (++g_lookAtData.frameSkip < 12) return; // ~5 Hz at 60fps
    g_lookAtData.frameSkip = 0;

    UObject* pc = GetPlayerController();
    if (!IsObjectValid(pc)) { g_lookAtData.valid = false; return; }

    UFunction* vpFn = pc->GetFunctionByNameInChain(STR("GetPlayerViewPoint"));
    if (!vpFn) { g_lookAtData.valid = false; return; }

    GetViewPointParams vp{};
    pc->ProcessEvent(vpFn, &vp);

    double pitch = vp.RotPitch * EYE_PI / 180.0;
    double yaw   = vp.RotYaw   * EYE_PI / 180.0;
    double cp    = std::cos(pitch);

    UObject* block = TraceForBlockQuiet(pc, vp.LocX, vp.LocY, vp.LocZ,
        cp * std::cos(yaw), cp * std::sin(yaw), std::sin(pitch));

    if (!block) { g_lookAtData.valid = false; return; }

    UFunction* rotFn = block->GetFunctionByNameInChain(STR("K2_GetActorRotation"));
    if (!rotFn) { g_lookAtData.valid = false; return; }

    struct { double Pitch, Yaw, Roll; } rot{};
    block->ProcessEvent(rotFn, &rot);

    g_lookAtData.yaw   = rot.Yaw;

    UObject* item = *reinterpret_cast<UObject**>(
        reinterpret_cast<uint8_t*>(block) + 0x0328);
    if (item && IsObjectValid(item))
    {
        auto wname = item->GetName();
        g_lookAtData.name.clear();
        g_lookAtData.name.reserve(wname.size());
        for (auto wc : wname)
            g_lookAtData.name.push_back(static_cast<char>(wc < 128 ? wc : '?'));
    }
    else
    {
        g_lookAtData.name.clear();
    }

    g_lookAtData.valid  = true;
}
