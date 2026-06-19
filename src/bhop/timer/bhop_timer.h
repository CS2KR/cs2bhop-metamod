#pragma once

#include "../bhop.h"
#include "../checkpoint/bhop_checkpoint.h"
#include "bhop/mappingapi/bhop_mappingapi.h"
#include "utils/uuid.h"

#define BHOP_MAX_MODE_NAME_LENGTH 128

#define BHOP_TIMER_MIN_GROUND_TIME 0.5f

#define BHOP_TIMER_SOUND_COOLDOWN       0.15f
#define BHOP_TIMER_SND_START            "Buttons.snd9"
#define BHOP_TIMER_SND_END              "tr.ScoreRegular"
#define BHOP_TIMER_SND_FALSE_END        "UIPanorama.buymenu_failure"
#define BHOP_TIMER_SND_MISSED_ZONE      "UIPanorama.buymenu_failure"
#define BHOP_TIMER_SND_REACH_CHECKPOINT "tr.Popup"
#define BHOP_TIMER_SND_REACH_STAGE      "UIPanorama.round_report_odds_up"
#define BHOP_TIMER_SND_STOP             "tr.PuckFail"
#define BHOP_TIMER_SND_MISSED_TIME      "UI.RankDown"

#define BHOP_PAUSE_COOLDOWN 1.0f

struct PBData
{
	PBData()
	{
		Reset();
	}

	void Reset()
	{
		overall.pbTime = {};
		overall.pbCpZoneTimes.SetCount(BHOP_MAX_CHECKPOINT_ZONES);
		overall.pbCpZoneTimes.FillWithValue(-1.0);
		overall.pbStageZoneTimes.SetCount(BHOP_MAX_STAGE_ZONES);
		overall.pbStageZoneTimes.FillWithValue(-1.0);
	}

	struct
	{
		f64 pbTime {};
		f64 points {};
		CUtlVectorFixed<f64, BHOP_MAX_CHECKPOINT_ZONES> pbCpZoneTimes;
		CUtlVectorFixed<f64, BHOP_MAX_STAGE_ZONES> pbStageZoneTimes;
	} overall;
};

// Convert mode and course ID to one single value.
typedef u64 PBDataKey;

inline PBDataKey ToPBDataKey(u32 modeID, u32 courseID)
{
	return modeID | ((u64)courseID << 32);
}

inline void ConvertFromPBDataKey(PBDataKey key, uint32_t *modeID, uint32_t *courseID)
{
	if (modeID)
	{
		*modeID = (uint32_t)key;
	}
	if (courseID)
	{
		*courseID = (uint32_t)(key >> 32);
	}
}

class BhopTimerServiceEventListener
{
public:
	virtual bool OnTimerStart(BhopPlayer *player, u32 courseGUID)
	{
		return true;
	}

	virtual void OnTimerStartPost(BhopPlayer *player, u32 courseGUID) {}

	virtual bool OnTimerEnd(BhopPlayer *player, u32 courseGUID, f32 time)
	{
		return true;
	}

	virtual void OnTimerEndPost(BhopPlayer *player, u32 courseGUID, f32 time) {}

	virtual void OnTimerStopped(BhopPlayer *player, u32 courseGUID) {}

	virtual void OnTimerInvalidated(BhopPlayer *player) {}

	virtual bool OnPause(BhopPlayer *player)
	{
		return true;
	}

	virtual void OnPausePost(BhopPlayer *player) {}

	virtual bool OnResume(BhopPlayer *player)
	{
		return true;
	}

	virtual void OnResumePost(BhopPlayer *player) {}

	virtual void OnCheckpointZoneTouchPost(BhopPlayer *player, u32 checkpointZone) {}

	virtual void OnStageZoneTouchPost(BhopPlayer *player, u32 stageZone) {}
};

