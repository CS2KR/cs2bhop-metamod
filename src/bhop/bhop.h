#pragma once

#include "common.h"
#include "movement/movement.h"
#include "sdk/datatypes.h"
#include "mappingapi/bhop_mappingapi.h"
#include "circularbuffer.h"

// TODO: If we want to enable player collision, we need to unhardcode this.
#define BHOP_COLLISION_GROUP_STANDARD  COLLISION_GROUP_DEBRIS
#define BHOP_COLLISION_GROUP_NOTRIGGER LAST_SHARED_COLLISION_GROUP

#define BHOP_SND_SET_CP    "UIPanorama.round_report_odds_none"
#define BHOP_SND_DO_TP     "UIPanorama.round_report_odds_none"
#define BHOP_SND_RESET_CPS "UIPanorama.round_report_odds_dn"

// TODO: add sound & menu addon if its even necessary
#define BHOP_WORKSHOP_ADDON_ID            "3665732047"
#define BHOP_WORKSHOP_ADDON_SNDEVENT_FILE ""

#define BHOP_DEFAULT_CHAT_PREFIX  "{lime}Bhop {grey}|{default}"
#define BHOP_DEFAULT_TIP_INTERVAL 75.0
#define BHOP_DEFAULT_LANGUAGE     "en"
#define BHOP_DEFAULT_STYLE        "Normal"
#define BHOP_DEFAULT_MODE         "128tick"

#define BHOP_RECENT_TELEPORT_THRESHOLD 0.05f

class BhopPlayer;
class BhopAnticheatService;
class BhopBeamService;
class BhopZoneBeamService;
class BhopCheckpointService;
class BhopDatabaseService;
class BhopGlobalService;
class BhopHUDService;
class BhopLanguageService;
class BhopMapService;
class BhopModeService;
class BhopNoclipService;
class BhopOptionService;
class BhopQuietService;
class BhopSpecService;
class BhopGotoService;
class BhopProfileService;
class BhopStyleService;
class BhopTelemetryService;
class BhopTimerService;
class BhopTipService;
class BhopTriggerService;
class BhopFOVService;
class BhopRecordingService;

class BhopPlayer : public MovementPlayer
{
public:
	BhopPlayer(i32 i) : MovementPlayer(i)
	{
		this->Init();
	}

	// General events
	virtual void Init() override;
	virtual void Reset() override;
	virtual void OnPlayerConnect(u64 steamID64) override;
	virtual void OnPlayerActive() override;
	virtual void OnPlayerFullyConnect() override;
	virtual void OnAuthorized() override;

	virtual void OnPhysicsSimulate() override;
	virtual void OnPhysicsSimulatePost() override;
	virtual void OnProcessUsercmds(PlayerCommand *, int) override;
	virtual void OnProcessUsercmdsPost(PlayerCommand *, int) override;
	virtual void OnSetupMove(PlayerCommand *) override;
	virtual void OnSetupMovePost(PlayerCommand *) override;
	virtual void OnProcessMovement() override;
	virtual void OnProcessMovementPost() override;
	virtual void OnPlayerMove() override;
	virtual void OnPlayerMovePost() override;
	virtual void OnCheckParameters() override;
	virtual void OnCheckParametersPost() override;
	virtual void OnCanMove() override;
	virtual void OnCanMovePost() override;
	virtual void OnFullWalkMove(bool &) override;
	virtual void OnFullWalkMovePost(bool) override;
	virtual void OnMoveInit() override;
	virtual void OnMoveInitPost() override;
	virtual void OnCheckWater() override;
	virtual void OnCheckWaterPost() override;
	virtual void OnWaterMove() override;
	virtual void OnWaterMovePost() override;
	virtual void OnCheckVelocity(const char *) override;
	virtual void OnCheckVelocityPost(const char *) override;
	virtual void OnDuck() override;
	virtual void OnDuckPost() override;
	virtual void OnCanUnduck() override;
	// Make an exception for this as it is the only time where we need to change the return value.
	virtual void OnCanUnduckPost(bool &) override;
	virtual void OnLadderMove() override;
	virtual void OnLadderMovePost() override;
	virtual void OnCheckJumpButtonLegacy() override;
	virtual void OnCheckJumpButtonPostLegacy() override;
	virtual void OnJumpLegacy() override;
	virtual void OnJumpPostLegacy() override;
	virtual void OnAirMove() override;
	virtual void OnAirMovePost() override;
	virtual void OnAirAccelerate(Vector &wishdir, f32 &wishspeed, f32 &accel) override;
	virtual void OnAirAcceleratePost(Vector wishdir, f32 wishspeed, f32 accel) override;
	virtual void OnFriction() override;
	virtual void OnFrictionPost() override;
	virtual void OnWalkMove() override;
	virtual void OnWalkMovePost() override;
	virtual void OnTryPlayerMove(Vector *, trace_t *, bool *) override;
	virtual void OnTryPlayerMovePost(Vector *, trace_t *, bool *) override;
	virtual void OnCategorizePosition(bool) override;
	virtual void OnCategorizePositionPost(bool) override;
	virtual void OnFinishGravity() override;
	virtual void OnFinishGravityPost() override;
	virtual void OnCheckFalling() override;
	virtual void OnCheckFallingPost() override;
	virtual void OnPostPlayerMove() override;
	virtual void OnPostPlayerMovePost() override;
	virtual void OnPostThink() override;
	virtual void OnPostThinkPost() override;

