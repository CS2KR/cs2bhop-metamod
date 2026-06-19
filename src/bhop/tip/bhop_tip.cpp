#include "bhop_tip.h"
#include "bhop/timer/bhop_timer.h"
#include "bhop/language/bhop_language.h"

#include <vendor/MultiAddonManager/public/imultiaddonmanager.h>
#include <vendor/ClientCvarValue/public/iclientcvarvalue.h>

extern IClientCvarValue *g_pClientCvarValue;
static_global KeyValues *pTipKeyValues;
static_global CUtlVector<const char *> tipNames;
static_global f64 tipInterval;
static_global i32 nextTipIndex;
static_global CTimer<> *tipTimer;

static_global class BhopTimerServiceEventListener_Tip : public BhopTimerServiceEventListener
{
	virtual void OnTimerStartPost(BhopPlayer *player, u32 courseGUID) override;
} timerEventListener;

extern IMultiAddonManager *g_pMultiAddonManager;

void BhopTipService::Init()
{
	LoadTips();
	ShuffleTips();
	tipTimer = StartTimer(PrintTips, true);
	BhopTimerService::RegisterEventListener(&timerEventListener);
}

void BhopTipService::Reset()
{
	this->showTips = true;
	this->teamJoinedAtLeastOnce = false;
	this->timerStartedAtLeastOnce = false;
}

void BhopTipService::ToggleTips()
{
	this->showTips = !this->showTips;
	player->languageService->PrintChat(true, false, this->showTips ? "Option - Tips - Enable" : "Option - Tips - Disable");
}

bool BhopTipService::ShouldPrintTip()
{
	return this->showTips;
}

void BhopTipService::PrintTip()
{
	this->player->languageService->PrintChat(true, false, tipNames[nextTipIndex]);
}

void BhopTipService::LoadTips()
{
	pTipKeyValues = new KeyValues("Tips");
	pTipKeyValues->UsesEscapeSequences(true);

	char buffer[1024];
	g_SMAPI->PathFormat(buffer, sizeof(buffer), "addons/cs2bhop/translations/*.*");
	FileFindHandle_t findHandle = {};
	const char *fileName = g_pFullFileSystem->FindFirst(buffer, &findHandle);
	if (fileName)
	{
		do
		{
			char fullPath[1024];
			g_SMAPI->PathFormat(fullPath, sizeof(fullPath), "%s/addons/cs2bhop/translations/%s", g_SMAPI->GetBaseDir(), fileName);
			if (V_strstr(fileName, "cs2bhop-tips-"))
			{
				if (!pTipKeyValues->LoadFromFile(g_pFullFileSystem, fullPath, nullptr))
				{
					META_CONPRINTF("Failed to load %s\n", fileName);
				}
			}
			fileName = g_pFullFileSystem->FindNext(findHandle);
		} while (fileName);
		g_pFullFileSystem->FindClose(findHandle);
	}

	FOR_EACH_SUBKEY(pTipKeyValues, it)
	{
		tipNames.AddToTail(it->GetName());
	}

	tipInterval = BhopOptionService::GetOptionFloat("tipInterval", BHOP_DEFAULT_TIP_INTERVAL);
	delete pTipKeyValues;
}

void BhopTipService::ShuffleTips()
{
	for (int i = tipNames.Count() - 1; i > 0; --i)
	{
		int j = RandomInt(0, i);
		V_swap(tipNames.Element(i), tipNames.Element(j));
	}
}

SCMD(bhop_tips, SCFL_MISC)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	player->tipService->ToggleTips();
	return MRES_SUPERCEDE;
}

f64 BhopTipService::PrintTips()
{
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(i);
		if (player->tipService->ShouldPrintTip())
		{
			player->tipService->PrintTip();
		}
	}
	nextTipIndex = (nextTipIndex + 1) % tipNames.Count();
	return tipInterval;
}

void BhopTipService::OnPlayerJoinTeam(i32 team)
{
	if (this->teamJoinedAtLeastOnce || (team != CS_TEAM_CT && team != CS_TEAM_T))
	{
		return;
	}

	this->teamJoinedAtLeastOnce = true;
	if (g_pMultiAddonManager)
	{
		this->player->languageService->PrintChat(true, false, "Menu Hint");
	}
	this->QueryBeamCvar();
}

void BhopTipService::QueryBeamCvar()
{
	CPlayerUserId userID = this->player->GetClient()->GetUserID();
	if (g_pClientCvarValue)
	{
		// clang-format off
		g_pClientCvarValue->QueryCvarValue(this->player->GetPlayerSlot(), "spec_show_xray",
			[userID](CPlayerSlot nSlot, ECvarValueStatus eStatus, const char *pszCvarName, const char *pszCvarValue)
			{
				BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(userID);
				if (!player)
				{
					return;
				}
		});
		// clang-format on
	}
}

void BhopTipService::OnTimerStartPost()
{
	if (this->timerStartedAtLeastOnce)
	{
		return;
	}

	this->teamJoinedAtLeastOnce = true;
	// TODO: Print no cheating stuff
}

void BhopTimerServiceEventListener_Tip::OnTimerStartPost(BhopPlayer *player, u32 courseGUID)
{
	player->tipService->OnTimerStartPost();
}
