#include "cs2bhop.h"
#include "bhop_recording.h"
#include "bhop/timer/bhop_timer.h"
#include "bhop/checkpoint/bhop_checkpoint.h"
#include "bhop/replays/bhop_replaysystem.h"

extern CConVar<bool> bhop_replay_recording_debug;

// Timer event listener implementation
class TimerEventListener : public BhopTimerServiceEventListener
{
public:
	virtual void OnTimerStartPost(BhopPlayer *player, u32 courseGUID) override
	{
		player->recordingService->OnTimerStart();
	}

	virtual void OnTimerEndPost(BhopPlayer *player, u32 courseGUID, f32 time) override
	{
		player->recordingService->OnTimerEnd();
	}

	virtual void OnTimerStopped(BhopPlayer *player, u32 courseGUID) override
	{
		player->recordingService->OnTimerStop();
	}

	virtual void OnTimerInvalidated(BhopPlayer *player) override
	{
		// player->PrintChat(false, false, "timerinvalidated");
	}

	virtual void OnPausePost(BhopPlayer *player) override
	{
		player->recordingService->OnPause();
	}

	virtual void OnResumePost(BhopPlayer *player) override
	{
		player->recordingService->OnResume();
	}

	virtual void OnCheckpointZoneTouchPost(BhopPlayer *player, u32 checkpoint) override
	{
		player->recordingService->OnCPZ(checkpoint);
	}

	virtual void OnStageZoneTouchPost(BhopPlayer *player, u32 stageZone) override
	{
		player->recordingService->OnStage(stageZone);
	}
} timerEventListener;

void BhopRecordingService::Init()
{
	BhopTimerService::RegisterEventListener(&timerEventListener);
	fileWriter = new ReplayFileWriter();
}

void BhopRecordingService::Shutdown()
{
	BhopTimerService::UnregisterEventListener(&timerEventListener);
	fileWriter->Stop();
	delete fileWriter;
	fileWriter = nullptr;
}

void BhopRecordingService::OnActivateServer()
{
	for (i32 i = 0; i < MAXPLAYERS + 1; i++)
	{
		BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(i);
		if (player && player->recordingService)
		{
			player->recordingService->Reset();
		}
	}
}

void BhopRecordingService::ProcessFileWriteCompletion()
{
	if (BhopRecordingService::fileWriter)
	{
		BhopRecordingService::fileWriter->RunFrame();
	}
}