class BhopTimerService : public BhopBaseService
{
	using BhopBaseService::BhopBaseService;

private:
	bool timerRunning {};
	f64 currentTime {};
	u32 currentCourseGUID {};
	f64 lastEndTime {};
	f64 lastFalseEndTime {};
	f64 lastStartSoundTime {};
	f64 lastMissedTimeSoundTime {};
	bool validTime {};
	bool inStartzone {};

	u32 lastCheckpoint {};
	i32 reachedCheckpoints {};
	CUtlVectorFixed<f64, BHOP_MAX_CHECKPOINT_ZONES> cpZoneTimes {};

	i32 currentStage {};
	CUtlVectorFixed<f64, BHOP_MAX_STAGE_ZONES> stageZoneTimes {};
	CUtlVectorFixed<f64, BHOP_MAX_STAGE_ZONES> stageEndTouchTimes {};

	// PB cache per mode and per course.
	std::unordered_map<PBDataKey, PBData> localPBCache;
	std::unordered_map<PBDataKey, PBData> globalPBCache;

	// SR cache should be loaded upon map start, every time !wr is queried and every time a run beats the server record.
	static std::unordered_map<PBDataKey, PBData> srCache;

	static std::unordered_map<PBDataKey, PBData> wrCache;

public:
	enum CompareType : u8
	{
		COMPARE_NONE = 0,
		COMPARE_SPB, // Local PB
		COMPARE_GPB, // Global PB
		COMPARE_SR,  // Server Record
		COMPARE_WR,  // Global Record
		COMPARETYPE_COUNT
	};

private:
	// The maximum level that we should compare our current time with.
	// For example, if the value is set to COMPARE_GPB, the player will not attempt to compare their splits with SR/WR,
	// but only global PB, and local PB if global data is not available.
	CompareType preferredCompareType = COMPARE_GPB;

	// What we are currently comparing our run against in this current run.
	// This stays the same from the start of the run (unless preferredCompareType changes) to have a consistent comparison across the run.
	CompareType currentCompareType = COMPARE_GPB;

	void UpdateCurrentCompareType(PBDataKey key);
	const PBData *GetCompareTargetForType(CompareType type, PBDataKey key);
	const PBData *GetCompareTarget(PBDataKey key);

	bool shouldAnnounceMissedTime = true;

public:
	static void ClearRecordCache();
	static void UpdateLocalRecordCache();
	static void InsertRecordToCache(f64 time, const BhopCourseDescriptor *courseName, PluginId modeID, bool global, CUtlString metadata = "");

	void ClearPBCache();
	const PBData *GetGlobalCachedPB(const BhopCourseDescriptor *course, PluginId modeID);
	void UpdateLocalPBCache();
	void InsertPBToCache(f64 time, const BhopCourseDescriptor *courseName, PluginId modeID, bool global, CUtlString metadata = "", f64 points = 0);
	void SetCompareTarget(const char *typeString);

	void CheckMissedTime();

	void ShowCheckpointText(u32 currentCheckpoint);
	void ShowStageText();

	CUtlString GetCurrentRunMetadata();

private:
	bool validJump {};
	f64 lastInvalidateTime {};

public:
	static void Init();
	static bool RegisterEventListener(BhopTimerServiceEventListener *eventListener);
	static bool UnregisterEventListener(BhopTimerServiceEventListener *eventListener);

	bool GetTimerRunning()
	{
		return timerRunning;
	}

	bool GetValidTimer()
	{
		return validTime;
	}

	f64 GetTime()
	{
		return currentTime;
	}

	i32 GetStage()
	{
		return currentStage;
	}

	void SetStage(int stage)
	{
		currentStage = stage;
	}

	bool InStartzone()
	{
		return inStartzone;
	}

	std::string GetStartSpeedText(const char *language);

	static void FormatDiffTime(f64 time, char *output, u32 length, bool precise = true)
	{
		char temp[32];
		if (time > 0)
		{
			utils::FormatTime(time, temp, sizeof(temp), precise);
			V_snprintf(output, length, "+%s", temp);
		}
		else
		{
			utils::FormatTime(-time, temp, sizeof(temp), precise);
			V_snprintf(output, length, "-%s", temp);
		}
	}

