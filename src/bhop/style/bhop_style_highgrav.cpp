#include "bhop_style_highgrav.h"

#include "utils/addresses.h"
#include "utils/interfaces.h"
#include "utils/gameconfig.h"

BhopHighGravStylePlugin g_BhopHighGravStylePlugin;

CGameConfig *g_pGameConfig = NULL;
BhopUtils *g_pBhopUtils = NULL;
BhopStyleManager *g_pStyleManager = NULL;
StyleServiceFactory g_StyleFactory = [](BhopPlayer *player) -> BhopStyleService * { return new BhopHighGravStyleService(player); };
PLUGIN_EXPOSE(BhopHighGravStylePlugin, g_BhopHighGravStylePlugin);

const char *incompatibleStyles[] = {"LG"};

bool BhopHighGravStylePlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
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

bool BhopHighGravStylePlugin::Unload(char *error, size_t maxlen)
{
	g_pStyleManager->UnregisterStyle(g_PLID);
	return true;
}

bool BhopHighGravStylePlugin::Pause(char *error, size_t maxlen)
{
	g_pStyleManager->UnregisterStyle(g_PLID);
	return true;
}

bool BhopHighGravStylePlugin::Unpause(char *error, size_t maxlen)
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

void BhopHighGravStyleService::Init()
{
	// called too early to set gravity scale here
}

const CVValue_t *BhopHighGravStyleService::GetTweakedConvarValue(const char *name)
{
	return nullptr;
}

void BhopHighGravStyleService::Cleanup()
{
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	if (pawn)
	{
		pawn->SetGravityScale(1.0f);
	}
}

void BhopHighGravStyleService::OnProcessMovement()
{
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	if (pawn && pawn->m_flActualGravityScale != 1.5f)
	{
		pawn->SetGravityScale(1.5f);
	}
}
