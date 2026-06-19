#include "bhop_spec.h"
#include "../timer/bhop_timer.h"
#include "bhop/language/bhop_language.h"
#include "utils/simplecmds.h"
#include "utils/ctimer.h"

static_global class BhopTimerServiceEventListener_Spec : public BhopTimerServiceEventListener
{
	virtual void OnTimerStartPost(BhopPlayer *player, u32 courseGUID) override;
} timerEventListener;

void BhopSpecService::Reset()
{
	this->ResetSavedPosition();
}

void BhopSpecService::Init()
{
	BhopTimerService::RegisterEventListener(&timerEventListener);
}

bool BhopSpecService::HasSavedPosition()
{
	return this->savedPosition;
}

void BhopSpecService::SavePosition()
{
	this->player->GetOrigin(&this->savedOrigin);
	this->player->GetAngles(&this->savedAngles);
	this->savedOnLadder = this->player->GetMoveType() == MOVETYPE_LADDER;
	this->savedPosition = true;
}

void BhopSpecService::LoadPosition()
{
	if (!this->HasSavedPosition())
	{
		return;
	}
	this->player->Teleport(&this->savedOrigin, &this->savedAngles, nullptr);
	if (this->savedOnLadder)
	{
		this->player->SetMoveType(MOVETYPE_LADDER);
	}
}

void BhopSpecService::ResetSavedPosition()
{
	this->savedOrigin = vec3_origin;
	this->savedAngles = vec3_angle;
	this->savedOnLadder = false;
	this->savedPosition = false;
}

bool BhopSpecService::IsSpectating(BhopPlayer *target)
{
	return this->GetSpectatedPlayer() == target;
}

bool BhopSpecService::SpectatePlayer(const char *playerName)
{
	CCSPlayerController *controller = this->player->GetController();
	BhopPlayer *targetPlayer = nullptr;
	if (BHOP_STREQI(playerName, "@me"))
	{
		if (!this->player->IsAlive())
		{
			this->player->languageService->PrintChat(true, false, "Spectate Failure (Dead)");
			return false;
		}
		targetPlayer = this->player;
	}
	else
	{
		// Prefer exact matches over partial matches.
		for (i32 i = 0; i <= MAXPLAYERS; i++)
		{
			CBasePlayerController *controller = g_pBhopPlayerManager->players[i]->GetController();
			BhopPlayer *otherPlayer = g_pBhopPlayerManager->ToPlayer(i);

			if (!controller || this->player == otherPlayer)
			{
				continue;
			}

			if (BHOP_STREQI(otherPlayer->GetName(), playerName))
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

				if (V_strstr(V_strlower((char *)otherPlayer->GetName()), V_strlower((char *)playerName)))
				{
					if (otherPlayer->GetController()->GetTeam() == CS_TEAM_SPECTATOR)
					{
						player->languageService->PrintChat(true, false, "Spectate Failure (Dead)");
						return MRES_SUPERCEDE;
					}
					targetPlayer = otherPlayer;
					break;
				}
			}
		}
	}

	if (!targetPlayer)
	{
		player->languageService->PrintChat(true, false, "Spectate Failure (Player Not Found)", playerName);
		return MRES_SUPERCEDE;
	}

	this->SpectatePlayer(targetPlayer);
	return true;
}

static_function f64 TeleportObserver(CPlayerUserId userID, Vector origin, QAngle angles)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(userID);
	if (player && player->GetObserverPawn())
	{
		player->GetObserverPawn()->Teleport(&origin, &angles, nullptr);
	}
	return 0.0f;
};

bool BhopSpecService::SpectatePlayer(BhopPlayer *target)
{
	if (!target || !this->CanSpectate())
	{
		return false;
	}
	// Join spectator team if not already in it.
	CCSPlayerController *controller = this->player->GetController();
	if (controller->GetTeam() != CS_TEAM_SPECTATOR)
	{
		Bhop::misc::JoinTeam(this->player, CS_TEAM_SPECTATOR, true);
	}

	CPlayer_ObserverServices *obsService = player->GetController()->m_hObserverPawn()->m_pObserverServices;
	if (!obsService)
	{
		player->languageService->PrintChat(true, false, "Spectate Failure (Generic)");
		return false;
	}

	obsService->m_iObserverMode(OBS_MODE_IN_EYE);
	obsService->m_iObserverLastMode(OBS_MODE_NONE);
	obsService->m_hObserverTarget(target->GetPlayerPawn());

	if (target == this->player)
	{
		controller->m_DesiredObserverMode(OBS_MODE_ROAMING);
		obsService->m_iObserverMode(OBS_MODE_ROAMING);
		controller->m_hDesiredObserverTarget(target->GetPlayerPawn());
		obsService->m_hObserverTarget(target->GetPlayerPawn());
		Vector origin;
		QAngle angles;
		this->player->GetEyeOrigin(&origin);
		this->player->GetAngles(&angles);
		StartTimer<CPlayerUserId, Vector, QAngle>(TeleportObserver, player->GetClient()->GetUserID(), std::move(origin), std::move(angles), 0.0f,
												  false, false);
	}
	return true;
}

