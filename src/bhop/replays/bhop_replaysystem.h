
#ifndef BHOP_REPLAYSYSTEM_H
#define BHOP_REPLAYSYSTEM_H
#include "sdk/datatypes.h"

class BhopPlayer;
class CCSPlayerPawnBase;
class PlayerCommand;
class CUserCmd;
class CBasePlayerWeapon;
struct EconInfo;
class CBaseTrigger;

namespace Bhop::replaysystem
{
	void Init();
	void Cleanup();
	void OnRoundStart();
	void OnGameFrame();
	void OnPhysicsSimulate(BhopPlayer *player);
	void OnProcessMovement(BhopPlayer *player);
	void OnProcessMovementPost(BhopPlayer *player);
	void OnFinishMovePre(BhopPlayer *player, CMoveData *pMoveData);
	void OnPhysicsSimulatePost(BhopPlayer *player);
	void OnPlayerRunCommandPre(BhopPlayer *player, PlayerCommand *command);
	bool IsReplayBot(BhopPlayer *player);
	bool CanTouchTrigger(BhopPlayer *player, CBaseTrigger *trigger);

	namespace item
	{
		void InitItemAttributes();
		std::string GetItemAttributeName(u16 id);
		std::string GetWeaponName(u16 id);
		gear_slot_t GetWeaponGearSlot(u16 id);
		bool DoesPaintKitUseLegacyModel(float paintKit);
		void ApplyItemAttributesToWeapon(CBasePlayerWeapon &weapon, const EconInfo &info);
		void ApplyModelAttributesToPawn(CCSPlayerPawn *pawn, const EconInfo &info, const char *modelName);
	} // namespace item

	i32 GetCurrentCpIndex();
	i32 GetCheckpointCount();
	i32 GetTeleportCount();
	f32 GetTime();
	f32 GetEndTime();
	bool GetPaused();

	void InitWatcher();
	void CleanupWatcher();
} // namespace Bhop::replaysystem

#endif // Bhop_REPLAYSYSTEM_H
