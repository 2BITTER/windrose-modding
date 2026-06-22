#include "bbt_common.h"

// ──────────────────────────────────────────────────────────────────────────────
// Free Build + Stability + Placement Freedom
// ──────────────────────────────────────────────────────────────────────────────

// Free-build: continuously mirrors g_cfg.freeNoCost into R5BuildingSettings
// (CDO + instances), so the ImGui checkbox is fully live and reversible.
//   bBuildingResourcesValidation @ 0x051C → false  = build with no resource cost
// Original byte values are captured once so turning a toggle back off restores
// the game's real default instead of guessing at it.
struct FreeBuildTarget
{
    uint8_t* costByte;
    uint8_t  origCost;
};
static std::vector<FreeBuildTarget> g_freeTargets;
static std::vector<UObject*>        g_validationProfiles;
static bool g_freeTargetsCached = false;
static bool g_prevNoCost = false;

static void CacheFreeBuildTargets()
{
    if (g_freeTargetsCached) return;

    bool any = false;
    auto addTarget = [&](UObject* s)
    {
        uint8_t* b = reinterpret_cast<uint8_t*>(s);
        FreeBuildTarget t{};
        t.costByte     = b + 0x051C;
        t.origCost     = *t.costByte;
        g_freeTargets.push_back(t);
        any = true;
    };
    UObject* cdo = UObjectGlobals::StaticFindObject<UObject*>(
        nullptr, nullptr, STR("/Script/R5.Default__R5BuildingSettings"));
    if (IsObjectValid(cdo)) addTarget(cdo);
    std::vector<UObject*> inst;
    UObjectGlobals::FindAllOf(STR("R5BuildingSettings"), inst);
    for (UObject* s : inst) if (IsObjectValid(s)) addTarget(s);
    if (!any) return; // not loaded yet — retry next tick

    g_freeTargetsCached = true;
    Output::send<LogLevel::Verbose>(
        STR("[BBT] Free-build targets cached (settings={})\n"), g_freeTargets.size());
}

static void CacheValidationProfiles()
{
    if (!g_validationProfiles.empty()) return;
    UObjectGlobals::FindAllOf(STR("R5BuildValidationProfile"), g_validationProfiles);
    if (!g_validationProfiles.empty())
        Output::send<LogLevel::Verbose>(
            STR("[BBT] Validation profiles found (count={})\n"), g_validationProfiles.size());
}

// Decoration restriction bypass: patch RestrictionType from 0 (BuildingCenterRequirements)
// to 0xFF (invalid — game skips it) in each UR5BuildValidationProfile.Restrictions entry.
// Fully reversible: set back to 0 to restore the restriction.
struct DecorRestrictionPatch
{
    uint8_t* typeByte;
};
static std::vector<DecorRestrictionPatch> g_decorPatches;
static bool g_decorPatchesCached = false;

static void CacheDecorPatches()
{
    if (g_decorPatchesCached) return;
    if (g_validationProfiles.empty()) return;

    constexpr int ELEM = 0x1C;
    int profilesScanned = 0;
    for (UObject* p : g_validationProfiles)
    {
        if (!IsObjectValid(p)) continue;
        uint8_t* pb   = reinterpret_cast<uint8_t*>(p);
        uint8_t* data = *reinterpret_cast<uint8_t**>(pb + 0x0030);
        int32_t  num  = *reinterpret_cast<int32_t*>(pb + 0x0038);
        if (!data || num <= 0) continue;
        ++profilesScanned;
        for (int32_t i = 0; i < num; ++i)
        {
            uint8_t* typePtr = data + i * ELEM;
            uint8_t  typeVal = *typePtr;
            if (typeVal == 0) // BuildingCenterRequirements
            {
                g_decorPatches.push_back({typePtr});
            }
        }
    }
    g_decorPatchesCached = true;
    Output::send<LogLevel::Verbose>(
        STR("[BBT] Decor patches cached: {} profiles total, {} scanned, {} patches\n"),
        g_validationProfiles.size(), profilesScanned, g_decorPatches.size());
}

static void SyncDecorRestrictions(bool bypass)
{
    for (auto& p : g_decorPatches)
        *p.typeByte = bypass ? 0xFF : 0;
}

