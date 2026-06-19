#include "bhop.h"
#include "utils/utils.h"
#include "utils/ctimer.h"
#include "anticheat/bhop_anticheat.h"
#include "beam/bhop_beam.h"
#include "beam/bhop_zone_beam.h"
#include "checkpoint/bhop_checkpoint.h"
#include "db/bhop_db.h"
#include "hud/bhop_hud.h"
#include "language/bhop_language.h"
#include "mode/bhop_mode.h"
#include "noclip/bhop_noclip.h"
#include "option/bhop_option.h"
#include "quiet/bhop_quiet.h"
#include "spec/bhop_spec.h"
#include "goto/bhop_goto.h"
#include "style/bhop_style.h"
#include "telemetry/bhop_telemetry.h"
#include "timer/bhop_timer.h"
#include "tip/bhop_tip.h"
#include "trigger/bhop_trigger.h"
#include "recording/bhop_recording.h"
#include "replays/bhop_replaysystem.h"
#include "global/bhop_global.h"
#include "profile/bhop_profile.h"
#include "fov/bhop_fov.h"

#include "sdk/datatypes.h"
#include "sdk/entity/cbasetrigger.h"
#include "vprof.h"
#include "steam/isteamgameserver.h"
#include "tier0/memdbgon.h"

extern CSteamGameServerAPIContext g_steamAPI;

void BhopPlayer::Init()
{
	MovementPlayer::Init();

	// TODO: initialize every service.
	delete this->anticheatService;
	delete this->beamService;
	delete this->zoneBeamService;
	delete this->checkpointService;
	delete this->languageService;
	delete this->databaseService;
	delete this->quietService;
	delete this->hudService;
	delete this->specService;
	delete this->timerService;
	delete this->optionService;
	delete this->noclipService;
	delete this->tipService;
	delete this->telemetryService;
	delete this->triggerService;
	delete this->recordingService;
	delete this->globalService;
	delete this->profileService;
	delete this->fovService;

	this->anticheatService = new BhopAnticheatService(this);
	this->beamService = new BhopBeamService(this);
	this->zoneBeamService = new BhopZoneBeamService(this);
	this->checkpointService = new BhopCheckpointService(this);
	this->databaseService = new BhopDatabaseService(this);
	this->languageService = new BhopLanguageService(this);
	this->noclipService = new BhopNoclipService(this);
	this->quietService = new BhopQuietService(this);
	this->hudService = new BhopHUDService(this);
	this->specService = new BhopSpecService(this);
	this->gotoService = new BhopGotoService(this);
	this->timerService = new BhopTimerService(this);
	this->optionService = new BhopOptionService(this);
	this->tipService = new BhopTipService(this);
	this->telemetryService = new BhopTelemetryService(this);
	this->triggerService = new BhopTriggerService(this);
	this->recordingService = new BhopRecordingService(this);
	this->globalService = new BhopGlobalService(this);
	this->profileService = new BhopProfileService(this);
	this->fovService = new BhopFOVService(this);

	Bhop::mode::InitModeService(this);
}

void BhopPlayer::Reset()
{
	MovementPlayer::Reset();

	// Reset services that should not persist across player sessions.
	this->languageService->Reset();
	this->tipService->Reset();
	this->modeService->Reset();
	this->optionService->Reset();
	this->checkpointService->Reset();
	this->noclipService->Reset();
	this->quietService->Reset();
	this->hudService->Reset();
	this->timerService->Reset();
	this->specService->Reset();
	this->triggerService->Reset();
	this->beamService->Reset();
	this->telemetryService->Reset();
	this->recordingService->Reset();

	g_pBhopModeManager->SwitchToMode(this, BhopOptionService::GetOptionStr("defaultMode", BHOP_DEFAULT_MODE), true, true, false);
	g_pBhopStyleManager->ClearStyles(this, true, false);
	CSplitString styles(BhopOptionService::GetOptionStr("defaultStyles"), ",");
	FOR_EACH_VEC(styles, i)
	{
		g_pBhopStyleManager->AddStyle(this, styles[i]);
	}
}

void BhopPlayer::OnPlayerConnect(u64 steamID64)
{
	this->languageService->OnPlayerConnect(steamID64);
}

void BhopPlayer::OnPlayerActive()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	// Mode/Styles stuff must be here for convars to be properly replicated.
	g_pBhopModeManager->SwitchToMode(this, this->modeService->GetModeName(), true, true, false);
	g_pBhopStyleManager->RefreshStyles(this, false);

	this->optionService->OnPlayerActive();
	this->recordingService->EnsureCircularRecorderInitialized();
}

void BhopPlayer::OnPlayerFullyConnect()
{
	this->anticheatService->OnPlayerFullyConnect();
}

