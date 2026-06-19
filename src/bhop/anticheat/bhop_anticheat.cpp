#include "../bhop.h"
#include "bhop_anticheat.h"
#include "bhop/language/bhop_language.h"
#include "bhop/timer/bhop_timer.h"
#include "utils/ctimer.h"

#include <vendor/ClientCvarValue/public/iclientcvarvalue.h>

extern IClientCvarValue *g_pClientCvarValue;

#define INTEGRITY_CHECK_MIN_INTERVAL 1.0f
#define INTEGRITY_CHECK_MAX_INTERVAL 5.0f
#define KICK_DELAY                   5.0f
#define MINIMUM_FPS_MAX              64.0f
#define MAXIMUM_M_YAW                0.3f

static_function f64 KickPlayerInvalidSettings(CPlayerUserId userID)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(userID);
	if (player)
	{
		player->Kick("Invalid player settings");
	}
	return 0.0f;
}

static_function void ValidateCvar(CPlayerSlot nSlot, ECvarValueStatus eStatus, const char *pszCvarName, const char *pszCvarValue)
{
	if (eStatus != ECvarValueStatus::ValueIntact)
	{
		return;
	}
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(nSlot);
	if (BHOP_STREQI(pszCvarName, "m_yaw"))
	{
		if (atof(pszCvarValue) > MAXIMUM_M_YAW)
		{
			player->languageService->PrintChat(true, false, "Kick Player m_yaw");
			player->languageService->PrintConsole(false, false, "Kick Player m_yaw (Console)");
			player->anticheatService->MarkHasInvalidCvars();
			player->timerService->TimerStop();
			StartTimer<CPlayerUserId>(KickPlayerInvalidSettings, player->GetClient()->GetUserID(), KICK_DELAY, true, true);
		}
	}
}

static_function f64 CheckClientCvars(CPlayerUserId userID)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(userID);
	if (!player || !g_pClientCvarValue || !player->anticheatService->ShouldCheckClientCvars())
	{
		return 0.0f;
	}
	g_pClientCvarValue->QueryCvarValue(player->GetPlayerSlot(), "m_yaw", ValidateCvar);
	return RandomFloat(INTEGRITY_CHECK_MIN_INTERVAL, INTEGRITY_CHECK_MAX_INTERVAL);
}

void BhopAnticheatService::OnPlayerFullyConnect()
{
	this->hasValidCvars = true;
	StartTimer<CPlayerUserId>(CheckClientCvars, this->player->GetClient()->GetUserID(),
							  RandomFloat(INTEGRITY_CHECK_MIN_INTERVAL, INTEGRITY_CHECK_MAX_INTERVAL), true, true);
}
