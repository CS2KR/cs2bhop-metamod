#include "bhop.h"

#include "tier0/memdbgon.h"
BhopPlayerManager g_BhopPlayerManager;

BhopPlayerManager *g_pBhopPlayerManager = &g_BhopPlayerManager;
PlayerManager *g_pPlayerManager = dynamic_cast<PlayerManager *>(&g_BhopPlayerManager);

BhopPlayer *BhopPlayerManager::ToPlayer(CPlayerPawnComponent *component)
{
	return static_cast<BhopPlayer *>(MovementPlayerManager::ToPlayer(component));
}

BhopPlayer *BhopPlayerManager::ToPlayer(CBasePlayerController *controller)
{
	return static_cast<BhopPlayer *>(MovementPlayerManager::ToPlayer(controller));
}

BhopPlayer *BhopPlayerManager::ToPlayer(CBasePlayerPawn *pawn)
{
	return static_cast<BhopPlayer *>(MovementPlayerManager::ToPlayer(pawn));
}

BhopPlayer *BhopPlayerManager::ToPlayer(CPlayerSlot slot)
{
	return static_cast<BhopPlayer *>(MovementPlayerManager::ToPlayer(slot));
}

BhopPlayer *BhopPlayerManager::ToPlayer(CEntityIndex entIndex)
{
	return static_cast<BhopPlayer *>(MovementPlayerManager::ToPlayer(entIndex));
}

BhopPlayer *BhopPlayerManager::ToPlayer(CPlayerUserId userID)
{
	return static_cast<BhopPlayer *>(MovementPlayerManager::ToPlayer(userID));
}

BhopPlayer *BhopPlayerManager::ToPlayer(u32 index)
{
	return static_cast<BhopPlayer *>(MovementPlayerManager::players[index]);
}

BhopPlayer *BhopPlayerManager::SteamIdToPlayer(u64 steamID, bool validated)
{
	return static_cast<BhopPlayer *>(PlayerManager::SteamIdToPlayer(steamID, validated));
}