void BhopPlayer::OnAuthorized()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	MovementPlayer::OnAuthorized();
	this->databaseService->SetupClient();
	this->profileService->timeToNextRatingRefresh = 0.0f; // Force immediate refresh
	this->globalService->OnPlayerAuthorized();
}

void BhopPlayer::OnPhysicsSimulate()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	MovementPlayer::OnPhysicsSimulate();
	this->recordingService->OnPhysicsSimulate();
	this->triggerService->OnPhysicsSimulate();
	this->modeService->OnPhysicsSimulate();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnPhysicsSimulate();
	}
	this->hudService->OnPhysicsSimulate();
	this->noclipService->HandleMoveCollision();
	this->EnableGodMode();
	this->UpdatePlayerModelAlpha();
	this->fovService->OnPhysicsSimulate();
	Bhop::replaysystem::OnPhysicsSimulate(this);
}

void BhopPlayer::OnPhysicsSimulatePost()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	MovementPlayer::OnPhysicsSimulatePost();
	this->recordingService->OnPhysicsSimulatePost();
	this->triggerService->OnPhysicsSimulatePost();
	this->telemetryService->OnPhysicsSimulatePost();
	this->modeService->OnPhysicsSimulatePost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnPhysicsSimulatePost();
	}
	this->timerService->OnPhysicsSimulatePost();
	Bhop::replaysystem::OnPhysicsSimulatePost(this);
	if (this->specService->GetSpectatedPlayer())
	{
		BhopHUDService::DrawPanels(this->specService->GetSpectatedPlayer(), this);
	}
	else if (this->IsAlive())
	{
		BhopHUDService::DrawPanels(this, this);
	}
	this->quietService->OnPhysicsSimulatePost();
	this->profileService->OnPhysicsSimulatePost();
}

void BhopPlayer::OnProcessUsercmds(PlayerCommand *cmds, int numcmds)
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->recordingService->OnProcessUsercmds(cmds, numcmds);
	this->modeService->OnProcessUsercmds(cmds, numcmds);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnProcessUsercmds(cmds, numcmds);
	}
}

void BhopPlayer::OnProcessUsercmdsPost(PlayerCommand *cmds, int numcmds)
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnProcessUsercmdsPost(cmds, numcmds);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnProcessUsercmdsPost(cmds, numcmds);
	}
}

void BhopPlayer::OnSetupMove(PlayerCommand *pc)
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->recordingService->OnSetupMove(pc);
	this->modeService->OnSetupMove(pc);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnSetupMove(pc);
	}
}

void BhopPlayer::OnSetupMovePost(PlayerCommand *pc)
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnSetupMovePost(pc);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnSetupMovePost(pc);
	}
}

void BhopPlayer::OnProcessMovement()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	MovementPlayer::OnProcessMovement();

	Bhop::mode::ApplyModeSettings(this);
	Bhop::replaysystem::OnProcessMovement(this);

	this->triggerService->OnProcessMovement();
	this->modeService->OnProcessMovement();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnProcessMovement();
	}
}

void BhopPlayer::OnProcessMovementPost()
{
	VPROF_BUDGET(__func__, "CS2Bhop");

	this->modeService->OnProcessMovementPost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnProcessMovementPost();
	}
	this->triggerService->OnProcessMovementPost();
	Bhop::replaysystem::OnProcessMovementPost(this);
	MovementPlayer::OnProcessMovementPost();
}

void BhopPlayer::OnPlayerMove()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnPlayerMove();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnPlayerMove();
	}
}

void BhopPlayer::OnPlayerMovePost()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnPlayerMovePost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnPlayerMovePost();
	}
}

void BhopPlayer::OnCheckParameters()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnCheckParameters();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCheckParameters();
	}
}

void BhopPlayer::OnCheckParametersPost()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnCheckParametersPost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCheckParametersPost();
	}
}

void BhopPlayer::OnCanMove()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnCanMove();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCanMove();
	}
}

void BhopPlayer::OnCanMovePost()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnCanMovePost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCanMovePost();
	}
}

void BhopPlayer::OnFullWalkMove(bool &ground)
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnFullWalkMove(ground);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnFullWalkMove(ground);
	}
}

void BhopPlayer::OnFullWalkMovePost(bool ground)
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnFullWalkMovePost(ground);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnFullWalkMovePost(ground);
	}
}

void BhopPlayer::OnMoveInit()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnMoveInit();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnMoveInit();
	}
}

void BhopPlayer::OnMoveInitPost()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnMoveInitPost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnMoveInitPost();
	}
}

void BhopPlayer::OnCheckWater()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnCheckWater();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCheckWater();
	}
}

