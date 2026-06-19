#include "bhop_timer.h"
#include "bhop/db/bhop_db.h"
#include "bhop/global/bhop_global.h"
#include "bhop/language/bhop_language.h"
#include "bhop/mode/bhop_mode.h"
#include "bhop/style/bhop_style.h"
#include "bhop/noclip/bhop_noclip.h"
#include "bhop/option/bhop_option.h"
#include "bhop/language/bhop_language.h"
#include "bhop/trigger/bhop_trigger.h"
#include "bhop/spec/bhop_spec.h"
#include "bhop/recording/bhop_recording.h"
#include "submission.h"

#include "utils/utils.h"
#include "utils/simplecmds.h"
#include "vendor/sql_mm/src/public/sql_mm.h"

// clang-format off
constexpr const char *diffTextKeys[BhopTimerService::CompareType::COMPARETYPE_COUNT] = {
	"",
	"Server PB Diff (Overall)",
	"Global PB Diff (Overall)",
	"SR Diff (Overall)",
	"WR Diff (Overall)"
};

constexpr const char *missedTimeKeys[BhopTimerService::CompareType::COMPARETYPE_COUNT] = {
	"",
	"Missed Server PB (Overall)",
	"Missed Global PB (Overall)",
	"Missed SR (Overall)",
	"Missed WR (Overall)"
};

// clang-format on

static_global class BhopDatabaseServiceEventListener_Timer : public BhopDatabaseServiceEventListener
{
public:
	virtual void OnMapSetup() override;
	virtual void OnCoursesSetup() override;
	virtual void OnClientSetup(Player *player, u64 steamID64, bool isCheater) override;
} databaseEventListener;

static_global class BhopOptionServiceEventListener_Timer : public BhopOptionServiceEventListener
{
	virtual void OnPlayerPreferencesLoaded(BhopPlayer *player)
	{
		player->timerService->OnPlayerPreferencesLoaded();
	}
} optionEventListener;

std::unordered_map<PBDataKey, PBData> BhopTimerService::srCache;
std::unordered_map<PBDataKey, PBData> BhopTimerService::wrCache;

static_global CUtlVector<BhopTimerServiceEventListener *> eventListeners;

bool BhopTimerService::RegisterEventListener(BhopTimerServiceEventListener *eventListener)
{
	if (eventListeners.Find(eventListener) >= 0)
	{
		return false;
	}
	eventListeners.AddToTail(eventListener);
	return true;
}

bool BhopTimerService::UnregisterEventListener(BhopTimerServiceEventListener *eventListener)
{
	return eventListeners.FindAndRemove(eventListener);
}

void BhopTimerService::StartZoneStartTouch(const BhopCourseDescriptor *course)
{
	this->touchedGroundSinceTouchingStartZone = !!(this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND);
	this->inStartzone = true;
}

void BhopTimerService::StartZoneEndTouch(const BhopCourseDescriptor *course)
{
	if (this->touchedGroundSinceTouchingStartZone && !this->timerRunning)
	{
		this->TimerStart(course);
	}
	this->inStartzone = false;
}

void BhopTimerService::CheckpointZoneStartTouch(const BhopCourseDescriptor *course, i32 cpNumber)
{
	if (!this->timerRunning || course->guid != this->currentCourseGUID)
	{
		return;
	}

	assert(cpNumber > INVALID_CHECKPOINT_NUMBER && cpNumber < BHOP_MAX_CHECKPOINT_ZONES);

	if (this->cpZoneTimes[cpNumber - 1] < 0)
	{
		this->PlayReachedCheckpointSound();
		this->cpZoneTimes[cpNumber - 1] = this->GetTime();
		this->ShowCheckpointText(cpNumber);
		this->lastCheckpoint = cpNumber;
		this->reachedCheckpoints++;
		CALL_FORWARD(eventListeners, OnCheckpointZoneTouchPost, this->player, cpNumber);
	}
}

void BhopTimerService::StageZoneStartTouch(const BhopCourseDescriptor *course, i32 stageNumber)
{
	if (!this->timerRunning || course->guid != this->currentCourseGUID)
	{
		return;
	}

	assert(stageNumber > INVALID_STAGE_NUMBER && stageNumber < BHOP_MAX_STAGE_ZONES);

	// skipped stage
	if (stageNumber > this->currentStage + 1)
	{
		this->PlayMissedZoneSound();
		this->player->languageService->PrintChat(true, false, "Touched too high stage number (Missed stage)", this->currentStage + 1);
		return;
	}

	// same stage (failed)
	if (stageNumber == this->currentStage)
	{
		return;
	}

	// next stage
	if (stageNumber == this->currentStage + 1)
	{
		this->stageZoneTimes[this->currentStage - 1] = this->GetTime() - this->stageEndTouchTimes[this->currentStage - 1];

		this->PlayReachedStageSound();
		this->ShowStageText();
		this->currentStage++;
		CALL_FORWARD(eventListeners, OnStageZoneTouchPost, this->player, stageNumber);
	}
}

void BhopTimerService::StageZoneEndTouch(const BhopCourseDescriptor *course, i32 stageNumber)
{
	if (!this->timerRunning || course->guid != this->currentCourseGUID)
	{
		return;
	}

	assert(stageNumber > INVALID_STAGE_NUMBER && stageNumber < BHOP_MAX_STAGE_ZONES);

	this->stageEndTouchTimes[this->currentStage - 1] = this->GetTime();
}

