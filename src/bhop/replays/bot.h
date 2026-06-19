#ifndef BHOP_REPLAYBOT_H
#define BHOP_REPLAYBOT_H

#include "sdk/datatypes.h"
#include "bhop_replay.h"

class CCSPlayerController;
class BhopPlayer;

namespace Bhop::replaysystem::bot
{
	// Bot lifecycle management
	void SpawnBot();
	void KickBot();
	void MakeBotAlive();
	void MoveBotToSpec();

	// Bot state management
	CCSPlayerController *GetBot();
	bool IsValidBot(CCSPlayerController *controller);
	BhopPlayer *GetBotPlayer();

	// Bot setup and configuration
	void InitializeBotForReplay(const ReplayHeader &header);

	// Bot spectator handling
	void SpectateBot(BhopPlayer *spectator);
} // namespace Bhop::replaysystem::bot

#endif // BHOP_REPLAYBOT_H