	// Movement events
	virtual void OnStartTouchGround() override;
	virtual void OnStopTouchGround() override;
	virtual void OnChangeMoveType(MoveType_t oldMoveType) override;

	// Other events
	virtual void OnChangeTeamPost(i32 team) override;
	virtual void OnTeleport(const Vector *origin, const QAngle *angles, const Vector *velocity) override;

	void PlayErrorSound();

private:
	f64 lastTeleportTime {};
	f32 lastValidYaw {};
	bool oldUsingTurnbinds {};

public:
	BhopAnticheatService *anticheatService {};
	BhopBeamService *beamService {};
	BhopZoneBeamService *zoneBeamService {};
	BhopCheckpointService *checkpointService {};
	BhopDatabaseService *databaseService {};
	BhopGlobalService *globalService {};
	BhopHUDService *hudService {};
	BhopLanguageService *languageService {};
	BhopModeService *modeService {};
	BhopNoclipService *noclipService {};
	BhopOptionService *optionService {};
	BhopQuietService *quietService {};
	BhopSpecService *specService {};
	BhopGotoService *gotoService {};
	BhopProfileService *profileService {};
	CUtlVector<BhopStyleService *> styleServices {};
	BhopTelemetryService *telemetryService {};
	BhopTimerService *timerService {};
	BhopTipService *tipService {};
	BhopTriggerService *triggerService {};
	BhopFOVService *fovService {};
	BhopRecordingService *recordingService {};

	void EnableGodMode();

	// Leg stuff
	void ToggleHideLegs();

	void UpdatePlayerModelAlpha();
	// Teleport checking, used for multiple services
	virtual bool JustTeleported(f32 threshold = BHOP_RECENT_TELEPORT_THRESHOLD);
	// Triggerfix stuff

	// Hit all triggers from start to end with the specified bounds,
	// and call Touch/StartTouch on triggers that the player is touching.
	virtual void TouchTriggersAlongPath(const Vector &start, const Vector &end, const bbox_t &bounds);

	// Update the list of triggers that the player is touching, and call StartTouch/EndTouch appropriately.
	virtual void UpdateTriggerTouchList();

	// Print helpers
	virtual void PrintConsole(bool addPrefix, bool includeSpectators, const char *format, ...);
	virtual void PrintChat(bool addPrefix, bool includeSpectators, const char *format, ...); // Already supports colors.
	virtual void PrintCentre(bool addPrefix, bool includeSpectators, const char *format, ...);
	virtual void PrintAlert(bool addPrefix, bool includeSpectators, const char *format, ...);
	virtual void PrintHTMLCentre(bool addPrefix, bool includeSpectators, const char *format, ...);

	const CVValue_t *GetCvarValueFromModeStyles(const char *name);
};

class BhopBaseService
{
public:
	BhopPlayer *player;

	BhopBaseService(BhopPlayer *player)
	{
		this->player = player;
	}

	// To be implemented by each service class
	virtual void Reset() {}
};

class BhopPlayerManager : public MovementPlayerManager
{
public:
	BhopPlayerManager()
	{
		for (int i = 0; i < MAXPLAYERS + 1; i++)
		{
			delete players[i];
			players[i] = new BhopPlayer(i);
		}
	}

public:
	BhopPlayer *ToPlayer(CPlayerPawnComponent *component);
	BhopPlayer *ToPlayer(CBasePlayerController *controller);
	BhopPlayer *ToPlayer(CBasePlayerPawn *pawn);
	BhopPlayer *ToPlayer(CPlayerSlot slot);
	BhopPlayer *ToPlayer(CEntityIndex entIndex);
	BhopPlayer *ToPlayer(CPlayerUserId userID);
	BhopPlayer *ToPlayer(u32 index);
	BhopPlayer *SteamIdToPlayer(u64 steamID, bool validated = true);

	BhopPlayer *ToBhopPlayer(MovementPlayer *player)
	{
		return static_cast<BhopPlayer *>(player);
	}

	BhopPlayer *ToBhopPlayer(Player *player)
	{
		return static_cast<BhopPlayer *>(player);
	}
};

extern BhopPlayerManager *g_pBhopPlayerManager;

namespace Bhop
{
	namespace misc
	{
		void Init();
		void OnActivateServer();
		void JoinTeam(BhopPlayer *player, int newTeam, bool restorePos = true);
		void ProcessConCommand(ConCommandRef cmd, const CCommandContext &ctx, const CCommand &args);
		META_RES CheckBlockedRadioCommands(const char *cmd);
		void OnRoundStart();
		void InitTimeLimit();
		void EnforceTimeLimit();
		void UnrestrictTimeLimit();
		void OnPhysicsGameSystemFrameBoundary(void *pThis);
		void HandleTeleportToCourse(BhopPlayer *player, const CCommand *args);
	} // namespace misc
}; // namespace Bhop