bool BhopTimerService::TimerStart(const BhopCourseDescriptor *courseDesc, bool playSound)
{
	// clang-format off
	if (!this->player->GetPlayerPawn()->IsAlive()
		|| this->JustStartedTimer()
		|| this->player->JustTeleported()
		|| this->player->noclipService->JustNoclipped()
		|| !this->HasValidMoveType()
		|| (this->GetTimerRunning() && courseDesc->guid == this->currentCourseGUID)
		|| (!(this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND) && !this->GetValidJump()))
	// clang-format on
	{
		return false;
	}
	if (this->player->inPerf || this->JustLanded())
	{
		// Have a .5s landing time cooldown to ensure no speed from recent perfs can be used to start
		this->player->languageService->PrintChat(true, false, "Can't Bhop Start");
		return false;
	}
	if (V_strlen(this->player->modeService->GetModeName()) > BHOP_MAX_MODE_NAME_LENGTH)
	{
		Warning("[Bhop] Timer start failed: Mode name is too long!");
		return false;
	}

	bool allowStart = true;
	FOR_EACH_VEC(eventListeners, i)
	{
		allowStart &= eventListeners[i]->OnTimerStart(this->player, courseDesc->guid);
	}
	if (!allowStart)
	{
		return false;
	}

	// In CS2Bhop you can touch trigger in half tick intervals, but here we are incrementing by full tick intervals only.
	// Since the player was still in the trigger for half a tick, we need to offset by half a tick if we started in a half tick.
	// So the current time should be subtracted by the difference between server curtime and client curtime at the moment of starting the timer,
	// That way when we increment by full tick intervals in OnPhysicsSimulatePost, the time will be correct.
	this->currentTime = g_pBhopUtils->GetGlobals()->curtime - g_pBhopUtils->GetServerGlobals()->curtime;
	assert(this->currentTime <= 0 && this->currentTime > -ENGINE_FIXED_TICK_INTERVAL);
	this->timerRunning = true;

	this->reachedCheckpoints = 0;
	this->lastCheckpoint = 0;

	f64 invalidTime = -1;
	this->cpZoneTimes.SetSize(courseDesc->checkpointCount);
	this->stageZoneTimes.SetSize(courseDesc->stageCount);
	this->stageEndTouchTimes.SetSize(courseDesc->stageCount);

	this->cpZoneTimes.FillWithValue(invalidTime);
	this->stageZoneTimes.FillWithValue(invalidTime);
	this->stageEndTouchTimes.FillWithValue(invalidTime);

	if (courseDesc->stageCount > 0)
	{
		this->currentStage = 1;
		// initialize stage 1 end touch time
		this->stageEndTouchTimes[0] = this->GetTime();
	}
	else
	{
		this->currentStage = 0;
	}

	this->player->checkpointService->ResetCheckpoints();

	SetCourse(courseDesc->guid);
	this->validTime = true;
	this->shouldAnnounceMissedTime = true;

	this->UpdateCurrentCompareType(ToPBDataKey(Bhop::mode::GetModeInfo(this->player->modeService).id, courseDesc->guid));

	if (playSound)
	{
		for (BhopPlayer *spec = player->specService->GetNextSpectator(NULL); spec != NULL; spec = player->specService->GetNextSpectator(spec))
		{
			player->timerService->PlayTimerStartSound();
		}
		this->PlayTimerStartSound();
	}

	if (!this->player->IsAuthenticated())
	{
		this->player->languageService->PrintChat(true, false, "No Steam Authentication Warning");
	}
	if (BhopGlobalService::IsAvailable() && !this->player->hasPrime)
	{
		this->player->languageService->PrintChat(true, false, "No Prime Warning");
	}

	const char *language = this->player->languageService->GetLanguage();
	std::string startSpeedText = this->player->timerService->GetStartSpeedText(language);

	this->player->languageService->PrintChat(true, true, startSpeedText.c_str());

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnTimerStartPost(this->player, courseDesc->guid);
	}
	return true;
}

bool BhopTimerService::TimerEnd(const BhopCourseDescriptor *courseDesc)
{
	if (!this->player->IsAlive())
	{
		return false;
	}

	if (!this->timerRunning || courseDesc->guid != this->currentCourseGUID)
	{
		for (BhopPlayer *spec = player->specService->GetNextSpectator(NULL); spec != NULL; spec = player->specService->GetNextSpectator(spec))
		{
			player->timerService->PlayTimerFalseEndSound();
		}
		this->PlayTimerFalseEndSound();
		this->lastFalseEndTime = g_pBhopUtils->GetServerGlobals()->curtime;
		return false;
	}

	if (courseDesc->stageCount > 0 && (this->currentStage - 1 != courseDesc->stageCount))
	{
		this->PlayMissedZoneSound();
		this->player->languageService->PrintChat(true, false, "Can't Finish Run (Missed Stage)", this->currentStage + 1);
		return false;
	}

	if (this->reachedCheckpoints != courseDesc->checkpointCount)
	{
		this->PlayMissedZoneSound();
		i32 missCount = courseDesc->checkpointCount - this->reachedCheckpoints;
		if (missCount == 1)
		{
			this->player->languageService->PrintChat(true, false, "Can't Finish Run (Missed a Checkpoint Zone)");
		}
		else
		{
			this->player->languageService->PrintChat(true, false, "Can't Finish Run (Missed Checkpoint Zones)", missCount);
		}
		return false;
	}

	f32 time = this->GetTime() + g_pBhopUtils->GetServerGlobals()->frametime;
	u32 teleportsUsed = this->player->checkpointService->GetTeleportCount();

	bool allowEnd = true;
	FOR_EACH_VEC(eventListeners, i)
	{
		allowEnd &= eventListeners[i]->OnTimerEnd(this->player, this->currentCourseGUID, time);
	}
	if (!allowEnd)
	{
		return false;
	}
	// Update current time for one last time.
	this->currentTime = time;

	this->timerRunning = false;
	this->lastEndTime = g_pBhopUtils->GetServerGlobals()->curtime;

	for (BhopPlayer *spec = player->specService->GetNextSpectator(NULL); spec != NULL; spec = player->specService->GetNextSpectator(spec))
	{
		player->timerService->PlayTimerEndSound();
	}
	this->PlayTimerEndSound();

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnTimerEndPost(this->player, this->currentCourseGUID, time);
	}

	// This must be called after OnTimerEndPost so that the run UUID is set correctly.
	if (!this->player->GetPlayerPawn()->IsBot())
	{
		RunSubmission::Create(this->player);
	}

	// Reset current stage immediately to remove HUD element
	this->currentStage = 0;

	return true;
}

