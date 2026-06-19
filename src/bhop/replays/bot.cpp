#include "cs2bhop.h"
#include "bhop/bhop.h"
#include "bot.h"
#include "bhop_replay.h"
#include "data.h"
#include "item.h"
#include "bhop/spec/bhop_spec.h"
#include "utils/ctimer.h"

static_global CHandle<CCSPlayerController> g_replayBot;
extern CConVar<bool> bhop_replay_playback_skins_enable;

static_function f64 SetBotModel()
{
	auto bot = g_replayBot.Get();
	if (!bot)
	{
		return -1.0f;
	}
	ReplayHeader &header = Bhop::replaysystem::data::GetCurrentReplay()->header;
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(bot);
	EconInfo glovesInfo = {};
	if (header.has_gloves())
	{
		if (header.gloves().has_main_info())
		{
			const auto &mainInfo = header.gloves().main_info();
			glovesInfo.mainInfo.itemDef = mainInfo.item_def();
			glovesInfo.mainInfo.quality = mainInfo.quality();
			glovesInfo.mainInfo.level = mainInfo.level();
			glovesInfo.mainInfo.accountID = mainInfo.account_id();
			glovesInfo.mainInfo.itemID = mainInfo.item_id();
			glovesInfo.mainInfo.inventoryPosition = mainInfo.inventory_position();
			V_strncpy(glovesInfo.mainInfo.customName, mainInfo.custom_name().c_str(), sizeof(glovesInfo.mainInfo.customName));
			V_strncpy(glovesInfo.mainInfo.customNameOverride, mainInfo.custom_name_override().c_str(),
					  sizeof(glovesInfo.mainInfo.customNameOverride));
		}

		glovesInfo.mainInfo.numAttributes = MIN(header.gloves().attributes_size(), 32);
		for (int i = 0; i < glovesInfo.mainInfo.numAttributes; i++)
		{
			const auto &attr = header.gloves().attributes(i);
			glovesInfo.attributes[i].defIndex = attr.def_index();
			glovesInfo.attributes[i].value = attr.value();
		}
	}
	Bhop::replaysystem::item::ApplyModelAttributesToPawn(player->GetPlayerPawn(), glovesInfo, header.model_name().c_str());
	return -1.0f;
}

namespace Bhop::replaysystem::bot
{
	void KickBot()
	{
		if (g_BhopPlugin.unloading)
		{
			return;
		}
		auto bot = g_replayBot.Get();
		if (!bot)
		{
			return;
		}
		interfaces::pEngine->KickClient(bot->GetPlayerSlot(), "bye bot", NETWORK_DISCONNECT_KICKED);
		g_replayBot.Term();
	}

	void SpawnBot()
	{
		if (g_replayBot.Get())
		{
			return;
		}
		BotProfile *profile = BotProfile::PersistentProfile(CS_TEAM_T);
		if (!profile)
		{
			return;
		}
		CCSPlayerController *bot = g_pBhopUtils->CreateBot(profile, CS_TEAM_T, true);
		if (!bot)
		{
			return;
		}

		g_replayBot = bot;
	}

	void MakeBotAlive()
	{
		SpawnBot();
		auto bot = g_replayBot.Get();
		if (!bot)
		{
			return;
		}
		BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(bot);
		Bhop::misc::JoinTeam(player, CS_TEAM_CT, false);
		// CS2 will kick bots that don't have spectator pending team when another player joins.
		player->GetController()->m_iPendingTeamNum(1);
		ReplayHeader &header = Bhop::replaysystem::data::GetCurrentReplay()->header;

		bot->GetPlayerPawn()->m_flViewmodelOffsetX() = header.viewmodel_offset_x();
		bot->GetPlayerPawn()->m_flViewmodelOffsetY() = header.viewmodel_offset_y();
		bot->GetPlayerPawn()->m_flViewmodelOffsetZ() = header.viewmodel_offset_z();
		bot->GetPlayerPawn()->m_flViewmodelFOV() = header.viewmodel_fov();
	}

	void MoveBotToSpec()
	{
		auto bot = g_replayBot.Get();
		if (!bot)
		{
			return;
		}
		BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(bot);
		Bhop::misc::JoinTeam(player, CS_TEAM_SPECTATOR, false);
	}

	CCSPlayerController *GetBot()
	{
		return g_replayBot.Get();
	}

	bool IsValidBot(CCSPlayerController *controller)
	{
		return controller && g_replayBot == controller;
	}

	BhopPlayer *GetBotPlayer()
	{
		auto bot = g_replayBot.Get();
		if (!bot)
		{
			return nullptr;
		}
		return g_pBhopPlayerManager->ToPlayer(bot);
	}

	void InitializeBotForReplay(const ReplayHeader &header)
	{
		MakeBotAlive();
		auto bot = g_replayBot.Get();
		if (!bot)
		{
			return;
		}
		BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(bot);
		if (header.has_player())
		{
			player->SetName(header.player().name().c_str());
		}
		if (bhop_replay_playback_skins_enable.Get())
		{
			StartTimer(SetBotModel, 0.05, false);
		}
	}

	void SpectateBot(BhopPlayer *spectator)
	{
		BhopPlayer *botPlayer = GetBotPlayer();
		if (botPlayer && spectator)
		{
			spectator->specService->SpectatePlayer(botPlayer);
		}
	}

} // namespace Bhop::replaysystem::bot