void BhopPlayer::OnWaterMove()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnWaterMove();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnWaterMove();
	}
}

void BhopPlayer::OnWaterMovePost()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnWaterMovePost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnWaterMovePost();
	}
}

void BhopPlayer::OnCheckWaterPost()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnCheckWaterPost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCheckWaterPost();
	}
}

void BhopPlayer::OnCheckVelocity(const char *a3)
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnCheckVelocity(a3);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCheckVelocity(a3);
	}
}

void BhopPlayer::OnCheckVelocityPost(const char *a3)
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnCheckVelocityPost(a3);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCheckVelocityPost(a3);
	}
}

void BhopPlayer::OnDuck()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnDuck();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnDuck();
	}
}

void BhopPlayer::OnDuckPost()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnDuckPost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnDuckPost();
	}
}

void BhopPlayer::OnCanUnduck()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnCanUnduck();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCanUnduck();
	}
}

void BhopPlayer::OnCanUnduckPost(bool &ret)
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnCanUnduckPost(ret);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCanUnduckPost(ret);
	}
}

void BhopPlayer::OnLadderMove()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnLadderMove();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnLadderMove();
	}
}

void BhopPlayer::OnLadderMovePost()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnLadderMovePost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnLadderMovePost();
	}
}

void BhopPlayer::OnCheckJumpButtonLegacy()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnCheckJumpButtonLegacy();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCheckJumpButtonLegacy();
	}
	this->triggerService->OnCheckJumpButtonLegacy();
}

void BhopPlayer::OnCheckJumpButtonPostLegacy()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnCheckJumpButtonPostLegacy();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCheckJumpButtonPostLegacy();
	}
}

void BhopPlayer::OnJumpLegacy()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnJumpLegacy();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnJumpLegacy();
	}
	this->hudService->OnJump();
}

void BhopPlayer::OnJumpPostLegacy()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnJumpPostLegacy();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnJumpPostLegacy();
	}
}

void BhopPlayer::OnAirMove()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnAirMove();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnAirMove();
	}
}

void BhopPlayer::OnAirMovePost()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnAirMovePost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnAirMovePost();
	}
}

void BhopPlayer::OnAirAccelerate(Vector &wishdir, f32 &wishspeed, f32 &accel)
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnAirAccelerate(wishdir, wishspeed, accel);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnAirAccelerate(wishdir, wishspeed, accel);
	}
}

void BhopPlayer::OnAirAcceleratePost(Vector wishdir, f32 wishspeed, f32 accel)
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnAirAcceleratePost(wishdir, wishspeed, accel);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnAirAcceleratePost(wishdir, wishspeed, accel);
	}
}

void BhopPlayer::OnFriction()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnFriction();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnFriction();
	}
}

void BhopPlayer::OnFrictionPost()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnFrictionPost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnFrictionPost();
	}
}

void BhopPlayer::OnWalkMove()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnWalkMove();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnWalkMove();
	}
}

void BhopPlayer::OnWalkMovePost()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnWalkMovePost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnWalkMovePost();
	}
}

void BhopPlayer::OnTryPlayerMove(Vector *pFirstDest, trace_t *pFirstTrace, bool *bIsBhoping)
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnTryPlayerMove(pFirstDest, pFirstTrace, bIsBhoping);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnTryPlayerMove(pFirstDest, pFirstTrace, bIsBhoping);
	}
}

void BhopPlayer::OnTryPlayerMovePost(Vector *pFirstDest, trace_t *pFirstTrace, bool *bIsBhoping)
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnTryPlayerMovePost(pFirstDest, pFirstTrace, bIsBhoping);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnTryPlayerMovePost(pFirstDest, pFirstTrace, bIsBhoping);
	}
}

void BhopPlayer::OnCategorizePosition(bool bStayOnGround)
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnCategorizePosition(bStayOnGround);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCategorizePosition(bStayOnGround);
	}
}

void BhopPlayer::OnCategorizePositionPost(bool bStayOnGround)
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnCategorizePositionPost(bStayOnGround);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCategorizePositionPost(bStayOnGround);
	}
}

void BhopPlayer::OnFinishGravity()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnFinishGravity();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnFinishGravity();
	}
}

void BhopPlayer::OnFinishGravityPost()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnFinishGravityPost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnFinishGravityPost();
	}
}

void BhopPlayer::OnCheckFalling()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnCheckFalling();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCheckFalling();
	}
}

void BhopPlayer::OnCheckFallingPost()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnCheckFallingPost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCheckFallingPost();
	}
}

void BhopPlayer::OnPostPlayerMove()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnPostPlayerMove();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnPostPlayerMove();
	}
}

