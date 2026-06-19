#include "bhop_style_halfsideways.h"
#include "sdk/usercmd.h"

#include "utils/addresses.h"
#include "utils/interfaces.h"
#include "utils/gameconfig.h"

BhopHalfSidewaysStylePlugin g_BhopHalfSidewaysStylePlugin;

CGameConfig *g_pGameConfig = NULL;
BhopUtils *g_pBhopUtils = NULL;
BhopStyleManager *g_pStyleManager = NULL;
StyleServiceFactory g_StyleFactory = [](BhopPlayer *player) -> BhopStyleService * { return new BhopHalfSidewaysStyleService(player); };
PLUGIN_EXPOSE(BhopHalfSidewaysStylePlugin, g_BhopHalfSidewaysStylePlugin);

const char *incompatibleStyles[] = {"SW", "OnlyW"};

bool BhopHalfSidewaysStylePlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();
	// Load mode
	int success;
	g_pStyleManager = (BhopStyleManager *)g_SMAPI->MetaFactory(BHOP_STYLE_MANAGER_INTERFACE, &success, 0);
	if (success == META_IFACE_FAILED)
	{
		V_snprintf(error, maxlen, "Failed to find %s interface", BHOP_STYLE_MANAGER_INTERFACE);
		return false;
	}
	g_pBhopUtils = (BhopUtils *)g_SMAPI->MetaFactory(BHOP_UTILS_INTERFACE, &success, 0);
	if (success == META_IFACE_FAILED)
	{
		V_snprintf(error, maxlen, "Failed to find %s interface", BHOP_UTILS_INTERFACE);
		return false;
	}
	modules::Initialize();
	if (!interfaces::Initialize(ismm, error, maxlen))
	{
		V_snprintf(error, maxlen, "Failed to initialize interfaces");
		return false;
	}

	if (nullptr == (g_pGameConfig = g_pBhopUtils->GetGameConfig()))
	{
		V_snprintf(error, maxlen, "Failed to get game config");
		return false;
	}

	if (!g_pStyleManager->RegisterStyle(g_PLID, STYLE_NAME_SHORT, STYLE_NAME, g_StyleFactory, incompatibleStyles,
										sizeof(incompatibleStyles) / sizeof(incompatibleStyles[0])))
	{
		V_snprintf(error, maxlen, "Failed to register style");
		return false;
	}
	ConVar_Register();

	return true;
}

bool BhopHalfSidewaysStylePlugin::Unload(char *error, size_t maxlen)
{
	g_pStyleManager->UnregisterStyle(g_PLID);
	return true;
}

bool BhopHalfSidewaysStylePlugin::Pause(char *error, size_t maxlen)
{
	g_pStyleManager->UnregisterStyle(g_PLID);
	return true;
}

bool BhopHalfSidewaysStylePlugin::Unpause(char *error, size_t maxlen)
{
	if (!g_pStyleManager->RegisterStyle(g_PLID, STYLE_NAME_SHORT, STYLE_NAME, g_StyleFactory))
	{
		return false;
	}
	return true;
}

CGameEntitySystem *GameEntitySystem()
{
	return g_pBhopUtils->GetGameEntitySystem();
}

void BhopHalfSidewaysStyleService::Init() {}

const CVValue_t *BhopHalfSidewaysStyleService::GetTweakedConvarValue(const char *name)
{
	return nullptr;
}

void BhopHalfSidewaysStyleService::Cleanup() {}

void BhopHalfSidewaysStyleService::OnSetupMove(PlayerCommand *pc)
{
	auto base = pc->mutable_base();
	auto subtickMoves = base->mutable_subtick_moves();

	// first scan all subtick moves in playercommand to look for non HSW inputs
	bool hasForwardBack = false;
	bool hasSide = false;

	for (const auto &move : *subtickMoves)
	{
		uint64 button = move.button();
		if (button & (IN_FORWARD | IN_BACK))
		{
			hasForwardBack = true;
		}
		if (button & (IN_MOVELEFT | IN_MOVERIGHT))
		{
			hasSide = true;
		}
	}

	// also check current move values
	if (base->forwardmove() != 0.0f)
	{
		hasForwardBack = true;
	}
	if (base->leftmove() != 0.0f)
	{
		hasSide = true;
	}

	// then remove non HSW inputs if found
	if (hasSide && !hasForwardBack)
	{
		auto iterator = subtickMoves->begin();
		while (iterator != subtickMoves->end())
		{
			if (iterator->button() & (IN_MOVELEFT | IN_MOVERIGHT))
			{
				iterator = subtickMoves->erase(iterator);
			}
			else
			{
				iterator++;
			}
		}
		base->set_leftmove(0.0f);
		this->player->DisableButton(IN_MOVELEFT);
		this->player->DisableButton(IN_MOVERIGHT);
	}
	else if (hasForwardBack && !hasSide)
	{
		auto iterator = subtickMoves->begin();
		while (iterator != subtickMoves->end())
		{
			if (iterator->button() & (IN_FORWARD | IN_BACK))
			{
				iterator = subtickMoves->erase(iterator);
			}
			else
			{
				iterator++;
			}
		}
		base->set_forwardmove(0.0f);
		this->player->DisableButton(IN_FORWARD);
		this->player->DisableButton(IN_BACK);
	}
}