bool BhopTimerService::TimerStop(bool playSound)
{
	if (!this->timerRunning)
	{
		return false;
	}
	this->timerRunning = false;
	if (playSound)
	{
		for (BhopPlayer *spec = player->specService->GetNextSpectator(NULL); spec != NULL; spec = player->specService->GetNextSpectator(spec))
		{
			spec->timerService->PlayTimerStopSound();
		}
		this->PlayTimerStopSound();
	}

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnTimerStopped(this->player, this->currentCourseGUID);
	}

	// Reset current stage immediately to remove HUD element
	this->currentStage = 0;

	return true;
}

void BhopTimerService::TimerStopAll(bool playSound)
{
	for (int i = 0; i < MAXPLAYERS + 1; i++)
	{
		BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(i);
		if (!player || !player->timerService)
		{
			continue;
		}
		player->timerService->TimerStop(playSound);
	}
}

void BhopTimerService::InvalidateJump()
{
	this->validJump = false;
	this->lastInvalidateTime = g_pBhopUtils->GetServerGlobals()->curtime;
}

void BhopTimerService::PlayTimerStartSound()
{
	if (g_pBhopUtils->GetServerGlobals()->curtime - this->lastStartSoundTime > BHOP_TIMER_SOUND_COOLDOWN && this->shouldPlayTimerSound)
	{
		utils::PlaySoundToClient(this->player->GetPlayerSlot(), BHOP_TIMER_SND_START);
		this->lastStartSoundTime = g_pBhopUtils->GetServerGlobals()->curtime;
	}
}

void BhopTimerService::InvalidateRun()
{
	if (!this->validTime)
	{
		return;
	}
	this->validTime = false;

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnTimerInvalidated(this->player);
	}
}

bool BhopTimerService::HasValidMoveType()
{
	return BhopTimerService::IsValidMoveType(this->player->GetMoveType());
}

bool BhopTimerService::JustEndedTimer()
{
	return g_pBhopUtils->GetServerGlobals()->curtime - this->lastEndTime > 1.0f;
}

void BhopTimerService::PlayTimerEndSound()
{
	if (this->shouldPlayTimerSound)
	{
		utils::PlaySoundToClient(this->player->GetPlayerSlot(), BHOP_TIMER_SND_END);
	}
}

void BhopTimerService::PlayTimerFalseEndSound()
{
	if (this->shouldPlayTimerSound)
	{
		utils::PlaySoundToClient(this->player->GetPlayerSlot(), BHOP_TIMER_SND_FALSE_END);
	}
}

void BhopTimerService::PlayMissedZoneSound()
{
	if (this->shouldPlayTimerSound)
	{
		utils::PlaySoundToClient(this->player->GetPlayerSlot(), BHOP_TIMER_SND_MISSED_ZONE);
	}
}

void BhopTimerService::PlayReachedCheckpointSound()
{
	if (this->shouldPlayTimerSound)
	{
		utils::PlaySoundToClient(this->player->GetPlayerSlot(), BHOP_TIMER_SND_REACH_CHECKPOINT);
	}
}

void BhopTimerService::PlayReachedStageSound()
{
	if (this->shouldPlayTimerSound)
	{
		utils::PlaySoundToClient(this->player->GetPlayerSlot(), BHOP_TIMER_SND_REACH_STAGE);
	}
}

void BhopTimerService::PlayTimerStopSound()
{
	if (this->shouldPlayTimerSound)
	{
		utils::PlaySoundToClient(this->player->GetPlayerSlot(), BHOP_TIMER_SND_STOP);
	}
}

void BhopTimerService::PlayMissedTimeSound()
{
	if (this->shouldPlayTimerSound)
	{
		if (g_pBhopUtils->GetServerGlobals()->curtime - this->lastMissedTimeSoundTime > BHOP_TIMER_SOUND_COOLDOWN)
		{
			utils::PlaySoundToClient(this->player->GetPlayerSlot(), BHOP_TIMER_SND_MISSED_TIME);
			this->lastMissedTimeSoundTime = g_pBhopUtils->GetServerGlobals()->curtime;
		}
	}
}

static_function std::string GetTeleportCountText(int tpCount, const char *language)
{
	return tpCount == 1 ? BhopLanguageService::PrepareMessageWithLang(language, "1 Teleport Text")
						: BhopLanguageService::PrepareMessageWithLang(language, "2+ Teleports Text", tpCount);
}

