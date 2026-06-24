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

void CacheFreeBuildTargets()
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

void CacheValidationProfiles()
{
    if (!g_validationProfiles.empty()) return;
    UObjectGlobals::FindAllOf(STR("R5BuildValidationProfile"), g_validationProfiles);
    if (!g_validationProfiles.empty())
        Output::send<LogLevel::Verbose>(
            STR("[BBT] Validation profiles found (count={})\n"), g_validationProfiles.size());
}

// Validation profile restriction bypass: patch RestrictionType to 0xFF (invalid — game skips it).
// Type 0 = BuildingCenterRequirements (bonfire), Type 5 = CoastlineDistance (shore).
// Fully reversible: restore original type byte to re-enable the restriction.
struct RestrictionPatch
{
    uint8_t* typeByte;
    uint8_t  origType;
};
static std::vector<RestrictionPatch> g_bonfireRestrictionPatches;
static std::vector<RestrictionPatch> g_shoreRestrictionPatches;
static bool g_decorPatchesCached = false;

void CacheDecorPatches()
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
                g_bonfireRestrictionPatches.push_back({typePtr, typeVal});
            else if (typeVal == 5) // CoastlineDistance
                g_shoreRestrictionPatches.push_back({typePtr, typeVal});
        }
    }
    g_decorPatchesCached = true;
    Output::send<LogLevel::Verbose>(
        STR("[BBT] Restriction patches cached: {} profiles, {} bonfire, {} shore\n"),
        profilesScanned, g_bonfireRestrictionPatches.size(), g_shoreRestrictionPatches.size());
}

static void SyncBonfireRestrictions(bool bypass)
{
    for (auto& p : g_bonfireRestrictionPatches)
        *p.typeByte = bypass ? 0xFF : p.origType;
}

static void SyncShoreRestrictions(bool bypass)
{
    for (auto& p : g_shoreRestrictionPatches)
        *p.typeByte = bypass ? 0xFF : p.origType;
}

