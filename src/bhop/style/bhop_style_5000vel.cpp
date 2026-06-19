#include "bhop_style_5000vel.h"

#include "utils/addresses.h"
#include "utils/interfaces.h"
#include "utils/gameconfig.h"

Bhop5000VelStylePlugin g_Bhop5000VelStylePlugin;

CGameConfig *g_pGameConfig = NULL;
BhopUtils *g_pBhopUtils = NULL;
BhopStyleManager *g_pStyleManager = NULL;
StyleServiceFactory g_StyleFactory = [](BhopPlayer *player) -> BhopStyleService * { return new Bhop5000VelStyleService(player); };
PLUGIN_EXPOSE(Bhop5000VelStylePlugin, g_Bhop5000VelStylePlugin);

CConVarRef<float> sv_maxvelocity("sv_maxvelocity");
const char *incompatibleStyles[] = {"3500vel", "10000vel"};

bool Bhop5000VelStylePlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
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

bool Bhop5000VelStylePlugin::Unload(char *error, size_t maxlen)
{
	g_pStyleManager->UnregisterStyle(g_PLID);
	return true;
}

bool Bhop5000VelStylePlugin::Pause(char *error, size_t maxlen)
{
	g_pStyleManager->UnregisterStyle(g_PLID);
	return true;
}

bool Bhop5000VelStylePlugin::Unpause(char *error, size_t maxlen)
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

void Bhop5000VelStyleService::Init()
{
	g_pBhopUtils->SendConVarValue(this->player->GetPlayerSlot(), sv_maxvelocity, "5000");
}

const CVValue_t *Bhop5000VelStyleService::GetTweakedConvarValue(const char *name)
{
	static_persist const CVValue_t sv_maxvelocity_desiredValue = 5000.f;
	if (!V_stricmp(name, "sv_maxvelocity"))
	{
		return &sv_maxvelocity_desiredValue;
	}
	return nullptr;
}

void Bhop5000VelStyleService::Cleanup()
{
	// Send default 3500 maxvel, course entry will replace with map maxvel if exists
	g_pBhopUtils->SendConVarValue(this->player->GetPlayerSlot(), sv_maxvelocity, "3500");
}

void Bhop5000VelStyleService::OnProcessMovement()
{
	sv_maxvelocity.Set(5000.f);
}