void BhopTimerService::Pause()
{
	if (!this->CanPause(true))
	{
		return;
	}

	bool allowPause = true;
	FOR_EACH_VEC(eventListeners, i)
	{
		allowPause &= eventListeners[i]->OnPause(this->player);
	}
	if (!allowPause)
	{
		this->player->languageService->PrintChat(true, false, "Can't Pause (Generic)");
		this->player->PlayErrorSound();
		return;
	}

	this->paused = true;
	this->pausedOnLadder = this->player->GetMoveType() == MOVETYPE_LADDER;
	this->lastDuckValue = this->player->GetMoveServices()->m_flDuckAmount;
	this->lastStaminaValue = this->player->GetMoveServices()->m_flStamina;
	this->player->SetVelocity(vec3_origin);
	this->player->SetMoveType(MOVETYPE_NONE);
	this->player->GetPlayerPawn()->SetGravityScale(0);

	if (this->GetTimerRunning())
	{
		this->hasPausedInThisRun = true;
		this->lastPauseTime = g_pBhopUtils->GetServerGlobals()->curtime;
	}

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnPausePost(this->player);
	}
}

bool BhopTimerService::CanPause(bool showError)
{
	if (this->paused)
	{
		return false;
	}

	Vector velocity;
	this->player->GetVelocity(&velocity);

	if (this->GetTimerRunning())
	{
		if (this->hasResumedInThisRun && g_pBhopUtils->GetServerGlobals()->curtime - this->lastResumeTime < BHOP_PAUSE_COOLDOWN)
		{
			if (showError)
			{
				this->player->languageService->PrintChat(true, false, "Can't Pause (Just Resumed)");
				this->player->PlayErrorSound();
			}
			return false;
		}
		else if (!(this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND) && !(velocity.Length2D() == 0.0f && velocity.z == 0.0f))
		{
			if (showError)
			{
				this->player->languageService->PrintChat(true, false, "Can't Pause (Midair)");
				this->player->PlayErrorSound();
			}
			return false;
		}
	}
	return true;
}

void BhopTimerService::Resume(bool force)
{
	if (!this->paused)
	{
		return;
	}
	if (!force && !this->CanResume(true))
	{
		return;
	}

	bool allowResume = true;
	FOR_EACH_VEC(eventListeners, i)
	{
		allowResume &= eventListeners[i]->OnResume(this->player);
	}
	if (!allowResume)
	{
		this->player->languageService->PrintChat(true, false, "Can't Resume (Generic)");
		this->player->PlayErrorSound();
		return;
	}

	if (this->pausedOnLadder)
	{
		this->player->SetMoveType(MOVETYPE_LADDER);
	}
	else
	{
		this->player->SetMoveType(MOVETYPE_WALK);
	}

	// GOKZ: prevent noclip exploit
	this->player->GetPlayerPawn()->m_Collision().m_CollisionGroup() = BHOP_COLLISION_GROUP_STANDARD;
	this->player->GetPlayerPawn()->CollisionRulesChanged();

	this->paused = false;
	if (this->GetTimerRunning())
	{
		this->hasResumedInThisRun = true;
		this->lastResumeTime = g_pBhopUtils->GetServerGlobals()->curtime;
	}
	this->player->GetMoveServices()->m_flDuckAmount = this->lastDuckValue;
	this->player->GetMoveServices()->m_flStamina = this->lastStaminaValue;
	this->player->GetPlayerPawn()->SetGravityScale(1);

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnResumePost(this->player);
	}
}

bool BhopTimerService::CanResume(bool showError)
{
	if (this->GetTimerRunning() && this->hasPausedInThisRun && g_pBhopUtils->GetServerGlobals()->curtime - this->lastPauseTime < BHOP_PAUSE_COOLDOWN)
	{
		if (showError)
		{
			this->player->languageService->PrintChat(true, false, "Can't Resume (Just Paused)");
			this->player->PlayErrorSound();
		}
		return false;
	}
	return true;
}

void BhopTimerService::TogglePause()
{
	if (!this->player->IsAlive())
	{
		Bhop::misc::JoinTeam(player, CS_TEAM_CT);
	}
	else
	{
		paused ? Resume() : Pause();
	}
}

SCMD(bhop_timerstopsound, SCFL_TIMER | SCFL_PREFERENCE)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	player->timerService->ToggleTimerStopSound();
	return MRES_SUPERCEDE;
}

SCMD_LINK(bhop_tss, bhop_timerstopsound);

void BhopTimerService::ToggleTimerStopSound()
{
	this->shouldPlayTimerSound = !this->shouldPlayTimerSound;
	this->player->optionService->SetPreferenceBool("timerStopSound", this->shouldPlayTimerSound);
	this->player->languageService->PrintChat(true, false, this->shouldPlayTimerSound ? "Timer Stop Sound Enabled" : "Timer Stop Sound Disabled");
}

void BhopTimerService::Reset()
{
	this->timerRunning = {};
	this->currentTime = {};
	this->currentCourseGUID = 0;
	this->lastEndTime = {};
	this->lastFalseEndTime = {};
	this->lastStartSoundTime = {};
	this->lastMissedTimeSoundTime = {};
	this->validTime = {};
	this->paused = {};
	this->pausedOnLadder = {};
	this->lastPauseTime = {};
	this->hasPausedInThisRun = {};
	this->lastResumeTime = {};
	this->hasResumedInThisRun = {};
	this->lastDuckValue = {};
	this->lastStaminaValue = {};
	this->validJump = {};
	this->lastInvalidateTime = {};
	this->touchedGroundSinceTouchingStartZone = {};
	this->shouldPlayTimerSound = true;
}

void BhopTimerService::OnPhysicsSimulatePost()
{
	if (this->player->IsAlive() && this->GetTimerRunning() && !this->GetPaused())
	{
		this->currentTime += ENGINE_FIXED_TICK_INTERVAL;
		this->CheckMissedTime();
	}
}

