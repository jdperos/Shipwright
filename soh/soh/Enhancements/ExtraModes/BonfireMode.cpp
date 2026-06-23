#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/SaveManager.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"

extern PlayState* gPlayState;
}

static constexpr int32_t CVAR_BONFIRE_DEFAULT = 0;
#define CVAR_BONFIRE_NAME CVAR_ENHANCEMENT("BonfireMode")
#define CVAR_BONFIRE_VALUE CVarGetInteger(CVAR_BONFIRE_NAME, CVAR_BONFIRE_DEFAULT)

namespace BonfireMode {

static constexpr int32_t INVALID_SAVE_SECTION_ID = -1;
static constexpr int32_t VERSION = 1;
static constexpr int16_t LINKS_HOUSE_ENTRANCE_INDEX = 187;

struct BonfireData {
    bool isSet = false;
    int16_t entranceIndex = 0;
    int8_t roomIndex = 0;
    Vec3f pos = {};
    int16_t yaw = 0;

    void Clear() {
        *this = {};
    }
};

static BonfireData sBonfire;

static void WriteRespawnData(PlayState* play) {
    RespawnData* respawnData = &gSaveContext.respawn[RESPAWN_MODE_DOWN];

    respawnData->entranceIndex = sBonfire.entranceIndex;
    respawnData->roomIndex = sBonfire.roomIndex;
    respawnData->pos = sBonfire.pos;
    respawnData->yaw = sBonfire.yaw;
    respawnData->playerParams = 0xDFF;
    respawnData->tempSwchFlags = play->actorCtx.flags.tempSwch;
    respawnData->tempCollectFlags = play->actorCtx.flags.tempCollect;
}

// Use when no PlayState is available
static void WriteBonfireToSaveContext() {
    gSaveContext.entranceIndex = sBonfire.entranceIndex;
    gSaveContext.respawnFlag = RESPAWN_MODE_RETURN;

    RespawnData& respawnData = gSaveContext.respawn[RESPAWN_MODE_DOWN];
    respawnData.entranceIndex = sBonfire.entranceIndex;
    respawnData.roomIndex = sBonfire.roomIndex;
    respawnData.pos = sBonfire.pos;
    respawnData.yaw = sBonfire.yaw;
    respawnData.playerParams = 0xDFF;
    respawnData.tempSwchFlags = 0;
    respawnData.tempCollectFlags = 0;
}

static void SaveFile() {
    SaveManager::Instance->SaveFile(gSaveContext.fileNum);
}

static void InitSave(bool isDebug) {
    sBonfire.Clear();
}

static void LoadSave() {
    sBonfire.Clear();
    SaveManager::Instance->LoadData("isSet", sBonfire.isSet, false);

    if (!sBonfire.isSet)
        return;

    SaveManager::Instance->LoadData("entranceIndex", sBonfire.entranceIndex, (int16_t)0);
    SaveManager::Instance->LoadData("roomIndex", sBonfire.roomIndex, (int8_t)0);
    SaveManager::Instance->LoadStruct("pos", []() {
        SaveManager::Instance->LoadData("x", sBonfire.pos.x, 0.0f);
        SaveManager::Instance->LoadData("y", sBonfire.pos.y, 0.0f);
        SaveManager::Instance->LoadData("z", sBonfire.pos.z, 0.0f);
    });
    SaveManager::Instance->LoadData("yaw", sBonfire.yaw, (int16_t)0);
}

static void OnLoadGame(int32_t fileNum) {
    if (!sBonfire.isSet)
        return;
    WriteBonfireToSaveContext();
}

static void Save(SaveContext* saveContext, int sectionID, bool fullSave) {
    SaveManager::Instance->SaveData("isSet", sBonfire.isSet);

    if (!sBonfire.isSet)
        return;

    SaveManager::Instance->SaveData("entranceIndex", sBonfire.entranceIndex);
    SaveManager::Instance->SaveData("roomIndex", sBonfire.roomIndex);
    SaveManager::Instance->SaveStruct("pos", []() {
        SaveManager::Instance->SaveData("x", sBonfire.pos.x);
        SaveManager::Instance->SaveData("y", sBonfire.pos.y);
        SaveManager::Instance->SaveData("z", sBonfire.pos.z);
    });
    SaveManager::Instance->SaveData("yaw", sBonfire.yaw);
}

static void RegisterSave() {
    static int32_t sSaveSectionId = INVALID_SAVE_SECTION_ID;
    if (sSaveSectionId != INVALID_SAVE_SECTION_ID)
        return;

    SaveManager::Instance->AddInitFunction(InitSave);
    SaveManager::Instance->AddLoadFunction("bonfireMode", VERSION, LoadSave);
    sSaveSectionId = SaveManager::Instance->AddSaveFunction("bonfireMode", VERSION, Save, true, SECTION_PARENT_NONE);
}

static void Activate(uint16_t* textId, bool* loadFromMessageTable) {
    if (gPlayState == nullptr)
        return;

    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr)
        return;