void SyncFreeBuild()
{
    CacheFreeBuildTargets();
    if (!g_freeTargetsCached) return;

    bool noCost = g_cfg.freeNoCost;

    for (auto& t : g_freeTargets)
        *t.costByte = noCost ? 0 : t.origCost;

    if (noCost != g_prevNoCost)
    {
        Output::send<LogLevel::Verbose>(STR("[BBT] Free-build synced (no_cost={})\n"), noCost);
        g_prevNoCost = noCost;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// No stability — single-block patching
// ──────────────────────────────────────────────────────────────────────────────
// Two systems enforce stability:
//   1) DA_BuildRestrictionParams has a "Stability" restriction entry — we flip
//      its RestrictionType to 0xFF to skip the placement validation check.
//   2) Each R5BuildingItem has IntegritySettings (weight/loads) used by the
//      BuildingGraph for structural calculations. We patch ONLY the currently
//      selected brush item, not all ~860 items. The graph saves patched values
//      on placement, so placed blocks stay stable permanently.
//
// Old approach patched all items globally, which poisoned the BuildingGraph
// for every connected structure — toggling NSR off caused entire buildings to
// collapse. Single-block patching avoids this entirely.

static constexpr uint8_t RESTRICTION_TYPE_COASTLINE  = 5;
static constexpr uint8_t RESTRICTION_TYPE_STABILITY  = 8;
static constexpr uint8_t RESTRICTION_TYPE_DISABLED   = 0xFF;
static constexpr int     VALIDATION_RESTRICTIONS_OFFSET = 0x0030;
static constexpr int     RESTRICTION_ENTRY_SIZE = 0x1C;
static constexpr int     INTEGRITY_OFFSET = 0x04BC;
static constexpr float   STABILITY_MAX_LOAD = 10000000.0f;

static bool     g_stabilityProfileCached = false;
static bool     g_prevNoStability        = false;
static uint8_t* g_stabilityTypePtr       = nullptr;
static uint8_t  g_stabilityOrigType      = RESTRICTION_TYPE_STABILITY;

static bool     g_prevNoBellShore        = false;

static UObject* g_nsrPatchedItem = nullptr;
static float    g_nsrOrigWeight  = 0.0f;
static float    g_nsrOrigHLoad   = 0.0f;
static float    g_nsrOrigVLoad   = 0.0f;
static float    g_nsrOrigMinExt  = 0.0f;

static void CacheStabilityProfile()
{
    if (g_stabilityProfileCached) return;

    std::vector<UObject*> profiles;
    UObjectGlobals::FindAllOf(STR("R5BuildValidationProfile"), profiles);
    for (UObject* prof : profiles)
    {
        if (!IsObjectValid(prof)) continue;
        File::StringType path = prof->GetPathName();
        if (path.find(STR("DA_BuildRestrictionParams")) == File::StringType::npos) continue;

        uint8_t* base = reinterpret_cast<uint8_t*>(prof);
        uint8_t* arrPtr = base + VALIDATION_RESTRICTIONS_OFFSET;
        uint8_t* data = *reinterpret_cast<uint8_t**>(arrPtr);
        int32_t  num  = *reinterpret_cast<int32_t*>(arrPtr + 8);

        for (int32_t i = 0; i < num; i++)
        {
            uint8_t* entry = data + (i * RESTRICTION_ENTRY_SIZE);
            if (*entry == RESTRICTION_TYPE_STABILITY && !g_stabilityTypePtr)
            {
                g_stabilityTypePtr  = entry;
                g_stabilityOrigType = *entry;
                Output::send<LogLevel::Verbose>(
                    STR("[BBT] Stability restriction found in profile (entry {}/{})\n"), i, num);
            }
        }
        break;
    }

    g_stabilityProfileCached = true;
    Output::send<LogLevel::Verbose>(STR("[BBT] Stability profile cached (found={})\n"),
        g_stabilityTypePtr ? STR("YES") : STR("NO"));
}

static void NsrRestoreItem()
{
    if (!g_nsrPatchedItem) return;
    if (!IsObjectValid(g_nsrPatchedItem)) { g_nsrPatchedItem = nullptr; return; }

    uint8_t* b = reinterpret_cast<uint8_t*>(g_nsrPatchedItem) + INTEGRITY_OFFSET;
    *reinterpret_cast<float*>(b + 0x00) = g_nsrOrigWeight;
    *reinterpret_cast<float*>(b + 0x04) = g_nsrOrigHLoad;
    *reinterpret_cast<float*>(b + 0x08) = g_nsrOrigVLoad;
    *reinterpret_cast<float*>(b + 0x0C) = g_nsrOrigMinExt;

    Output::send<LogLevel::Verbose>(STR("[BBT] NSR restored: {}\n"), g_nsrPatchedItem->GetName());
    g_nsrPatchedItem = nullptr;
}

static void NsrPatchItem(UObject* item)
{
    if (!item || !IsObjectValid(item)) return;

    uint8_t* b = reinterpret_cast<uint8_t*>(item) + INTEGRITY_OFFSET;
    g_nsrOrigWeight = *reinterpret_cast<float*>(b + 0x00);
    g_nsrOrigHLoad  = *reinterpret_cast<float*>(b + 0x04);
    g_nsrOrigVLoad  = *reinterpret_cast<float*>(b + 0x08);
    g_nsrOrigMinExt = *reinterpret_cast<float*>(b + 0x0C);

    *reinterpret_cast<float*>(b + 0x00) = 0.0f;
    *reinterpret_cast<float*>(b + 0x04) = STABILITY_MAX_LOAD;
    *reinterpret_cast<float*>(b + 0x08) = STABILITY_MAX_LOAD;
    *reinterpret_cast<float*>(b + 0x0C) = 0.0f;

    g_nsrPatchedItem = item;
    Output::send<LogLevel::Verbose>(STR("[BBT] NSR patched: {} (w={:.1f} h={:.1f} v={:.1f})\n"),
        item->GetName(), g_nsrOrigWeight, g_nsrOrigHLoad, g_nsrOrigVLoad);
}

void SyncStability()
{
    CacheStabilityProfile();

    bool noStability = g_cfg.freeNoStability;

    // Part 1: restriction profile — global toggle, safe
    if (g_stabilityTypePtr)
        *g_stabilityTypePtr = noStability ? RESTRICTION_TYPE_DISABLED : g_stabilityOrigType;

    // Part 2: single-block IntegritySettings
    if (!noStability)
    {
        NsrRestoreItem();
    }
    else
    {
        UObject* currentItem = GetCurrentBrushItem();
        if (currentItem != g_nsrPatchedItem)
        {
            NsrRestoreItem();
            NsrPatchItem(currentItem);
        }
    }

    if (noStability != g_prevNoStability)
    {
        Output::send<LogLevel::Verbose>(STR("[BBT] Stability {} (single-block mode)\n"),
            noStability ? STR("DISABLED") : STR("RESTORED"));
        g_prevNoStability = noStability;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Placement Freedom: shelter/roof/bonfire bypass
// ──────────────────────────────────────────────────────────────────────────────
// Patches UR5ShelterCheckSetup data assets at runtime to bypass roof restrictions.
//   - "Allow Under Roof" (furnace/kiln): set ShelterPercentThreshold > 1.0
//   - "No Roof Required" (workstations/beds): set MandatoryStrategies checker Length to 0
//   - "No Bonfire Required": bSkipBuildingCenterValidation + decoration restriction strip
//
// UR5ShelterCheckSetup layout (from CXX headers):
//   MandatoryStrategies  @ 0x0030  TArray<UObject*>
//   ShelterPercentThreshold @ 0x0040  float
//   ThresholdStrategies  @ 0x0048  TArray<UObject*>
// UR5ShelterChecker (base):
//   Length @ 0x0028  float
struct PlacementTarget
{
    float*    thresholdPtr;    // ShelterPercentThreshold for "not under roof" assets
    float     origThreshold;
    int32_t*  mandatoryNumPtr; // MandatoryStrategies.Num
    int32_t   origMandatoryNum;
    UObject** mandatoryDataPtr; // MandatoryStrategies.Data[0] — null the checker reference (zee's approach)
    UObject*  origMandatoryData;
};
static PlacementTarget g_placeFurnace{};
static PlacementTarget g_placeKiln{};
static PlacementTarget g_placeRoofReq{};
static bool g_placementCached = false;
static bool g_prevAllowUnderRoof = false;
static bool g_prevNoRoofRequired = false;
static bool g_prevNoBonfire      = false;

// Bonfire-only targets: bSkipBuildingCenterValidation on BuildingSettings
// Separate from g_freeTargets so we can toggle bonfire without touching decoration restrictions.
struct BonfireTarget
{
    uint8_t* anywhereByte;
    uint8_t  origAnywhere;
};
static std::vector<BonfireTarget> g_bonfireTargets;
static bool g_bonfireCached = false;

// Crafting interaction bonfire bypass: null out UR5InteractionOption::Requirement @ +0x0060
// on each DA_InteractOption_CraftWithBuildingCenter* asset so workstations don't require bonfire proximity.
struct CraftBonfireTarget
{
    UObject** requirementPtr;
    UObject*  origRequirement;
};
static std::vector<CraftBonfireTarget> g_craftBonfireTargets;
static bool g_craftBonfireCached = false;

// Per-item bRequiresBuildingCenter bypass: single-item patching (same pattern as NSR).
// Patches only the currently selected brush item, restores before switching.
static constexpr int REQUIRES_BUILDING_CENTER_OFFSET = 0x0561;

static UObject* g_bonfirePatchedItem  = nullptr;
static uint8_t  g_bonfireOrigFlag     = 0;

static void BonfireRestoreItem()
{
    if (!g_bonfirePatchedItem) return;
    if (!IsObjectValid(g_bonfirePatchedItem)) { g_bonfirePatchedItem = nullptr; return; }

    uint8_t* flagPtr = reinterpret_cast<uint8_t*>(g_bonfirePatchedItem) + REQUIRES_BUILDING_CENTER_OFFSET;
    *flagPtr = g_bonfireOrigFlag;

    Output::send<LogLevel::Verbose>(STR("[BBT] Bonfire restored: {}\n"), g_bonfirePatchedItem->GetName());
    g_bonfirePatchedItem = nullptr;
}

static void BonfirePatchItem(UObject* item)
{
    if (!item || !IsObjectValid(item)) return;

    uint8_t* flagPtr = reinterpret_cast<uint8_t*>(item) + REQUIRES_BUILDING_CENTER_OFFSET;
    g_bonfireOrigFlag = *flagPtr;
    *flagPtr = 0;

    g_bonfirePatchedItem = item;
    Output::send<LogLevel::Verbose>(STR("[BBT] Bonfire patched: {} (was={})\n"),
        item->GetName(), g_bonfireOrigFlag);
}

// Per-item CoastlineDistanceRange bypass: single-item patching (same pattern as NSR/bonfire).
// FFloatRange at offset 0x020C (size 0x10): LowerBound(float+enum) + UpperBound(float+enum)
// Widen to [-1M, +1M] so any distance from shore passes.
static constexpr int COASTLINE_RANGE_OFFSET = 0x020C;
static constexpr int COASTLINE_RANGE_SIZE   = 0x10;

static UObject* g_shorePatchedItem = nullptr;
static uint8_t  g_shoreOrigRange[0x10] = {};

static void ShoreRestoreItem()
{
    if (!g_shorePatchedItem) return;
    if (!IsObjectValid(g_shorePatchedItem)) { g_shorePatchedItem = nullptr; return; }

    uint8_t* rangePtr = reinterpret_cast<uint8_t*>(g_shorePatchedItem) + COASTLINE_RANGE_OFFSET;
    std::memcpy(rangePtr, g_shoreOrigRange, COASTLINE_RANGE_SIZE);

    Output::send<LogLevel::Verbose>(STR("[BBT] Shore restored: {}\n"), g_shorePatchedItem->GetName());
    g_shorePatchedItem = nullptr;
}

static bool IsNearShoreItem(uint8_t* rangePtr)
{
    // Near-shore items (bells, wharfs, piers) have a NEGATIVE lower bound
    // (e.g. -30000, -10000) meaning "must be within X units of shoreline."
    // Away-from-shore items (tent) have a POSITIVE lower bound (e.g. 200)
    // meaning "must be at least X units from shore." We only bypass near-shore.
    float lowerValue;
    std::memcpy(&lowerValue, rangePtr, sizeof(float));
    return lowerValue < 0.0f;
}

static void ShorePatchItem(UObject* item)
{
    if (!item || !IsObjectValid(item)) return;

    uint8_t* rangePtr = reinterpret_cast<uint8_t*>(item) + COASTLINE_RANGE_OFFSET;

    if (!IsNearShoreItem(rangePtr))
    {
        Output::send<LogLevel::Verbose>(STR("[BBT] Shore skip (not near-shore item): {}\n"), item->GetName());
        return;
    }

    std::memcpy(g_shoreOrigRange, rangePtr, COASTLINE_RANGE_SIZE);

    // FFloatRange: LowerBound(float value, uint8 type) + UpperBound(same)
    // Match QM's approach: Exclusive bounds at [-1M, +1M]
    float lo = -1000000.0f;
    float hi =  1000000.0f;
    uint8_t exclusive = 0; // ERangeBoundTypes::Exclusive
    std::memcpy(rangePtr + 0x00, &lo, 4);
    std::memcpy(rangePtr + 0x04, &exclusive, 1);
    std::memcpy(rangePtr + 0x08, &hi, 4);
    std::memcpy(rangePtr + 0x0C, &exclusive, 1);

    g_shorePatchedItem = item;
    Output::send<LogLevel::Verbose>(STR("[BBT] Shore patched: {} (range → [-1M, +1M])\n"), item->GetName());
}

static UObject* FindShelterCheckSetup(const TCHAR* path)
{
    UObject* obj = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, path);
    if (obj && IsObjectValid(obj)) return obj;
    return nullptr;
}

static void CachePlacementTargets()
{
    if (g_placementCached) return;

    // Cache each target independently — they may load at different times.
    // Don't mark fully cached until ALL are resolved.
    if (!g_placeFurnace.thresholdPtr) {
        UObject* obj = FindShelterCheckSetup(
            STR("/Game/Gameplay/Comfort/Shelter/DA_ShelterCheckSetup_NotUnderRoof_Furnace.DA_ShelterCheckSetup_NotUnderRoof_Furnace"));
        if (obj) {
            uint8_t* b = reinterpret_cast<uint8_t*>(obj);
            g_placeFurnace.thresholdPtr = reinterpret_cast<float*>(b + 0x0040);
            g_placeFurnace.origThreshold = *g_placeFurnace.thresholdPtr;
            Output::send<LogLevel::Verbose>(STR("[BBT] Placement: furnace cached (threshold={})\n"), g_placeFurnace.origThreshold);
        }
    }
    if (!g_placeKiln.thresholdPtr) {
        UObject* obj = FindShelterCheckSetup(
            STR("/Game/Gameplay/Comfort/Shelter/DA_ShelterCheckSetup_NotUnderRoof_Kiln.DA_ShelterCheckSetup_NotUnderRoof_Kiln"));
        if (obj) {
            uint8_t* b = reinterpret_cast<uint8_t*>(obj);
            g_placeKiln.thresholdPtr = reinterpret_cast<float*>(b + 0x0040);
            g_placeKiln.origThreshold = *g_placeKiln.thresholdPtr;
            Output::send<LogLevel::Verbose>(STR("[BBT] Placement: kiln cached (threshold={})\n"), g_placeKiln.origThreshold);
        }
    }
    if (!g_placeRoofReq.mandatoryNumPtr) {
        UObject* obj = FindShelterCheckSetup(
            STR("/Game/Gameplay/Comfort/Shelter/DA_ShelterCheckSetup_RoofRequired.DA_ShelterCheckSetup_RoofRequired"));
        if (obj) {
            uint8_t* b = reinterpret_cast<uint8_t*>(obj);
            g_placeRoofReq.mandatoryNumPtr = reinterpret_cast<int32_t*>(b + 0x0030 + 0x08);
            g_placeRoofReq.origMandatoryNum = *g_placeRoofReq.mandatoryNumPtr;
            // Capture Data[0] pointer — null it to bypass roof check (matches zee's PAK approach)
            uint8_t** dataPtr = reinterpret_cast<uint8_t**>(b + 0x0030);
            if (*dataPtr && g_placeRoofReq.origMandatoryNum > 0) {
                g_placeRoofReq.mandatoryDataPtr = reinterpret_cast<UObject**>(*dataPtr);
                g_placeRoofReq.origMandatoryData = *g_placeRoofReq.mandatoryDataPtr;
            }
            Output::send<LogLevel::Verbose>(STR("[BBT] Placement: roofReq cached (mandatoryNum={}, dataPtr={})\n"),
                g_placeRoofReq.origMandatoryNum, g_placeRoofReq.mandatoryDataPtr ? STR("YES") : STR("NO"));
        }
    }

    bool allFound = g_placeFurnace.thresholdPtr && g_placeKiln.thresholdPtr && g_placeRoofReq.mandatoryNumPtr;
    if (allFound) {
        g_placementCached = true;
        Output::send<LogLevel::Verbose>(STR("[BBT] Placement targets ALL cached\n"));
    }
}

static void CacheBonfireTargets()
{
    if (g_bonfireCached) return;

    auto addTarget = [](UObject* s) {
        uint8_t* b = reinterpret_cast<uint8_t*>(s);
        BonfireTarget t{};
        t.anywhereByte = b + 0x00B9;
        t.origAnywhere = *t.anywhereByte;
        g_bonfireTargets.push_back(t);
    };

    UObject* cdo = UObjectGlobals::StaticFindObject<UObject*>(
        nullptr, nullptr, STR("/Script/R5.Default__R5BuildingSettings"));
    if (!IsObjectValid(cdo)) return;
    addTarget(cdo);

    std::vector<UObject*> inst;
    UObjectGlobals::FindAllOf(STR("R5BuildingSettings"), inst);
    for (UObject* s : inst)
        if (IsObjectValid(s) && s != cdo) addTarget(s);

    g_bonfireCached = true;
    Output::send<LogLevel::Verbose>(
        STR("[BBT] Bonfire targets cached (count={})\n"), g_bonfireTargets.size());
}

static void CacheCraftBonfireTargets()
{
    if (g_craftBonfireCached) return;

    static const TCHAR* paths[] = {
        STR("/Game/Gameplay/Interaction/Options/DA_InteractOption_CraftWithBuildingCenter.DA_InteractOption_CraftWithBuildingCenter"),
        STR("/Game/Gameplay/Interaction/Options/DA_InteractOption_CraftWithBuildingCenter_Equipment.DA_InteractOption_CraftWithBuildingCenter_Equipment"),
        STR("/Game/Gameplay/Interaction/Options/DA_InteractOption_CraftWithBuildingCenter_Gear.DA_InteractOption_CraftWithBuildingCenter_Gear"),
        STR("/Game/Gameplay/Interaction/Options/DA_InteractOption_CraftWithBuildingCenter_Production.DA_InteractOption_CraftWithBuildingCenter_Production"),
        STR("/Game/Gameplay/Interaction/Options/DA_InteractOption_Disassemble.DA_InteractOption_Disassemble"),
    };

    bool allFound = true;
    for (auto* path : paths)
    {
        UObject* obj = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, path);
        if (!obj || !IsObjectValid(obj)) { allFound = false; continue; }

        uint8_t* b = reinterpret_cast<uint8_t*>(obj);
        CraftBonfireTarget t{};
        t.requirementPtr = reinterpret_cast<UObject**>(b + 0x0060);
        t.origRequirement = *t.requirementPtr;
        g_craftBonfireTargets.push_back(t);
    }

    if (!allFound && g_craftBonfireTargets.empty()) return;

    g_craftBonfireCached = true;
    Output::send<LogLevel::Verbose>(
        STR("[BBT] Craft bonfire targets cached (count={})\n"), g_craftBonfireTargets.size());
}

void SyncPlacementFreedom()
{
    CachePlacementTargets();
    CacheBonfireTargets();
    CacheCraftBonfireTargets();
    bool allowUnderRoof = g_cfg.placementAllowUnderRoof;
    bool noRoofRequired = g_cfg.placementNoRoofRequired;
    bool noBonfire      = g_cfg.placementNoBonfire;
    bool noBellShore    = g_cfg.placementNoBellShore;

    // Apply patches for whichever targets have been cached so far (they load independently)
    if (g_placeFurnace.thresholdPtr)
        *g_placeFurnace.thresholdPtr = allowUnderRoof ? 2.0f : g_placeFurnace.origThreshold;
    if (g_placeKiln.thresholdPtr)
        *g_placeKiln.thresholdPtr = allowUnderRoof ? 2.0f : g_placeKiln.origThreshold;
    if (g_placeRoofReq.mandatoryDataPtr)
        *g_placeRoofReq.mandatoryDataPtr = noRoofRequired ? nullptr : g_placeRoofReq.origMandatoryData;

    // No Bonfire: set bSkipBuildingCenterValidation AND strip decoration restrictions.
    if (g_bonfireCached)
    {
        for (auto& t : g_bonfireTargets)
            *t.anywhereByte = noBonfire ? 1 : t.origAnywhere;
    }
    CacheValidationProfiles();
    CacheDecorPatches();
    if (g_decorPatchesCached) SyncDecorRestrictions(noBonfire);

    // Per-item bonfire bypass: single-item patching (like NSR)
    if (!noBonfire)
    {
        BonfireRestoreItem();
    }
    else
    {
        UObject* currentItem = GetCurrentBrushItem();
        if (currentItem != g_bonfirePatchedItem)
        {
            BonfireRestoreItem();
            BonfirePatchItem(currentItem);
        }
    }

    // Crafting interaction bonfire bypass — null permanently once cached.
    // Never restore: benches placed outside bonfire range would crash on interact if restored.
    if (g_craftBonfireCached)
    {
        for (auto& t : g_craftBonfireTargets)
            *t.requirementPtr = nullptr;
    }

    // Shore restriction bypass: single-item CoastlineDistanceRange patching (like NSR/bonfire)
    if (!noBellShore)
    {
        ShoreRestoreItem();
    }
    else
    {
        UObject* currentItem = GetCurrentBrushItem();
        if (currentItem != g_shorePatchedItem)
        {
            ShoreRestoreItem();
            ShorePatchItem(currentItem);
        }
    }

    if (allowUnderRoof != g_prevAllowUnderRoof || noRoofRequired != g_prevNoRoofRequired ||
        noBonfire != g_prevNoBonfire || noBellShore != g_prevNoBellShore)
    {
        Output::send<LogLevel::Verbose>(
            STR("[BBT] Placement freedom synced (underRoof={}, noRoof={}, noBonfire={}, noBellShore={})\n"),
            allowUnderRoof, noRoofRequired, noBonfire, noBellShore);
        g_prevAllowUnderRoof = allowUnderRoof;
        g_prevNoRoofRequired = noRoofRequired;
        g_prevNoBonfire = noBonfire;
        g_prevNoBellShore = noBellShore;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Cleanup functions called from CheckBuildMode (dllmain.cpp)
// ──────────────────────────────────────────────────────────────────────────────
void FreeBuildCleanup()
{
    if (g_freeTargetsCached) {
        g_freeTargets.clear();
        g_validationProfiles.clear();
        g_freeTargetsCached = false;
        g_prevNoCost = false;
    }
    if (g_decorPatchesCached) {
        g_decorPatches.clear();
        g_decorPatchesCached = false;
    }
}

void StabilityCleanup()
{
    NsrRestoreItem();
    if (g_stabilityProfileCached) {
        if (g_stabilityTypePtr) *g_stabilityTypePtr = g_stabilityOrigType;
        g_stabilityTypePtr = nullptr;
        g_stabilityProfileCached = false;
        g_prevNoStability = false;
    }
}

void PlacementCleanup()
{
    if (g_placementCached) {
        g_placeFurnace = {};
        g_placeKiln = {};
        g_placeRoofReq = {};
        g_placementCached = false;
        g_prevAllowUnderRoof = false;
        g_prevNoRoofRequired = false;
    }
    if (g_bonfireCached) {
        g_bonfireTargets.clear();
        g_bonfireCached = false;
        g_prevNoBonfire = false;
    }
    if (g_craftBonfireCached) {
        g_craftBonfireTargets.clear();
        g_craftBonfireCached = false;
    }
    BonfireRestoreItem();
    ShoreRestoreItem();
}