void BhopTimerService::OnStartTouchGround()
{
	this->touchedGroundSinceTouchingStartZone = true;
}

void BhopTimerService::OnStopTouchGround()
{
	if (this->HasValidMoveType() && this->lastInvalidateTime != g_pBhopUtils->GetServerGlobals()->curtime)
	{
		this->validJump = true;
	}
	else
	{
		this->InvalidateJump();
	}
}

void BhopTimerService::OnChangeMoveType(MoveType_t oldMoveType)
{
	if (oldMoveType == MOVETYPE_LADDER && this->player->GetMoveType() == MOVETYPE_WALK
		&& this->lastInvalidateTime != g_pBhopUtils->GetServerGlobals()->curtime)
	{
		this->validJump = true;
	}
	else
	{
		this->InvalidateJump();
	}
	// Check if player has escaped MOVETYPE_NONE
	if (!this->paused || this->player->GetMoveType() == MOVETYPE_NONE)
	{
		return;
	}

	this->paused = false;
	if (this->GetTimerRunning())
	{
		this->hasResumedInThisRun = true;
		this->lastResumeTime = g_pBhopUtils->GetServerGlobals()->curtime;
	}

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnResumePost(this->player);
	}
}

void BhopTimerService::OnTeleportToStart()
{
	this->TimerStop();
}

void BhopTimerService::OnClientDisconnect()
{
	this->TimerStop();
}

void BhopTimerService::OnPlayerSpawn()
{
	if (!this->player->GetPlayerPawn() || !this->paused)
	{
		return;
	}

	// Player has left paused state by spawning in, so resume
	this->paused = false;
	if (this->GetTimerRunning())
	{
		this->hasResumedInThisRun = true;
		this->lastResumeTime = g_pBhopUtils->GetServerGlobals()->curtime;
	}
	this->player->GetMoveServices()->m_flDuckAmount = this->lastDuckValue;
	this->player->GetMoveServices()->m_flStamina = this->lastStaminaValue;

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnResumePost(this->player);
	}
}

void BhopTimerService::OnPlayerJoinTeam(i32 team)
{
	if (team == CS_TEAM_SPECTATOR)
	{
		this->paused = true;
		if (this->GetTimerRunning())
		{
			this->hasPausedInThisRun = true;
			this->lastPauseTime = g_pBhopUtils->GetServerGlobals()->curtime;
		}

		FOR_EACH_VEC(eventListeners, i)
		{
			eventListeners[i]->OnPausePost(this->player);
		}
	}
}

void BhopTimerService::OnPlayerDeath()
{
	this->TimerStop();
}

void BhopTimerService::OnRoundStart()
{
	BhopTimerService::TimerStopAll();
}

void BhopTimerService::OnTeleport(const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity)
{
	if (newPosition || newVelocity)
	{
		this->InvalidateJump();
	}
}

SCMD(bhop_stop, SCFL_TIMER)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	if (player->timerService->GetTimerRunning())
	{
		player->timerService->TimerStop();
	}
	return MRES_SUPERCEDE;
}

SCMD(bhop_pause, SCFL_TIMER)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	player->timerService->TogglePause();
	return MRES_SUPERCEDE;
}

SCMD(bhop_comparelevel, SCFL_RECORD | SCFL_TIMER | SCFL_PREFERENCE)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	player->timerService->SetCompareTarget(args->Arg(1));
	return MRES_SUPERCEDE;
}

static_function BhopTimerService::CompareType GetCompareTypeFromString(const char *typeString)
{
	if (V_stricmp("off", typeString) == 0 || V_stricmp("none", typeString) == 0)
	{
		return BhopTimerService::CompareType::COMPARE_NONE;
	}
	if (V_stricmp("spb", typeString) == 0)
	{
		return BhopTimerService::CompareType::COMPARE_SPB;
	}
	if (V_stricmp("gpb", typeString) == 0 || V_stricmp("pb", typeString) == 0)
	{
		return BhopTimerService::CompareType::COMPARE_GPB;
	}
	if (V_stricmp("sr", typeString) == 0)
	{
		return BhopTimerService::CompareType::COMPARE_SR;
	}
	if (V_stricmp("wr", typeString) == 0)
	{
		return BhopTimerService::CompareType::COMPARE_WR;
	}
	return BhopTimerService::CompareType::COMPARETYPE_COUNT;
}

void BhopTimerService::SetCompareTarget(const char *typeString)
{
	if (!typeString || !V_stricmp("", typeString))
	{
		this->player->languageService->PrintChat(true, false, "Compare Command Usage");
		return;
	}

	CompareType type = GetCompareTypeFromString(typeString);
	if (type == COMPARETYPE_COUNT)
	{
		this->player->languageService->PrintChat(true, false, "Compare Command Usage");
		return;
	}

	assert(type < COMPARETYPE_COUNT && type >= COMPARE_NONE);
	switch (type)
	{
		case COMPARE_NONE:
		{
			this->player->languageService->PrintChat(true, false, "Compare Disabled");
			break;
		}
		case COMPARE_SPB:
		{
			this->player->languageService->PrintChat(true, false, "Compare Server PB");
			break;
		}
		case COMPARE_GPB:
		{
			this->player->languageService->PrintChat(true, false, "Compare Global PB");
			break;
		}
		case COMPARE_SR:
		{
			this->player->languageService->PrintChat(true, false, "Compare Server Record");
			break;
		}
		case COMPARE_WR:
		{
			this->player->languageService->PrintChat(true, false, "Compare World Record");
			break;
		}
	}
	this->preferredCompareType = type;
	this->player->optionService->SetPreferenceInt("preferredCompareType", this->preferredCompareType);
	if (this->GetCourse())
	{
		this->UpdateCurrentCompareType(ToPBDataKey(Bhop::mode::GetModeInfo(this->player->modeService).id, this->GetCourse()->guid));
	}
}