bool BhopSpecService::CanSpectate()
{
	return !this->player->IsAlive() || this->player->timerService->GetPaused() || this->player->timerService->CanPause();
}

void BhopSpecService::GetSpectatorList(CUtlVector<CUtlString> &spectatorList)
{
	BhopPlayer *spectator = this->player->specService->GetNextSpectator(nullptr);
	while (spectator)
	{
		spectatorList.AddToTail(spectator->GetName());
		spectator = this->player->specService->GetNextSpectator(spectator);
	}
}

BhopPlayer *BhopSpecService::GetSpectatedPlayer()
{
	if (!player || player->IsAlive())
	{
		return NULL;
	}
	if (!player->GetController() || !player->GetController()->m_hObserverPawn())
	{
		return NULL;
	}
	CPlayer_ObserverServices *obsService = player->GetController()->m_hObserverPawn()->m_pObserverServices;
	if (!obsService)
	{
		return NULL;
	}
	if (!obsService->m_hObserverTarget().IsValid())
	{
		return NULL;
	}
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	CBasePlayerPawn *target = (CBasePlayerPawn *)obsService->m_hObserverTarget().Get();
	// If the player is spectating their own corpse, consider that as not spectating anyone.
	return target == pawn ? nullptr : g_pBhopPlayerManager->ToPlayer(target);
}

BhopPlayer *BhopSpecService::GetNextSpectator(BhopPlayer *current)
{
	for (int i = current ? current->index + 1 : 0; i < MAXPLAYERS + 1; i++)
	{
		BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(i);
		if (player->specService->IsSpectating(this->player))
		{
			return player;
		}
	}
	return nullptr;
}

void BhopTimerServiceEventListener_Spec::OnTimerStartPost(BhopPlayer *player, u32 courseGUID)
{
	player->specService->Reset();
}

SCMD(bhop_spec, SCFL_SPEC)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	if (!player->specService->CanSpectate())
	{
		player->languageService->PrintChat(true, false, "Spectate Failure (Generic)");
		return MRES_SUPERCEDE;
	}
	// Count alive players and find first alive player
	u32 numAlivePlayers = 0;
	BhopPlayer *firstAlivePlayer = nullptr;
	for (i32 i = 0; i < MAXPLAYERS + 1; i++)
	{
		BhopPlayer *otherPlayer = g_pBhopPlayerManager->ToPlayer(i);
		if (otherPlayer && otherPlayer->IsAlive() && otherPlayer != player)
		{
			numAlivePlayers++;
			if (!firstAlivePlayer)
			{
				firstAlivePlayer = otherPlayer;
			}
		}
	}

	// Handle automatic spectating
	if (numAlivePlayers == 0 && args->ArgC() == 1)
	{
		player->specService->SpectatePlayer("@me");
		return MRES_SUPERCEDE;
	}
	if (numAlivePlayers == 1)
	{
		player->specService->SpectatePlayer(firstAlivePlayer);
		return MRES_SUPERCEDE;
	}

	// If no target is provided, default to the first alive player
	if (args->ArgC() < 2)
	{
		player->specService->SpectatePlayer(firstAlivePlayer);
		return MRES_SUPERCEDE;
	}

	// Handle explicit target
	player->specService->SpectatePlayer(args->Arg(1));
	return MRES_SUPERCEDE;
}

SCMD(bhop_specs, SCFL_SPEC)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	BhopPlayer *targetPlayer = player->IsAlive() ? player : player->specService->GetSpectatedPlayer();

	if (!targetPlayer)
	{
		player->languageService->PrintChat(true, false, "Spectator List (None)");
		return MRES_SUPERCEDE;
	}
	CUtlVector<CUtlString> spectatorList;
	targetPlayer->specService->GetSpectatorList(spectatorList);
	if (spectatorList.Count() == 0)
	{
		if (targetPlayer == player)
		{
			player->languageService->PrintChat(true, false, "Spectator List (None)");
		}
		else
		{
			player->languageService->PrintChat(true, false, "Target Spectator List (None)", targetPlayer->GetName());
		}
	}
	else
	{
		CUtlString spectatorListString;
		for (i32 i = 0; i < spectatorList.Count(); i++)
		{
			spectatorListString += spectatorList[i];
			if (i != spectatorList.Count() - 1)
			{
				spectatorListString += ", ";
			}
		}
		if (targetPlayer == player)
		{
			player->languageService->PrintChat(true, false, "Spectator List", spectatorList.Count(), spectatorListString.Get());
		}
		else
		{
			player->languageService->PrintChat(true, false, "Target Spectator List", targetPlayer->GetName(), spectatorList.Count(),
											   spectatorListString.Get());
		}
	}
	return MRES_SUPERCEDE;
}

SCMD_LINK(bhop_speclist, bhop_specs);
