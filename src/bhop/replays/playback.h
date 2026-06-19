#ifndef BHOP_REPLAYPLAYBACK_H
#define BHOP_REPLAYPLAYBACK_H
#include "common.h"
#include "sdk/datatypes.h"

class BhopPlayer;
class PlayerCommand;
class CMoveData;
struct TickData;
struct SubtickData;
class CBasePlayerWeapon;
struct EconInfo;

namespace Bhop::replaysystem::playback
{
	// Core playback functions
	void OnPhysicsSimulate(BhopPlayer *player);
	void OnProcessMovement(BhopPlayer *player);
	void OnProcessMovementPost(BhopPlayer *player);
	void OnFinishMovePre(BhopPlayer *player, CMoveData *mv);
	void OnPhysicsSimulatePost(BhopPlayer *player);
	void OnPlayerRunCommandPre(BhopPlayer *player, PlayerCommand *command);

	// Weapon management during playback
	void CheckWeapon(BhopPlayer &player, PlayerCommand &cmd);
	void InitializeWeapons();

	// Playback state management
	void StartReplay();

	// Navigation support
	void NavigateToTick(u32 targetTick);
	void ApplyTickState(BhopPlayer *player, const TickData *tickData);
} // namespace Bhop::replaysystem::playback

#endif // BHOP_REPLAYPLAYBACK_H