void BhopTimerService::UpdateCurrentCompareType(PBDataKey key)
{
	for (u8 type = this->preferredCompareType; type > COMPARE_NONE; type--)
	{
		if (this->GetCompareTargetForType((CompareType)type, key))
		{
			this->currentCompareType = (CompareType)type;
			return;
		}
	}
	this->currentCompareType = COMPARE_NONE;
}

const PBData *BhopTimerService::GetCompareTargetForType(CompareType type, PBDataKey key)
{
	switch (type)
	{
		case COMPARE_WR:
		{
			if (BhopTimerService::wrCache.find(key) != BhopTimerService::wrCache.end())
			{
				return &BhopTimerService::wrCache[key];
			}
			break;
		}
		case COMPARE_SR:
		{
			if (BhopTimerService::srCache.find(key) != BhopTimerService::srCache.end())
			{
				return &BhopTimerService::srCache[key];
			}
			break;
		}
		case COMPARE_GPB:
		{
			if (BhopTimerService::globalPBCache.find(key) != BhopTimerService::globalPBCache.end())
			{
				return &this->globalPBCache[key];
			}
			break;
		}
		case COMPARE_SPB:
		{
			if (BhopTimerService::localPBCache.find(key) != BhopTimerService::localPBCache.end())
			{
				return &this->localPBCache[key];
			}
			break;
		}
	}
	return nullptr;
}

const PBData *BhopTimerService::GetCompareTarget(PBDataKey key)
{
	switch (this->currentCompareType)
	{
		case COMPARE_WR:
		{
			if (BhopTimerService::wrCache.find(key) != BhopTimerService::wrCache.end())
			{
				return &BhopTimerService::wrCache[key];
			}
			break;
		}
		case COMPARE_SR:
		{
			if (BhopTimerService::srCache.find(key) != BhopTimerService::srCache.end())
			{
				return &BhopTimerService::srCache[key];
			}
			break;
		}
		case COMPARE_GPB:
		{
			if (BhopTimerService::globalPBCache.find(key) != BhopTimerService::globalPBCache.end())
			{
				return &this->globalPBCache[key];
			}
			break;
		}
		case COMPARE_SPB:
		{
			if (BhopTimerService::localPBCache.find(key) != BhopTimerService::localPBCache.end())
			{
				return &this->localPBCache[key];
			}
			break;
		}
	}
	return nullptr;
}

void BhopTimerService::ClearRecordCache()
{
	BhopTimerService::srCache.clear();
	BhopTimerService::wrCache.clear();
	for (i32 i = 0; i < MAXPLAYERS + 1; i++)
	{
		BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(i);
		if (player && player->timerService)
		{
			player->timerService->ClearPBCache();
		}
	}
}

void BhopTimerService::UpdateLocalRecordCache()
{
	auto onQuerySuccess = [](std::vector<ISQLQuery *> queries)
	{
		ISQLResult *result = queries[0]->GetResultSet();
		if (result && result->GetRowCount() > 0)
		{
			while (result->FetchRow())
			{
				auto modeInfo = Bhop::mode::GetModeInfoFromDatabaseID(result->GetInt(2));
				if (modeInfo.databaseID < 0)
				{
					continue;
				}
				const BhopCourseDescriptor *course = Bhop::course::GetCourseByLocalCourseID(result->GetInt(1));
				if (!course)
				{
					continue;
				}
				BhopTimerService::InsertRecordToCache(result->GetFloat(0), course, modeInfo.id, false, result->GetString(3));
			}
		}
	};
	BhopDatabaseService::QueryAllRecords(g_pBhopUtils->GetCurrentMapName(), onQuerySuccess, BhopDatabaseService::OnGenericTxnFailure);
}

void BhopTimerService::InsertRecordToCache(f64 time, const BhopCourseDescriptor *course, PluginId modeID, bool global, CUtlString metadata)
{
	PBData &pb = global ? BhopTimerService::wrCache[ToPBDataKey(modeID, course->guid)] : BhopTimerService::srCache[ToPBDataKey(modeID, course->guid)];

	pb.overall.pbTime = time;
	KeyValues3 kv(KV3_TYPEEX_TABLE, KV3_SUBTYPE_UNSPECIFIED);
	CUtlString error = "";
	if (metadata.IsEmpty())
	{
		return;
	}
	LoadKV3FromJSON(&kv, &error, metadata.Get(), "");
	if (!error.IsEmpty())
	{
		META_CONPRINTF("[Bhop::Timer] Failed to insert PB to cache due to metadata error: %s\n", error.Get());
		return;
	}

	KeyValues3 *data = kv.FindMember("cpZoneTimes");
	if (data && data->GetType() == KV3_TYPE_ARRAY)
	{
		for (i32 i = 0; i < course->checkpointCount; i++)
		{
			f64 time = -1.0f;
			KeyValues3 *element = data->GetArrayElement(i);
			if (element)
			{
				time = element->GetDouble(-1.0);
			}
			pb.overall.pbCpZoneTimes[i] = time;
		}
	}

	data = kv.FindMember("stageZoneTimes");
	if (data && data->GetType() == KV3_TYPE_ARRAY)
	{
		for (i32 i = 0; i < course->stageCount; i++)
		{
			f64 time = -1.0f;
			KeyValues3 *element = data->GetArrayElement(i);
			if (element)
			{
				time = element->GetDouble(-1.0);
			}
			pb.overall.pbStageZoneTimes[i] = time;
		}
	}
}