	static CUtlString FormatDiffTime(f64 time, bool precise = true)
	{
		char temp[32];
		FormatDiffTime(time, temp, sizeof(temp), precise);
		return CUtlString(temp);
	}

	void SetTime(f64 time)
	{
		currentTime = time;
		timerRunning = time > 0.0f;
	}

	const BhopCourseDescriptor *GetCourse()
	{
		return Bhop::course::GetCourse(currentCourseGUID);
	}

	void SetCourse(u32 courseGUID)
	{
		currentCourseGUID = courseGUID;
	}

	enum TimeType_t
	{
		TimeType_Standard
	};

	TimeType_t GetCurrentTimeType()
	{
		return TimeType_Standard;
	}

	void StartZoneStartTouch(const BhopCourseDescriptor *course);
	void StartZoneEndTouch(const BhopCourseDescriptor *course);
	void CheckpointZoneStartTouch(const BhopCourseDescriptor *course, i32 cpNumber);
	void StageZoneStartTouch(const BhopCourseDescriptor *course, i32 stageNumber);
	void StageZoneEndTouch(const BhopCourseDescriptor *course, i32 stageNumber);
	bool TimerStart(const BhopCourseDescriptor *course, bool playSound = true);
	bool TimerEnd(const BhopCourseDescriptor *course);
	bool TimerStop(bool playSound = true);
	static void TimerStopAll(bool playSound = true);

	bool GetValidJump()
	{
		return validJump;
	}

	void InvalidateJump();
	void PlayTimerStartSound();

	// To be used for saveloc.
	void InvalidateRun();

private:
	bool HasValidMoveType();

	static bool IsValidMoveType(MoveType_t moveType)
	{
		return moveType == MOVETYPE_WALK || moveType == MOVETYPE_LADDER || moveType == MOVETYPE_NONE || moveType == MOVETYPE_OBSERVER;
	}

	bool JustLanded()
	{
		return g_pBhopUtils->GetGlobals()->curtime - this->player->landingTime < BHOP_TIMER_MIN_GROUND_TIME;
	}

	bool JustStartedTimer()
	{
		return timerRunning && this->GetTime() < EPSILON;
	}

	bool JustEndedTimer();

public:
	void PlayTimerEndSound();
	void PlayTimerFalseEndSound();
	void PlayMissedZoneSound();
	void PlayReachedCheckpointSound();
	void PlayReachedStageSound();
	void PlayTimerStopSound();
	void PlayMissedTimeSound();

	/*
	 * Pause stuff also goes here.
	 */

private:
	bool paused {};
	bool pausedOnLadder {};
	f32 lastPauseTime {};
	bool hasPausedInThisRun {};
	f32 lastResumeTime {};
	bool hasResumedInThisRun {};
	f32 lastDuckValue {};
	f32 lastStaminaValue {};
	bool touchedGroundSinceTouchingStartZone {};

public:
	bool GetPaused()
	{
		return paused;
	}

	void SetPausedOnLadder(bool ladder)
	{
		pausedOnLadder = ladder;
	}

	void Pause();
	bool CanPause(bool showError = false);
	void Resume(bool force = false);
	bool CanResume(bool showError = false);

	void TogglePause();

	void ToggleTimerStopSound();
	bool shouldPlayTimerSound = true;

public:
	virtual void Reset() override;
	void OnPhysicsSimulatePost();
	void OnStartTouchGround();
	void OnStopTouchGround();
	void OnChangeMoveType(MoveType_t oldMoveType);
	void OnTeleportToStart();
	void OnClientDisconnect();
	void OnPlayerSpawn();
	void OnPlayerJoinTeam(i32 team);
	void OnPlayerDeath();
	static void OnRoundStart();
	void OnTeleport(const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity);

	void OnPlayerPreferencesLoaded();
};
