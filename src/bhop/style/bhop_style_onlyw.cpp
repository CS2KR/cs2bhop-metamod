#include "bhop_style_onlyw.h"
#include "sdk/usercmd.h"

#include "utils/addresses.h"
#include "utils/interfaces.h"
#include "utils/gameconfig.h"

BhopOnlyWStylePlugin g_BhopOnlyWStylePlugin;

CGameConfig *g_pGameConfig = NULL;
BhopUtils *g_pBhopUtils = NULL;
BhopStyleManager *g_pStyleManager = NULL;
StyleServiceFactory g_StyleFactory = [](BhopPlayer *player) -> BhopStyleService * { return new BhopOnlyWStyleService(player); };
PLUGIN_EXPOSE(BhopOnlyWStylePlugin, g_BhopOnlyWStylePlugin);

const char *incompatibleStyles[] = {"HSW", "SW"};

bool BhopOnlyWStylePlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
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

bool BhopOnlyWStylePlugin::Unload(char *error, size_t maxlen)
{
	g_pStyleManager->UnregisterStyle(g_PLID);
	return true;
}

bool BhopOnlyWStylePlugin::Pause(char *error, size_t maxlen)
{
	g_pStyleManager->UnregisterStyle(g_PLID);
	return true;
}

bool BhopOnlyWStylePlugin::Unpause(char *error, size_t maxlen)
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

void BhopOnlyWStyleService::Init() {}

const CVValue_t *BhopOnlyWStyleService::GetTweakedConvarValue(const char *name)
{
	return nullptr;
}

void BhopOnlyWStyleService::Cleanup() {}

void BhopOnlyWStyleService::OnSetupMove(PlayerCommand *pc)
{
	auto subtickMoves = pc->mutable_base()->mutable_subtick_moves();
	auto iterator = subtickMoves->begin();

	while (iterator != subtickMoves->end())
	{
		uint64 button = iterator->button();
		if (button == IN_MOVELEFT || button == IN_MOVERIGHT || button == IN_BACK)
		{
			iterator = subtickMoves->erase(iterator);
		}
		else
		{
			iterator++;
		}
	}

	pc->mutable_base()->set_leftmove(0.0f);
	if (pc->mutable_base()->forwardmove() < 0.0f)
	{
		pc->mutable_base()->set_forwardmove(0.0f);
	}

	// disable buttons for HUD
	this->player->DisableButton(IN_MOVELEFT);
	this->player->DisableButton(IN_MOVERIGHT);
	this->player->DisableButton(IN_BACK);
}
