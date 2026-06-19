#include "cs2bhop.h"

#include "entity2/entitysystem.h"
#include "steam/steam_gameserver.h"

#include "sdk/cgameresourceserviceserver.h"
#include "utils/utils.h"
#include "utils/hooks.h"
#include "utils/gameconfig.h"
#include "utils/async_file_io.h"

#include "movement/movement.h"
#include "bhop/bhop.h"
#include "bhop/db/bhop_db.h"
#include "bhop/hud/bhop_hud.h"
#include "bhop/mode/bhop_mode.h"
#include "bhop/spec/bhop_spec.h"
#include "bhop/goto/bhop_goto.h"
#include "bhop/style/bhop_style.h"
#include "bhop/quiet/bhop_quiet.h"
#include "bhop/tip/bhop_tip.h"
#include "bhop/option/bhop_option.h"
#include "bhop/language/bhop_language.h"
#include "bhop/mappingapi/bhop_mappingapi.h"
#include "bhop/global/bhop_global.h"
#include "bhop/beam/bhop_beam.h"
#include "bhop/beam/bhop_zone_beam.h"
#include "bhop/recording/bhop_recording.h"
#include "bhop/replays/bhop_replaysystem.h"

#include <vendor/MultiAddonManager/public/imultiaddonmanager.h>
#include <vendor/ClientCvarValue/public/iclientcvarvalue.h>

#include "tier0/memdbgon.h"
BhopPlugin g_BhopPlugin;

IMultiAddonManager *g_pMultiAddonManager;
IClientCvarValue *g_pClientCvarValue;
CSteamGameServerAPIContext g_steamAPI;

PLUGIN_EXPOSE(BhopPlugin, g_BhopPlugin);

bool BhopPlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	setlocale(LC_ALL, "en_US.utf8");
	PLUGIN_SAVEVARS();

	if (!utils::Initialize(ismm, error, maxlen))
	{
		return false;
	}
	ConVar_Register();
	hooks::Initialize();
	movement::InitDetours();
	BhopCheckpointService::Init();
	BhopTimerService::Init();
	BhopSpecService::Init();
	BhopGotoService::Init();
	BhopHUDService::Init();
	BhopLanguageService::Init();
	BhopBeamService::Init();
	BhopZoneBeamService::Init();
	Bhop::misc::Init();
	BhopQuietService::Init();
	AsyncFileIO::Init();
	BhopRecordingService::Init();
	if (!Bhop::mode::CheckModeCvars())
	{
		return false;
	}

	ismm->AddListener(this, this);
	Bhop::mapapi::Init();
	Bhop::mode::InitModeManager();
	Bhop::style::InitStyleManager();

	Bhop::mode::DisableReplicatedModeCvars();

	BhopOptionService::InitOptions();
	BhopTipService::Init();
	if (late)
	{
		g_steamAPI.Init();
		g_pBhopPlayerManager->OnLateLoad();
		// We need to reset the map for mapping api to properly load in.
		utils::ResetMap();
		Bhop::replaysystem::Init();
	}

	// We don't need command filtering for bhop maps.
	CommandLine()->AppendParm("-disable_workshop_command_filtering", "");

	Bhop::replaysystem::InitWatcher();

	return true;
}

bool BhopPlugin::Unload(char *error, size_t maxlen)
{
	this->unloading = true;
	Bhop::misc::UnrestrictTimeLimit();
	BhopRecordingService::Shutdown();
	AsyncFileIO::Cleanup();
	hooks::Cleanup();
	Bhop::mode::EnableReplicatedModeCvars();
	utils::Cleanup();
	g_pBhopModeManager->Cleanup();
	g_pBhopStyleManager->Cleanup();
	g_pPlayerManager->Cleanup();
	BhopDatabaseService::Cleanup();
	BhopGlobalService::Cleanup();
	BhopLanguageService::Cleanup();
	BhopOptionService::Cleanup();
	Bhop::replaysystem::Cleanup();
	ConVar_Unregister();
	return true;
}

void BhopPlugin::AllPluginsLoaded()
{
	BhopDatabaseService::Init();
	Bhop::mode::LoadModePlugins();
	Bhop::style::LoadStylePlugins();
	g_pBhopPlayerManager->ResetPlayers();
	this->UpdateSelfMD5();
	g_pMultiAddonManager = (IMultiAddonManager *)g_SMAPI->MetaFactory(MULTIADDONMANAGER_INTERFACE, nullptr, nullptr);
	g_pClientCvarValue = (IClientCvarValue *)g_SMAPI->MetaFactory(CLIENTCVARVALUE_INTERFACE, nullptr, nullptr);
}

void BhopPlugin::AddonInit()
{
	static_persist bool addonLoaded;
	if (g_pMultiAddonManager != nullptr && !addonLoaded)
	{
		addonLoaded = g_pMultiAddonManager->AddAddon(BHOP_WORKSHOP_ADDON_ID, true);
		CConVarRef<bool> mm_cache_clients_with_addons("mm_cache_clients_with_addons");
		CConVarRef<float> mm_cache_clients_duration("mm_cache_clients_duration");
		mm_cache_clients_with_addons.Set(true);
		mm_cache_clients_duration.Set(30.0f);
	}
}

bool BhopPlugin::IsAddonMounted()
{
	if (g_pMultiAddonManager != nullptr)
	{
		return g_pMultiAddonManager->IsAddonMounted(BHOP_WORKSHOP_ADDON_ID, true);
	}
	return false;
}

bool BhopPlugin::Pause(char *error, size_t maxlen)
{
	return true;
}

bool BhopPlugin::Unpause(char *error, size_t maxlen)
{
	return true;
}

void *BhopPlugin::OnMetamodQuery(const char *iface, int *ret)
{
	if (strcmp(iface, BHOP_MODE_MANAGER_INTERFACE) == 0)
	{
		*ret = META_IFACE_OK;
		return g_pBhopModeManager;
	}
	else if (strcmp(iface, BHOP_STYLE_MANAGER_INTERFACE) == 0)
	{
		*ret = META_IFACE_OK;
		return g_pBhopStyleManager;
	}
	else if (strcmp(iface, BHOP_UTILS_INTERFACE) == 0)
	{
		*ret = META_IFACE_OK;
		return g_pBhopUtils;
	}
	else if (strcmp(iface, BHOP_MAPPING_INTERFACE) == 0)
	{
		*ret = META_IFACE_OK;
		return g_pMappingApi;
	}
	*ret = META_IFACE_FAILED;

	return NULL;
}

void BhopPlugin::UpdateSelfMD5()
{
	ISmmPluginManager *pluginManager = (ISmmPluginManager *)g_SMAPI->MetaFactory(MMIFACE_PLMANAGER, nullptr, nullptr);
	const char *path;
	pluginManager->Query(g_PLID, &path, nullptr, nullptr);
	g_pBhopUtils->GetFileMD5(path, this->md5, sizeof(this->md5));
}

CGameEntitySystem *GameEntitySystem()
{
	return interfaces::pGameResourceServiceServer->GetGameEntitySystem();
}
