#include "bhop_style_tas.h"

#include "utils/addresses.h"
#include "utils/interfaces.h"
#include "utils/gameconfig.h"

BhopTASStylePlugin g_BhopTASStylePlugin;

CGameConfig *g_pGameConfig = NULL;
BhopUtils *g_pBhopUtils = NULL;
BhopStyleManager *g_pStyleManager = NULL;
StyleServiceFactory g_StyleFactory = [](BhopPlayer *player) -> BhopStyleService * { return new BhopTASStyleService(player); };
PLUGIN_EXPOSE(BhopTASStylePlugin, g_BhopTASStylePlugin);

bool BhopTASStylePlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
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

	if (!g_pStyleManager->RegisterStyle(g_PLID, STYLE_NAME_SHORT, STYLE_NAME, g_StyleFactory))
	{
		V_snprintf(error, maxlen, "Failed to register style");
		return false;
	}
	ConVar_Register();

	return true;
}

bool BhopTASStylePlugin::Unload(char *error, size_t maxlen)
{
	g_pStyleManager->UnregisterStyle(g_PLID);
	return true;
}

bool BhopTASStylePlugin::Pause(char *error, size_t maxlen)
{
	g_pStyleManager->UnregisterStyle(g_PLID);
	return true;
}

bool BhopTASStylePlugin::Unpause(char *error, size_t maxlen)
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

void BhopTASStyleService::Init() {}

const CVValue_t *BhopTASStyleService::GetTweakedConvarValue(const char *name)
{
	return nullptr;
}

void BhopTASStyleService::Cleanup() {}