void BhopPlayer::OnPostPlayerMovePost()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnPostPlayerMovePost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnPostPlayerMovePost();
	}
}

void BhopPlayer::OnPostThink()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnPostThink();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnPostThink();
	}
	MovementPlayer::OnPostThink();
}

void BhopPlayer::OnPostThinkPost()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->modeService->OnPostThinkPost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnPostThinkPost();
	}
}

void BhopPlayer::OnStartTouchGround()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->timerService->OnStartTouchGround();
	this->modeService->OnStartTouchGround();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnStartTouchGround();
	}
}

void BhopPlayer::OnStopTouchGround()
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->timerService->OnStopTouchGround();
	this->modeService->OnStopTouchGround();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnStopTouchGround();
	}
	this->triggerService->OnStopTouchGround();
}

void BhopPlayer::OnChangeMoveType(MoveType_t oldMoveType)
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->timerService->OnChangeMoveType(oldMoveType);
	this->modeService->OnChangeMoveType(oldMoveType);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnChangeMoveType(oldMoveType);
	}
}

void BhopPlayer::OnTeleport(const Vector *origin, const QAngle *angles, const Vector *velocity)
{
	VPROF_BUDGET(__func__, "CS2Bhop");
	this->lastTeleportTime = g_pBhopUtils->GetServerGlobals()->curtime;
	this->modeService->OnTeleport(origin, angles, velocity);
	this->timerService->OnTeleport(origin, angles, velocity);
	this->recordingService->OnTeleport(origin, angles, velocity);
	if (origin)
	{
		this->beamService->OnTeleport();
	}
	this->triggerService->OnTeleport();
}

void BhopPlayer::EnableGodMode()
{
	CCSPlayerPawn *pawn = this->GetPlayerPawn();
	if (!pawn)
	{
		return;
	}
	if (pawn->m_bTakesDamage())
	{
		pawn->m_bTakesDamage(false);
	}
}

void BhopPlayer::UpdatePlayerModelAlpha()
{
	CCSPlayerPawn *pawn = this->GetPlayerPawn();
	if (!pawn)
	{
		return;
	}
	Color ogColor = pawn->m_clrRender();
	bool hideLegs = this->optionService->GetPreferenceBool("hideLegs");
	if (hideLegs && pawn->m_clrRender().a() == 255)
	{
		pawn->m_clrRender(Color(255, 255, 255, 254));
	}
	else if (!hideLegs && pawn->m_clrRender().a() != 255)
	{
		pawn->m_clrRender(Color(255, 255, 255, 255));
	}
}

bool BhopPlayer::JustTeleported(f32 threshold)
{
	return g_pBhopUtils->GetServerGlobals()->curtime - this->lastTeleportTime < threshold;
}

void BhopPlayer::ToggleHideLegs()
{
	this->optionService->SetPreferenceBool("hideLegs", !this->optionService->GetPreferenceBool("hideLegs", false));
}

void BhopPlayer::PlayErrorSound()
{
	utils::PlaySoundToClient(this->GetPlayerSlot(), MV_SND_ERROR);
}

void BhopPlayer::TouchTriggersAlongPath(const Vector &start, const Vector &end, const bbox_t &bounds)
{
	this->triggerService->TouchTriggersAlongPath(start, end, bounds);
}

void BhopPlayer::UpdateTriggerTouchList()
{
	this->triggerService->UpdateTriggerTouchList();
}

void BhopPlayer::OnChangeTeamPost(i32 team)
{
	this->timerService->OnPlayerJoinTeam(team);
}

const CVValue_t *BhopPlayer::GetCvarValueFromModeStyles(const char *name)
{
	if (!name)
	{
		assert(0);
		return CVValue_t::InvalidValue();
	}

	ConVarRefAbstract cvarRef(name);
	if (!cvarRef.IsValidRef() || !cvarRef.IsConVarDataAvailable())
	{
		assert(0);
		META_CONPRINTF("Failed to find %s!\n", name);
		return CVValue_t::InvalidValue();
	}

	FOR_EACH_VEC_BACK(this->styleServices, i)
	{
		if (this->styleServices[i]->GetTweakedConvarValue(name))
		{
			return this->styleServices[i]->GetTweakedConvarValue(name);
		}
	}

	for (int i = 0; i < MODECVAR_COUNT; i++)
	{
		if (!Bhop::mode::modeCvarRefs[i]->IsValidRef() || !Bhop::mode::modeCvarRefs[i]->IsConVarDataAvailable())
		{
			continue;
		}
		if (!V_stricmp(Bhop::mode::modeCvarRefs[i]->GetName(), name))
		{
			return &this->modeService->GetModeConVarValues()[i];
		}
	}

	return cvarRef.GetConVarData()->Value(-1);
}
