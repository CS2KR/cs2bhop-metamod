#include "common.h"
#include "bhop_goto.h"
#include "utils/simplecmds.h"
#include "utils/utils.h"

#include "../language/bhop_language.h"
#include "../timer/bhop_timer.h"

void BhopGotoService::Init() {}

void BhopGotoService::Reset() {}

bool BhopGotoService::GotoPlayer(const char *playerNamePart)
{
	if (!playerNamePart || !V_stricmp("", playerNamePart))
	{
		player->languageService->PrintChat(true, false, "Goto - Command Usage");
		return false;
	}

	if (this->player->timerService->GetTimerRunning())
	{
		this->player->languageService->PrintChat(true, false, "Goto - Error Message (Timer Running)");
		return false;
	}

	BhopPlayer *targetPlayer = nullptr;

	// Prefer exact matches over partial matches.
	for (i32 i = 0; i <= MAXPLAYERS; i++)
	{
		CBasePlayerController *controller = g_pBhopPlayerManager->players[i]->GetController();
		BhopPlayer *otherPlayer = g_pBhopPlayerManager->ToPlayer(i);

		if (!controller || this->player == otherPlayer)
		{
			continue;
		}

		if (BHOP_STREQI(otherPlayer->GetName(), playerNamePart))
		{
			if (otherPlayer->GetController()->GetTeam() == CS_TEAM_SPECTATOR)
			{
				continue;
			}
			targetPlayer = otherPlayer;
			break;
		}
	}

	// If no exact match was found, try partial matches.
	if (!targetPlayer)
	{
		for (i32 i = 0; i <= MAXPLAYERS; i++)
		{
			CBasePlayerController *controller = g_pBhopPlayerManager->players[i]->GetController();
			BhopPlayer *otherPlayer = g_pBhopPlayerManager->ToPlayer(i);

			if (!controller || this->player == otherPlayer)
			{
				continue;
			}

			if (V_strstr(V_strlower((char *)otherPlayer->GetName()), V_strlower((char *)playerNamePart)))
			{
				if (otherPlayer->GetController()->GetTeam() == CS_TEAM_SPECTATOR)
				{
					continue;
				}
				targetPlayer = otherPlayer;
				break;
			}
		}
	}

	if (!targetPlayer)
	{
		player->languageService->PrintChat(true, false, "Error Message (Player Not Found)", playerNamePart);
		return false;
	}

	if (this->player->GetController()->GetTeam() == CS_TEAM_SPECTATOR)
	{
		this->player->GetController()->SwitchTeam(CS_TEAM_CT);
		this->player->GetController()->Respawn();
	}

	CCSPlayer_MovementServices *ms = this->player->GetMoveServices();

	if (targetPlayer->GetMoveType() == MOVETYPE_LADDER)
	{
		ms->m_vecLadderNormal(targetPlayer->GetMoveServices()->m_vecLadderNormal());
		this->player->SetMoveType(MOVETYPE_LADDER);
	}
	else
	{
		ms->m_vecLadderNormal(vec3_origin);
	}

	Vector origin;
	QAngle angles;
	targetPlayer->GetOrigin(&origin);
	targetPlayer->GetAngles(&angles);

	this->player->Teleport(&origin, &angles, &vec3_origin);
	this->player->languageService->PrintChat(true, false, "Goto - Teleported", targetPlayer->GetName());
	if (this->player->GetPlayerPawn()->m_Collision().m_CollisionGroup() != BHOP_COLLISION_GROUP_STANDARD)
	{
		this->player->GetPlayerPawn()->m_Collision().m_CollisionGroup() = BHOP_COLLISION_GROUP_STANDARD;
		this->player->GetPlayerPawn()->CollisionRulesChanged();
	}
	return true;
}

SCMD(bhop_goto, SCFL_PLAYER)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	const char *targetNamePart = args->ArgS();
	player->gotoService->GotoPlayer(targetNamePart);
	return MRES_SUPERCEDE;
}