void BhopTimerService::ClearPBCache()
{
	this->localPBCache.clear();
}

const PBData *BhopTimerService::GetGlobalCachedPB(const BhopCourseDescriptor *course, PluginId modeID)
{
	PBDataKey key = ToPBDataKey(modeID, course->guid);

	if (this->globalPBCache.find(key) == this->globalPBCache.end())
	{
		return nullptr;
	}

	return &this->globalPBCache[key];
}

void BhopTimerService::InsertPBToCache(f64 time, const BhopCourseDescriptor *course, PluginId modeID, bool global, CUtlString metadata, f64 points)
{
	PBData &pb = global ? this->globalPBCache[ToPBDataKey(modeID, course->guid)] : this->localPBCache[ToPBDataKey(modeID, course->guid)];

	pb.overall.points = points;
	pb.overall.pbTime = time;
	KeyValues3 kv(KV3_TYPEEX_TABLE, KV3_SUBTYPE_UNSPECIFIED);
	CUtlString error = "";
	if (metadata.IsEmpty())
	{
		return;
	}
	LoadKV3FromJSON(&kv, &error, metadata.Get(), "");
	if (!error.IsEmpty())
	{
		META_CONPRINTF("[Bhop::Timer] Failed to insert server record to cache due to metadata error: %s\n", error.Get());
		return;
	}

	KeyValues3 *data = kv.FindMember("cpZoneTimes");
	if (data && data->GetType() == KV3_TYPE_ARRAY)
	{
		for (i32 i = 0; i < course->checkpointCount; i++)
		{
			f64 time = -1.0f;
			KeyValues3 *element = data->GetArrayElement(i);
			if (element)
			{
				time = element->GetDouble(-1.0);
			}
			pb.overall.pbCpZoneTimes[i] = time;
		}
	}

	data = kv.FindMember("stageZoneTimes");
	if (data && data->GetType() == KV3_TYPE_ARRAY)
	{
		for (i32 i = 0; i < course->stageCount; i++)
		{
			f64 time = -1.0f;
			KeyValues3 *element = data->GetArrayElement(i);
			if (element)
			{
				time = element->GetDouble(-1.0);
			}
			pb.overall.pbStageZoneTimes[i] = time;
		}
	}
}

void BhopTimerService::CheckMissedTime()
{
	const BhopCourseDescriptor *course = this->GetCourse();
	// No active course, the timer is not running or if we already announce late PBs.
	if (!course || !this->GetTimerRunning() || !this->shouldAnnounceMissedTime)
	{
		return;
	}
	// No comparison available for styled runs.
	if (this->player->styleServices.Count() > 0)
	{
		return;
	}
	auto modeInfo = Bhop::mode::GetModeInfo(this->player->modeService->GetModeName());

	PBDataKey key = ToPBDataKey(modeInfo.id, course->guid);

	// Check if there is personal best data for this mode and course.
	auto pb = this->GetCompareTarget(key);
	if (!pb)
	{
		return;
	}

	if (this->shouldAnnounceMissedTime && pb->overall.pbTime > 0 && this->GetTime() > pb->overall.pbTime)
	{
		CUtlString timeText = utils::FormatTime(pb->overall.pbTime);
		this->player->languageService->PrintChat(true, false, missedTimeKeys[this->currentCompareType], timeText.Get());
		this->shouldAnnounceMissedTime = false;
		this->PlayMissedTimeSound();
	}
}

void BhopTimerService::ShowCheckpointText(u32 currentCheckpoint)
{
	const BhopCourseDescriptor *course = this->GetCourse();
	// No active course so we can't compare anything.
	if (!course)
	{
		return;
	}
	// No comparison available for styled runs.
	if (this->player->styleServices.Count() > 0)
	{
		return;
	}

	CUtlString time;
	std::string pbDiff = "";

	time = utils::FormatTime(this->cpZoneTimes[currentCheckpoint - 1]);
	if (this->lastCheckpoint != 0)
	{
		f64 diff = this->cpZoneTimes[currentCheckpoint - 1] - this->cpZoneTimes[this->lastCheckpoint - 1];
		CUtlString splitTime = BhopTimerService::FormatDiffTime(diff);
		splitTime.Format(" {grey}({default}%s{grey})", splitTime.Get());
		time.Append(splitTime.Get());
	}

	auto modeInfo = Bhop::mode::GetModeInfo(this->player->modeService->GetModeName());
	PBDataKey key = ToPBDataKey(modeInfo.id, course->guid);

	// Check if there is personal best data for this mode and course.
	const PBData *pb = this->GetCompareTarget(key);
	if (pb)
	{
		if (pb->overall.pbCpZoneTimes[currentCheckpoint - 1] > 0)
		{
			f64 diff = this->cpZoneTimes[currentCheckpoint - 1] - pb->overall.pbCpZoneTimes[currentCheckpoint - 1];
			CUtlString diffText = BhopTimerService::FormatDiffTime(diff);
			diffText.Format("{grey}%s%s{grey}", diff < 0 ? "{green}" : "{lightred}", diffText.Get());
			pbDiff = this->player->languageService->PrepareMessage(diffTextKeys[this->currentCompareType], diffText.Get());
		}
	}

	this->player->languageService->PrintChat(true, false, "Course Checkpoint Reached", currentCheckpoint, time.Get(), pbDiff.c_str());
}

