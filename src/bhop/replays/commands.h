#ifndef BHOP_REPLAYCOMMANDS_H
#define BHOP_REPLAYCOMMANDS_H

#include "sdk/datatypes.h"

class BhopPlayer;

namespace Bhop::replaysystem::commands
{
	// Navigation functions
	void NavigateReplay(BhopPlayer *player, u32 targetTick);

	// Command handlers
	void LoadReplay(BhopPlayer *player, const char *uuid);
	void CheckReplayLoadProgress(BhopPlayer *player);
	void CancelReplayLoad(BhopPlayer *player);
	void JumpToReplayTime(BhopPlayer *player, const char *input);
	void JumpToReplayTick(BhopPlayer *player, const char *input);
	void GetReplayInfo(BhopPlayer *player);
	void ToggleReplayPause(BhopPlayer *player);
	void ListReplays(BhopPlayer *player, const char *input);
	void ToggleLegsVisibility(BhopPlayer *player);
} // namespace Bhop::replaysystem::commands

#endif // BHOP_REPLAYCOMMANDS_H