    sBonfire.isSet = true;
    sBonfire.entranceIndex = (int16_t)gSaveContext.entranceIndex;
    sBonfire.roomIndex = (int8_t)gPlayState->roomCtx.curRoom.num;
    sBonfire.pos = player->actor.world.pos;
    sBonfire.yaw = player->actor.shape.rot.y;

    // Push immediately so a void-out on the same frame already uses the bonfire
    WriteRespawnData(gPlayState);

    // Full heal
    const int16_t missing = (int16_t)(gSaveContext.healthCapacity - gSaveContext.health);
    if (missing > 0) {
        gSaveContext.healthAccumulator = missing;
    }

    SaveFile();

    // TODO: redirect *textId to a custom message "This stone is now your respawn point."
    // *textId = BONFIRE_MSG_ID;
    // *loadFromMessageTable = false;
}

// Fires before Play_TriggerVoidOut reads gSaveContext.respawn[RESPAWN_MODE_DOWN]
// We overwrite that slot so the void-out lands at the bonfire
static void OnVoidOut(GIVanillaBehavior flag, bool* result, va_list args) {
    if (!sBonfire.isSet)
        return;
    WriteRespawnData(gPlayState);
}

static void RedirectRespawn(PlayState* play) {
    if (sBonfire.isSet) {
        play->nextEntranceIndex = sBonfire.entranceIndex;
        WriteRespawnData(play);
        gSaveContext.respawnFlag = RESPAWN_MODE_RETURN;
    } else {
        play->nextEntranceIndex = LINKS_HOUSE_ENTRANCE_INDEX;
    }
}

// Fires at the end of Play_LoadToLastEntrance (new hook in z_play.c)
// Covers mid-gameplay deaths... Play_TriggerRespawn to Play_LoadToLastEntrance
// Sets respawnFlag = 1 so Player_Init takes the position-copying branch
// (same as a void-out), reading our bonfire data from RESPAWN_MODE_DOWN
static void OnLoadToLastEntrance(PlayState* play) {
    RedirectRespawn(play);
}

// Fires at the end of KaleidoScope case 0x11 (new hook in z_kaleido_scope.c)
// after the game-over screen has fully run and KaleidoScope has committed
// respawnFlag = -2, reset health, reset magic, and incremented the death counter.
//
// KaleidoScope owns the game-over experience. We own the spawn destination.
// These are orthogonal - we don't touch health/magic/deaths, and KaleidoScope
// doesn't own where the player lands. Setting respawnFlag = 1 here is identical
// in purpose to what Farore's Wind does: use the RESPAWN_MODE_DOWN position
// rather than the entrance's hardcoded spawn point. The game-over screen ran
// in full; we're only redirecting the landing spot.
static void OnGameOverRespawn(PlayState* play) {
    RedirectRespawn(play);
}

static void Register() {
    RegisterSave();

    COND_ID_HOOK(OnOpenText, 0x2053, CVAR_BONFIRE_VALUE, Activate);
    COND_ID_HOOK(OnOpenText, 0x2054, CVAR_BONFIRE_VALUE, Activate);
    COND_ID_HOOK(OnVanillaBehavior, VB_TRIGGER_VOIDOUT, CVAR_BONFIRE_VALUE, OnVoidOut);
    COND_HOOK(OnLoadToLastEntrance, CVAR_BONFIRE_VALUE, OnLoadToLastEntrance);
    COND_HOOK(OnGameOverRespawn, CVAR_BONFIRE_VALUE, OnGameOverRespawn);
    COND_HOOK(OnLoadGame, CVAR_BONFIRE_VALUE, OnLoadGame);
}

} // namespace BonfireMode

static RegisterShipInitFunc initFunc(BonfireMode::Register, { CVAR_BONFIRE_NAME });