void SyncFreeBuild()
{
    CacheFreeBuildTargets();
    if (!g_freeTargetsCached) return;

    bool noCost = g_cfg.freeNoCost;
    if (noCost != g_prevNoCost)
    {
        for (auto& t : g_freeTargets)
            *t.costByte = noCost ? 0 : t.origCost;
        Output::send<LogLevel::Verbose>(STR("[BBT] Free-build → {}\n"), noCost);
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

void CacheStabilityProfile()
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

    // Part 1: restriction profile — toggle-only, safe
    if (noStability != g_prevNoStability)
    {
        if (g_stabilityTypePtr)
            *g_stabilityTypePtr = noStability ? RESTRICTION_TYPE_DISABLED : g_stabilityOrigType;
    }

    // Part 2: single-block IntegritySettings — MUST stay per-tick (tracks selected brush item)
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
static bool g_craftBonfireApplied = false;

// Global bRequiresBuildingCenter bypass: find all items with the flag at cache time,
// batch flip on toggle. No per-tick per-item cost.
static constexpr int REQUIRES_BUILDING_CENTER_OFFSET = 0x0561;

struct BonfireItemTarget
{
    uint8_t* flagPtr;
    uint8_t  origFlag;
};
static std::vector<BonfireItemTarget> g_bonfireItemTargets;
static bool g_bonfireItemsCached = false;

void CacheBonfireItems()
{
    if (g_bonfireItemsCached) return;

    std::vector<UObject*> items;
    UObjectGlobals::FindAllOf(STR("R5BuildingItem"), items);

    for (UObject* item : items)
    {
        if (!IsObjectValid(item)) continue;
        uint8_t* flagPtr = reinterpret_cast<uint8_t*>(item) + REQUIRES_BUILDING_CENTER_OFFSET;
        if (*flagPtr != 0)
            g_bonfireItemTargets.push_back({flagPtr, *flagPtr});
    }

    if (g_bonfireItemTargets.empty()) return;

    g_bonfireItemsCached = true;
    Output::send<LogLevel::Verbose>(
        STR("[BBT] Bonfire item targets cached (count={})\n"), g_bonfireItemTargets.size());
}

// Global CoastlineDistanceRange bypass: find all items with negative LowerBound.Value
// at cache time, batch widen to [-1M, +1M] on toggle, restore on toggle off.
//
// FFloatRangeBound layout (VERIFIED from CXX headers):
//   uint8 Type  @ +0x00  (ERangeBoundTypes: Exclusive=0, Inclusive=1, Open=2)
//   pad[3]      @ +0x01
//   float Value @ +0x04
// FFloatRange = LowerBound(0x08) + UpperBound(0x08) = 0x10 total
static constexpr int COASTLINE_RANGE_OFFSET = 0x020C;
static constexpr int COASTLINE_RANGE_SIZE   = 0x10;
static constexpr int BOUND_VALUE_OFFSET     = 0x04; // Value within FFloatRangeBound

struct ShoreItemTarget
{
    uint8_t* rangePtr;
    uint8_t  origRange[0x10];
};
static std::vector<ShoreItemTarget> g_shoreItemTargets;
static bool g_shoreItemsCached = false;

static bool IsNearShoreItem(uint8_t* rangePtr)
{
    float lowerValue;
    std::memcpy(&lowerValue, rangePtr + BOUND_VALUE_OFFSET, sizeof(float));
    return lowerValue < 0.0f;
}

void CacheShoreItems()
{
    if (g_shoreItemsCached) return;

    std::vector<UObject*> items;
    UObjectGlobals::FindAllOf(STR("R5BuildingItem"), items);

    for (UObject* item : items)
    {
        if (!IsObjectValid(item)) continue;
        uint8_t* rangePtr = reinterpret_cast<uint8_t*>(item) + COASTLINE_RANGE_OFFSET;
        if (IsNearShoreItem(rangePtr))
        {
            ShoreItemTarget t{};
            t.rangePtr = rangePtr;
            std::memcpy(t.origRange, rangePtr, COASTLINE_RANGE_SIZE);
            g_shoreItemTargets.push_back(t);
        }
    }

    if (g_shoreItemTargets.empty()) return;

    g_shoreItemsCached = true;
    Output::send<LogLevel::Verbose>(
        STR("[BBT] Shore item targets cached (count={})\n"), g_shoreItemTargets.size());
}

static void ShorePatchAll()
{
    uint8_t exclusive = 0; // ERangeBoundTypes::Exclusive
    float lo = -1000000.0f;
    float hi =  1000000.0f;
    for (auto& t : g_shoreItemTargets)
    {
        // LowerBound: Type@+0, Value@+4
        std::memcpy(t.rangePtr + 0x00, &exclusive, 1);
        std::memcpy(t.rangePtr + 0x04, &lo, 4);
        // UpperBound: Type@+8, Value@+C
        std::memcpy(t.rangePtr + 0x08, &exclusive, 1);
        std::memcpy(t.rangePtr + 0x0C, &hi, 4);
    }
}

static void ShoreRestoreAll()
{
    for (auto& t : g_shoreItemTargets)
        std::memcpy(t.rangePtr, t.origRange, COASTLINE_RANGE_SIZE);
}

static UObject* FindShelterCheckSetup(const TCHAR* path)
{
    UObject* obj = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, path);
    if (obj && IsObjectValid(obj)) return obj;
    return nullptr;
}

void CachePlacementTargets()
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

void CacheBonfireTargets()
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

void CacheCraftBonfireTargets()
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
    // Cache calls are idempotent — run every tick to pick up late-loading assets.
    CachePlacementTargets();
    CacheBonfireTargets();
    CacheCraftBonfireTargets();
    CacheValidationProfiles();
    CacheDecorPatches();
    CacheBonfireItems();
    CacheShoreItems();

    bool allowUnderRoof = g_cfg.placementAllowUnderRoof;
    bool noRoofRequired = g_cfg.placementNoRoofRequired;
    bool noBonfire      = g_cfg.placementNoBonfire;
    bool noBellShore    = g_cfg.placementNoBellShore;

    // Only patch on toggle change — zero per-tick cost when toggles are stable.

    if (allowUnderRoof != g_prevAllowUnderRoof)
    {
        if (g_placeFurnace.thresholdPtr)
            *g_placeFurnace.thresholdPtr = allowUnderRoof ? 2.0f : g_placeFurnace.origThreshold;
        if (g_placeKiln.thresholdPtr)
            *g_placeKiln.thresholdPtr = allowUnderRoof ? 2.0f : g_placeKiln.origThreshold;
        g_prevAllowUnderRoof = allowUnderRoof;
        Output::send<LogLevel::Verbose>(STR("[BBT] Allow Under Roof → {}\n"), allowUnderRoof);
    }

    if (noRoofRequired != g_prevNoRoofRequired)
    {
        if (g_placeRoofReq.mandatoryDataPtr)
            *g_placeRoofReq.mandatoryDataPtr = noRoofRequired ? nullptr : g_placeRoofReq.origMandatoryData;
        g_prevNoRoofRequired = noRoofRequired;
        Output::send<LogLevel::Verbose>(STR("[BBT] No Roof Required → {}\n"), noRoofRequired);
    }

    if (noBonfire != g_prevNoBonfire)
    {
        if (g_bonfireCached)
            for (auto& t : g_bonfireTargets)
                *t.anywhereByte = noBonfire ? 1 : t.origAnywhere;
        if (g_decorPatchesCached)
            SyncBonfireRestrictions(noBonfire);
        if (g_bonfireItemsCached)
            for (auto& t : g_bonfireItemTargets)
                *t.flagPtr = noBonfire ? 0 : t.origFlag;
        g_prevNoBonfire = noBonfire;
        Output::send<LogLevel::Verbose>(STR("[BBT] No Bonfire → {}\n"), noBonfire);
    }

    // Crafting interaction bonfire bypass — null permanently once cached.
    // Never restore: benches placed outside bonfire range would crash on interact if restored.
    // One-shot: apply once after caching, then skip on all subsequent ticks.
    if (g_craftBonfireCached && !g_craftBonfireApplied)
    {
        for (auto& t : g_craftBonfireTargets)
            *t.requirementPtr = nullptr;
        g_craftBonfireApplied = true;
        Output::send<LogLevel::Verbose>(STR("[BBT] Craft bonfire requirements nulled (one-shot)\n"));
    }

    if (noBellShore != g_prevNoBellShore)
    {
        if (g_decorPatchesCached)
            SyncShoreRestrictions(noBellShore);
        if (g_shoreItemsCached)
        {
            if (noBellShore) ShorePatchAll();
            else             ShoreRestoreAll();
        }
        g_prevNoBellShore = noBellShore;
        Output::send<LogLevel::Verbose>(STR("[BBT] No Shore → {}\n"), noBellShore);
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
        g_bonfireRestrictionPatches.clear();
        g_shoreRestrictionPatches.clear();
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
        g_craftBonfireApplied = false;
    }
    if (g_bonfireItemsCached) {
        for (auto& t : g_bonfireItemTargets)
            *t.flagPtr = t.origFlag;
        g_bonfireItemTargets.clear();
        g_bonfireItemsCached = false;
    }
    if (g_shoreItemsCached) {
        ShoreRestoreAll();
        g_shoreItemTargets.clear();
        g_shoreItemsCached = false;
    }
}
