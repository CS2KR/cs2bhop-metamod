#include "cs2bhop.h"
#include "bhop/bhop.h"
#include "bhop_replaysystem.h"
#include "bot.h"
#include "data.h"
#include "playback.h"
#include "events.h"
#include "commands.h"

namespace Bhop::replaysystem
{

	void Init()
	{
		Bhop::replaysystem::item::InitItemAttributes();
	}

	void Cleanup()
	{
		bot::KickBot();
		CleanupWatcher();
	}

	void OnRoundStart()
	{
		bot::KickBot();
	}

	void OnGameFrame()
	{
		// Process any completed async loads on the main thread
		data::ProcessAsyncLoadCompletion();
	}

	void OnPhysicsSimulate(BhopPlayer *player)
	{
		playback::OnPhysicsSimulate(player);
	}

	void OnProcessMovement(BhopPlayer *player)
	{
		playback::OnProcessMovement(player);
	}

	void OnProcessMovementPost(BhopPlayer *player)
	{
		playback::OnProcessMovementPost(player);
	}

	void OnFinishMovePre(BhopPlayer *player, CMoveData *pMoveData)
	{
		playback::OnFinishMovePre(player, pMoveData);
	}

	void OnPhysicsSimulatePost(BhopPlayer *player)
	{
		playback::OnPhysicsSimulatePost(player);
	}

	void OnPlayerRunCommandPre(BhopPlayer *player, PlayerCommand *command)
	{
		playback::OnPlayerRunCommandPre(player, command);
	}

	bool IsReplayBot(BhopPlayer *player)
	{
		return bot::IsValidBot(player ? player->GetController() : nullptr);
	}

	bool CanTouchTrigger(BhopPlayer *player, CBaseTrigger *trigger)
	{
		// Don't care about non-bot players.
		if (!bot::IsValidBot(player->GetController()))
		{
			return true;
		}

		// Don't care about non timer triggers.
		const BhopTrigger *bhopTrigger = Bhop::mapapi::GetBhopTrigger(trigger);
		if (!bhopTrigger)
		{
			return true;
		}

		if (Bhop::mapapi::IsTimerTrigger(bhopTrigger->type) || Bhop::mapapi::IsTeleportTrigger(bhopTrigger->type))
		{
			return false;
		}

		return true;
	}

	i32 GetCurrentCpIndex()
	{
		return data::GetCurrentCpIndex();
	}

	i32 GetCheckpointCount()
	{
		return data::GetCheckpointCount();
	}

	i32 GetTeleportCount()
	{
		return data::GetTeleportCount();
	}

	f32 GetTime()
	{
		return data::GetReplayTime();
	}

	f32 GetEndTime()
	{
		return data::GetEndTime();
	}

	bool GetPaused()
	{
		return data::GetPaused();
	}

} // namespace Bhop::replaysystem
