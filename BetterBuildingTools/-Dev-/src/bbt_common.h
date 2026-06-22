#pragma once

#include <Mod/CppUserModBase.hpp>
#include <UE4SSProgram.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/Rotator.hpp>
#include <Unreal/Quat.hpp>
#include <Unreal/NameTypes.hpp>
#include <Input/Handler.hpp>
#include <Input/KeyDef.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <LuaMadeSimple/LuaMadeSimple.hpp>
extern "C" {
#include <lua.h>
}
#include <windows.h>
#include <deque>
#include <mutex>
#include <vector>
#include <set>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <fstream>
#include <string>
#include <cctype>
#include <cstring>

using namespace RC;
using namespace RC::Unreal;
using namespace RC::Input;

static constexpr double EYE_PI = 3.14159265358979323846;

// UE TArray header (UE5 64-bit)
struct FTArray
{
    uint8_t* Data;
    int32_t  Num;
    int32_t  Max;
};

// GetPlayerViewPoint out-params
#pragma pack(push, 1)
struct GetViewPointParams
{
    double LocX, LocY, LocZ;
    double RotPitch, RotYaw, RotRoll;
};
#pragma pack(pop)

// ──────────────────────────────────────────────────────────────────────────────
// Config
// ──────────────────────────────────────────────────────────────────────────────
struct BBTConfig
{
    std::atomic<bool> copyObjEnabled{true};
    Key               copyObjKey     = Key::C;
    bool              copyObjShift   = true;
    std::atomic<bool> copyAngleEnabled{true};
    Key               copyAngleKey  = Key::C;
    bool              copyAngleAlt  = true;
    std::atomic<bool> undoEnabled{true};
    Key               undoKey        = Key::Z;
    bool              undoShift      = true;
    std::atomic<int>  undoMaxStack{10};
    std::atomic<bool> freeNoCost{false};
    std::atomic<bool> freeNoStability{false};
    std::atomic<bool> rotation1Enabled{true};
    std::atomic<bool> rotation5Enabled{true};
    std::atomic<bool> rotation10Enabled{true};
    std::atomic<bool> bstatEnabled{true};
    std::atomic<bool> placementAllowUnderRoof{false};
    std::atomic<bool> placementNoRoofRequired{false};
    std::atomic<bool> placementNoBonfire{false};
    std::atomic<bool> placementNoBellShore{false};
    std::atomic<int>  uiMenuScale{100};
    std::atomic<int>  uiBStatScale{100};
};
extern BBTConfig g_cfg;

// ──────────────────────────────────────────────────────────────────────────────
// Shared globals
// ──────────────────────────────────────────────────────────────────────────────
extern std::atomic<bool> g_reqUndo;
extern std::atomic<bool> g_reqCopyObj;
extern std::atomic<bool> g_reqMatchAngle;

extern UObject* g_cachedPC;
extern UObject* g_cachedConstruct;
extern bool     g_inBuildPrev;

// Undo stack (shared between undo and bridge)
extern std::deque<UObject*> g_UndoStack;
extern std::mutex           g_StackMutex;

// Copy angle hold state (shared between copy and bridge)
struct BBTQuat { double X, Y, Z, W; double RotPitch, RotYaw, RotRoll; };
extern BBTQuat           g_copyAngleTarget;
extern std::atomic<bool> g_copyAngleHold;

// Look-at data (shared between copy and bridge)
struct LookAtData {
    double yaw{0.0};
    std::string name;
    bool   valid{false};
    int    frameSkip{0};
};
extern LookAtData g_lookAtData;

// ──────────────────────────────────────────────────────────────────────────────
// Shared helpers
// ──────────────────────────────────────────────────────────────────────────────
bool IsObjectValid(UObject* obj);
UClass* GetBlockClass();
UObject* GetPlayerController();
UObject* GetConstructAbility();
UObject* GetCurrentBrushItem();
std::wstring BBT_ModFolder();
char BBT_KeyChar(Key k);

std::string KeyToString(Key k, bool shift, bool alt);

// Config I/O
void BBT_LoadConfig();
void BBT_SaveConfig();

// ──────────────────────────────────────────────────────────────────────────────
// Feature functions (each in their own .cpp)
// ──────────────────────────────────────────────────────────────────────────────

// bbt_undo.cpp
void EnsureUndoInfrastructure();
void TryUndo();

// bbt_copy.cpp
UObject* TraceForBlock(UObject* worldCtx, double cx, double cy, double cz,
                       double dx, double dy, double dz);
void TryCopyObject();
void TryMatchAngle();
void SyncMatchAngle();
void UpdateLookAtData();

// bbt_placement.cpp
void SyncFreeBuild();
void SyncStability();
void SyncPlacementFreedom();
void PlacementCleanup();
void StabilityCleanup();
void FreeBuildCleanup();

// bbt_rotation.cpp
bool BBT_AnyFineStepEnabled();
void EnsureRotationHook();
std::vector<int32_t> BuildRotationCycle();

// bbt_bridge.cpp
int lua_BBT_GetBuildStatus(lua_State* L);
int lua_BBT_GetConfig(lua_State* L);
int lua_BBT_SetConfig(lua_State* L);
int lua_BBT_SaveConfig(lua_State* L);
int lua_BuildingUndo_Push(lua_State* L);
int lua_BuildingUndo_Clear(lua_State* L);