void BhopRecordingService::OnProcessUsercmds(PlayerCommand *cmds, i32 numCmds)
{
	if (Bhop::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	if (numCmds <= 0)
	{
		return;
	}

	// record data for replay playback
	if (this->player->IsFakeClient())
	{
		return;
	}

	this->RecordCommand(cmds, numCmds);
}

void BhopRecordingService::OnTimerStart()
{
	if (Bhop::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	this->runRecorders.push_back(RunRecorder(this->player));
	if (bhop_replay_recording_debug.Get())
	{
		META_CONPRINTF("bhop_replay_recording_debug: Timer start\n");
	}
	this->InsertTimerEvent(RpEvent::RpEventData::TimerEvent::TIMER_START, this->player->timerService->GetTime(),
						   this->player->timerService->GetCourse()->id);
	// Reset currentRunUUID to invalid state at timer start
	this->currentRunUUID = UUID_t(false);
}

void BhopRecordingService::OnTimerStop()
{
	if (Bhop::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	if (bhop_replay_recording_debug.Get())
	{
		META_CONPRINTF("bhop_replay_recording_debug: Timer stop\n");
	}
	this->InsertTimerEvent(RpEvent::RpEventData::TimerEvent::TIMER_STOP, this->player->timerService->GetTime());

	// Remove all active run recorders, which are the ones without desired stop time set.
	auto it = this->runRecorders.begin();
	while (it != this->runRecorders.end())
	{
		if (it->desiredStopTime < 0.0f)
		{
			it = this->runRecorders.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void BhopRecordingService::OnTimerEnd()
{
	if (Bhop::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	if (bhop_replay_recording_debug.Get())
	{
		META_CONPRINTF("bhop_replay_recording_debug: Timer end\n");
	}
	this->InsertTimerEvent(RpEvent::RpEventData::TimerEvent::TIMER_END, this->player->timerService->GetTime(),
						   this->player->timerService->GetCourse()->id);

	for (auto &recorder : this->runRecorders)
	{
		if (recorder.desiredStopTime < 0.0f)
		{
			recorder.End(this->player->timerService->GetTime(), this->player->checkpointService->GetTeleportCount());
			// Generate UUID now (at timer end) so it's available for RecordAnnounce
			// File will be written later after breather time
			recorder.uuid = UUID_t(true);
			this->currentRunUUID = recorder.uuid;
		}
	}
}

void BhopRecordingService::OnPause()
{
	if (Bhop::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	if (bhop_replay_recording_debug.Get())
	{
		META_CONPRINTF("bhop_replay_recording_debug: Timer pause\n");
	}
	this->InsertTimerEvent(RpEvent::RpEventData::TimerEvent::TIMER_PAUSE,
						   this->player->timerService->GetTimerRunning() ? this->player->timerService->GetTime() : 0.0f);
}

void BhopRecordingService::OnResume()
{
	if (Bhop::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	if (bhop_replay_recording_debug.Get())
	{
		META_CONPRINTF("bhop_replay_recording_debug: Timer resume\n");
	}
	this->InsertTimerEvent(RpEvent::RpEventData::TimerEvent::TIMER_RESUME,
						   this->player->timerService->GetTimerRunning() ? this->player->timerService->GetTime() : 0.0f);
}

void BhopRecordingService::OnSplit(i32 split)
{
	if (Bhop::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	if (bhop_replay_recording_debug.Get())
	{
		META_CONPRINTF("bhop_replay_recording_debug: Timer split %d\n", split);
	}
	this->InsertTimerEvent(RpEvent::RpEventData::TimerEvent::TIMER_SPLIT, this->player->timerService->GetTime(), split);
}

void BhopRecordingService::OnCPZ(i32 cpz)
{
	if (Bhop::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	if (bhop_replay_recording_debug.Get())
	{
		META_CONPRINTF("bhop_replay_recording_debug: Checkpoint %d\n", cpz);
	}
	this->InsertTimerEvent(RpEvent::RpEventData::TimerEvent::TIMER_CPZ, this->player->timerService->GetTime(), cpz);
}

void BhopRecordingService::OnStage(i32 stage)
{
	if (Bhop::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	if (bhop_replay_recording_debug.Get())
	{
		META_CONPRINTF("bhop_replay_recording_debug: Stage %d\n", stage);
	}
	this->InsertTimerEvent(RpEvent::RpEventData::TimerEvent::TIMER_STAGE, this->player->timerService->GetTime(), stage);
}

void BhopRecordingService::OnTeleport(const Vector *origin, const QAngle *angles, const Vector *velocity)
{
	if (Bhop::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	if (bhop_replay_recording_debug.Get())
	{
		META_CONPRINTF("bhop_replay_recording_debug: Teleport\n");
	}
	this->InsertTeleportEvent(origin, angles, velocity);
}

void BhopRecordingService::OnClientDisconnect()
{
	for (auto &recorder : this->runRecorders)
	{
		if (recorder.desiredStopTime > 0.0f && fileWriter)
		{
			auto recorderPtr = std::make_unique<RunRecorder>(std::move(recorder));
			this->CopyWeaponsToRecorder(recorderPtr.get());
			fileWriter->QueueWriteToFile(std::move(recorderPtr));
		}
	}
	this->runRecorders.clear();

	// Clean up circular recorder when player disconnects
	delete this->circularRecording;
	this->circularRecording = nullptr;
}

void BhopRecordingService::OnPhysicsSimulate()
{
	if (Bhop::replaysystem::IsReplayBot(this->player))
	{
		return;
	}

	// record data for replay playback
	if (!this->player->IsAlive() || this->player->IsFakeClient())
	{
		return;
	}

	this->RecordTickData_PhysicsSimulate();
}

void BhopRecordingService::OnSetupMove(PlayerCommand *pc)
{
	if (Bhop::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	if (!this->player->IsAlive() || this->player->IsFakeClient())
	{
		return;
	}
	this->RecordTickData_SetupMove(pc);
}

void BhopRecordingService::OnPhysicsSimulatePost()
{
	if (this->player->IsFakeClient() || Bhop::replaysystem::IsReplayBot(this->player))
	{
		return;
	}

	this->CheckRecorders();

	// record data for replay playback
	if (!this->player->IsAlive())
	{
		return;
	}
	this->CheckWeapons();
	this->RecordTickData_PhysicsSimulatePost();
	this->CheckModeStyles();
	this->CheckCheckpoints();

	// Remove old events from circular buffer (keep 2 minutes)
	u32 currentTick = g_pBhopUtils->GetServerGlobals()->tickcount;
	if (this->circularRecording)
	{
		this->circularRecording->TrimOldData(currentTick);
	}
}
