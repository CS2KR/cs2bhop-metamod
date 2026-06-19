#include "bhop_noclip.h"

#include "../timer/bhop_timer.h"
#include "../language/bhop_language.h"

#include "utils/utils.h"
#include "utils/simplecmds.h"

#define FL_NOCLIP (1 << 3)

void BhopNoclipService::Reset()
{
	this->lastNoclipTime = {};
	this->inNoclip = {};
}

void BhopNoclipService::HandleNoclip()
{
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	if (this->inNoclip)
	{
		if ((pawn->m_fFlags() & FL_NOCLIP) == 0)
		{
			pawn->m_fFlags(pawn->m_fFlags() | FL_NOCLIP);
		}
		if (pawn->m_MoveType() != MOVETYPE_NOCLIP)
		{
			this->player->SetMoveType(MOVETYPE_NOCLIP);
			this->player->timerService->TimerStop();
		}
		// if (pawn->m_Collision().m_CollisionGroup() != BHOP_COLLISION_GROUP_NOTRIGGER)
		// {
		// 	pawn->m_Collision().m_CollisionGroup() = BHOP_COLLISION_GROUP_NOTRIGGER;
		// 	pawn->CollisionRulesChanged();
		// }
		this->lastNoclipTime = g_pBhopUtils->GetServerGlobals()->curtime;
		this->player->timerService->TimerStop();
	}
	else
	{
		if ((pawn->m_fFlags() & FL_NOCLIP) != 0)
		{
			pawn->m_fFlags(pawn->m_fFlags() & ~FL_NOCLIP);
		}
		if (pawn->m_nActualMoveType() == MOVETYPE_NOCLIP)
		{
			this->player->SetMoveType(MOVETYPE_WALK);
			this->player->timerService->TimerStop();
		}
		if (pawn->m_Collision().m_CollisionGroup() != BHOP_COLLISION_GROUP_STANDARD)
		{
			pawn->m_Collision().m_CollisionGroup() = BHOP_COLLISION_GROUP_STANDARD;
			pawn->CollisionRulesChanged();
		}
		if (pawn->m_nActualMoveType() == MOVETYPE_NOCLIP || pawn->m_MoveType() == MOVETYPE_NOCLIP)
		{
			if (this->player->IsAlive() && this->player->timerService->GetTimerRunning())
			{
				this->player->timerService->TimerStop();
			}
		}
	}
}

// Commands

SCMD(bhop_noclip, SCFL_PLAYER)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	player->noclipService->ToggleNoclip();
	if (player->noclipService->IsNoclipping())
	{
		player->languageService->PrintChat(true, false, "Noclip - Enable");
	}
	else
	{
		player->languageService->PrintChat(true, false, "Noclip - Disable");
	}
	return MRES_SUPERCEDE;
}

SCMD_LINK(bhop_nc, bhop_noclip);
SCMD_LINK(noclip, bhop_noclip);

void BhopNoclipService::HandleMoveCollision()
{
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	if (!pawn)
	{
		return;
	}
	if (pawn->m_lifeState() != LIFE_ALIVE)
	{
		this->DisableNoclip();
		return;
	}
	this->HandleNoclip();
}