void BhopTimerService::ShowStageText()
{
	const BhopCourseDescriptor *course = this->GetCourse();
	// No active course so we can't compare anything.
	if (!course)
	{
		return;
	}
	// No comparison available for styled runs.
	if (this->player->styleServices.Count() > 0)
	{
		return;
	}

	CUtlString time;
	std::string pbDiff = "";

	time = utils::FormatTime(this->stageZoneTimes[this->currentStage - 1]);

	auto modeInfo = Bhop::mode::GetModeInfo(this->player->modeService->GetModeName());
	PBDataKey key = ToPBDataKey(modeInfo.id, course->guid);

	// Check if there is personal best data for this mode and course.
	const PBData *pb = this->GetCompareTarget(key);
	if (pb)
	{
		if (pb->overall.pbStageZoneTimes[this->currentStage - 1] > 0)
		{
			f64 diff = this->stageZoneTimes[this->currentStage - 1] - pb->overall.pbStageZoneTimes[this->currentStage - 1];
			CUtlString diffText = BhopTimerService::FormatDiffTime(diff);
			diffText.Format("{grey}%s%s{grey}", diff < 0 ? "{green}" : "{lightred}", diffText.Get());
			pbDiff = this->player->languageService->PrepareMessage(diffTextKeys[this->currentCompareType], diffText.Get());
		}
	}

	this->player->languageService->PrintChat(true, false, "Course Stage Reached", this->currentStage, time.Get(), pbDiff.c_str());
}

CUtlString BhopTimerService::GetCurrentRunMetadata()
{
	KeyValues3 kv(KV3_TYPEEX_TABLE, KV3_SUBTYPE_UNSPECIFIED);

	KeyValues3 *cpZoneTimesKV = kv.FindOrCreateMember("cpZoneTimes");
	cpZoneTimesKV->SetToEmptyArray();
	FOR_EACH_VEC(this->cpZoneTimes, i)
	{
		KeyValues3 *time = cpZoneTimesKV->ArrayAddElementToTail();
		time->SetDouble(this->cpZoneTimes[i]);
	}

	KeyValues3 *stageZoneTimesKV = kv.FindOrCreateMember("stageZoneTimes");
	FOR_EACH_VEC(this->stageZoneTimes, i)
	{
		KeyValues3 *time = stageZoneTimesKV->ArrayAddElementToTail();
		time->SetDouble(this->stageZoneTimes[i]);
	}

	CUtlString result, error;
	if (SaveKV3AsJSON(&kv, &error, &result))
	{
		return result;
	}
	META_CONPRINTF("[Bhop::Timer] Failed to obtain current run's metadata! (%s)\n", error.Get());
	return "";
}

void BhopTimerService::UpdateLocalPBCache()
{
	CPlayerUserId uid = player->GetClient()->GetUserID();

	auto onQuerySuccess = [uid](std::vector<ISQLQuery *> queries)
	{
		BhopPlayer *pl = g_pBhopPlayerManager->ToPlayer(uid);
		if (!pl)
		{
			return;
		}
		ISQLResult *result = queries[0]->GetResultSet();
		if (result && result->GetRowCount() > 0)
		{
			while (result->FetchRow())
			{
				auto modeInfo = Bhop::mode::GetModeInfoFromDatabaseID(result->GetInt(2));
				if (modeInfo.databaseID < 0)
				{
					continue;
				}
				const BhopCourseDescriptor *course = Bhop::course::GetCourseByLocalCourseID(result->GetInt(1));
				if (!course)
				{
					continue;
				}
				pl->timerService->InsertPBToCache(result->GetFloat(0), course, modeInfo.id, false, result->GetString(3));
			}
		}
	};
	BhopDatabaseService::QueryAllPBs(player->GetSteamId64(), g_pBhopUtils->GetCurrentMapName(), onQuerySuccess,
									 BhopDatabaseService::OnGenericTxnFailure);
}

std::string BhopTimerService::GetStartSpeedText(const char *language)
{
	Vector velocity, baseVelocity;
	this->player->GetVelocity(&velocity);
	this->player->GetBaseVelocity(&baseVelocity);
	velocity += baseVelocity;

	float startSpeed = velocity.Length2D();
	return BhopLanguageService::PrepareMessageWithLang(language, "Start Speed", startSpeed);
}

void BhopTimerService::Init()
{
	BhopDatabaseService::RegisterEventListener(&databaseEventListener);
	BhopOptionService::RegisterEventListener(&optionEventListener);
}

void BhopTimerService::OnPlayerPreferencesLoaded()
{
	if (this->player->optionService->GetPreferenceInt("preferredCompareType", COMPARE_GPB) > COMPARETYPE_COUNT)
	{
		this->preferredCompareType = COMPARE_GPB;
		return;
	}
	this->preferredCompareType = (CompareType)this->player->optionService->GetPreferenceInt("preferredCompareType", COMPARE_GPB);
	this->shouldPlayTimerSound = this->player->optionService->GetPreferenceBool("timerStopSound", true);
}

void BhopDatabaseServiceEventListener_Timer::OnMapSetup()
{
	Bhop::course::SetupLocalCourses();
}

void BhopDatabaseServiceEventListener_Timer::OnCoursesSetup()
{
	BhopTimerService::UpdateLocalRecordCache();
}

void BhopDatabaseServiceEventListener_Timer::OnClientSetup(Player *player, u64 steamID64, bool isCheater)
{
	BhopPlayer *BhopPlayer = g_pBhopPlayerManager->ToBhopPlayer(player);
	BhopPlayer->timerService->UpdateLocalPBCache();
}
