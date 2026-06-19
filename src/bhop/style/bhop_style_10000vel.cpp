#include "bhop_style_10000vel.h"

#include "utils/addresses.h"
#include "utils/interfaces.h"
#include "utils/gameconfig.h"

Bhop10000VelStylePlugin g_Bhop10000VelStylePlugin;

CGameConfig *g_pGameConfig = NULL;
BhopUtils *g_pBhopUtils = NULL;
BhopStyleManager *g_pStyleManager = NULL;
StyleServiceFactory g_StyleFactory = [](BhopPlayer *player) -> BhopStyleService * { return new Bhop10000VelStyleService(player); };
PLUGIN_EXPOSE(Bhop10000VelStylePlugin, g_Bhop10000VelStylePlugin);

CConVarRef<float> sv_maxvelocity("sv_maxvelocity");
const char *incompatibleStyles[] = {"5000vel", "3500vel"};

bool Bhop10000VelStylePlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
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

bool Bhop10000VelStylePlugin::Unload(char *error, size_t maxlen)
{
	g_pStyleManager->UnregisterStyle(g_PLID);
	return true;
}

bool Bhop10000VelStylePlugin::Pause(char *error, size_t maxlen)
{
	g_pStyleManager->UnregisterStyle(g_PLID);
	return true;
}

bool Bhop10000VelStylePlugin::Unpause(char *error, size_t maxlen)
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

void Bhop10000VelStyleService::Init()
{
	g_pBhopUtils->SendConVarValue(this->player->GetPlayerSlot(), sv_maxvelocity, "10000");
}

const CVValue_t *Bhop10000VelStyleService::GetTweakedConvarValue(const char *name)
{
	static_persist const CVValue_t sv_maxvelocity_desiredValue = 10000.f;
	if (!V_stricmp(name, "sv_maxvelocity"))
	{
		return &sv_maxvelocity_desiredValue;
	}
	return nullptr;
}

void Bhop10000VelStyleService::Cleanup()
{
	// Send default 3500 maxvel, course entry will replace with map maxvel if exists
	g_pBhopUtils->SendConVarValue(this->player->GetPlayerSlot(), sv_maxvelocity, "3500");
}

void Bhop10000VelStyleService::OnProcessMovement()
{
	sv_maxvelocity.Set(10000.f);
}
