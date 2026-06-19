#include "bhop_telemetry.h"
#include "utils/simplecmds.h"
#include "bhop/language/bhop_language.h"
#include "sdk/usercmd.h"

#define AFK_THRESHOLD 30.0f
f64 BhopTelemetryService::lastActiveCheckTime = 0.0f;

void BhopTelemetryService::OnPhysicsSimulatePost()
{
	// AFK check
	if (!this->player->GetMoveServices())
	{
		return;
	}
	if (this->player->GetMoveServices()->m_nButtons()->m_pButtonStates[1] != 0
		|| this->player->GetMoveServices()->m_nButtons()->m_pButtonStates[2] != 0)
	{
		this->activeStats.lastActionTime = g_pBhopUtils->GetServerGlobals()->realtime;
		return;
	}
}

void BhopTelemetryService::ActiveCheck()
{
	f64 currentTime = g_pBhopUtils->GetServerGlobals()->realtime;
	f64 duration = currentTime - BhopTelemetryService::lastActiveCheckTime;
	for (u32 i = 0; i < MAXPLAYERS + 1; i++)
	{
		BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(i);
		if (!player->IsInGame() || player->IsFakeClient() || player->IsCSTV())
		{
			continue;
		}
		player->telemetryService->activeStats.timeSpentInServer += duration;
		if (player->IsAlive())
		{
			if (currentTime - player->telemetryService->activeStats.lastActionTime > AFK_THRESHOLD)
			{
				player->telemetryService->activeStats.afkDuration += duration;
			}
			else
			{
				player->telemetryService->activeStats.activeTime += duration;
			}
		}
	}
	BhopTelemetryService::lastActiveCheckTime = currentTime;
}
