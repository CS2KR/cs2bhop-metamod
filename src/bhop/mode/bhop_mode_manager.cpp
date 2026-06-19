#include "bhop_mode.h"
#include "bhop_mode_64t.h"

#include "filesystem.h"

#include "utils/utils.h"
#include "interfaces/interfaces.h"

#include "../timer/bhop_timer.h"
#include "../style/bhop_style.h"
#include "../language/bhop_language.h"
#include "../db/bhop_db.h"
#include "../option/bhop_option.h"
#include "../telemetry/bhop_telemetry.h"
#include "../profile/bhop_profile.h"

#include "utils/simplecmds.h"
#include "utils/plat.h"

static_function SCMD_CALLBACK(Command_BhopModeShort);

static_global BhopModeManager modeManager;
BhopModeManager *g_pBhopModeManager = &modeManager;

CUtlVector<BhopModeManager::ModePluginInfo> modeInfos;

CConVarRef<float> sv_maxvelocity("sv_maxvelocity");

static_global class BhopDatabaseServiceEventListener_Modes : public BhopDatabaseServiceEventListener
{
public:
	virtual void OnDatabaseSetup() override;
} databaseEventListener;

static_global class BhopOptionServiceEventListener_Modes : public BhopOptionServiceEventListener
{
	virtual void OnPlayerPreferencesLoaded(BhopPlayer *player) override;
} optionEventListener;

bool Bhop::mode::CheckModeCvars()
{
	for (u32 i = 0; i < MODECVAR_COUNT; i++)
	{
		if (!modeCvarRefs[i]->IsValidRef())
		{
			META_CONPRINTF("Failed to find %s reference!\n", Bhop::mode::modeCvarNames[i]);
			return false;
		}
		if (!modeCvarRefs[i]->IsConVarDataAvailable())
		{
			META_CONPRINTF("Failed to find %s cvarData!\n", Bhop::mode::modeCvarNames[i]);
		}
	}
	return true;
}

void Bhop::mode::InitModeManager()
{
	static_persist bool initialized = false;
	if (initialized)
	{
		return;
	}
	ModeServiceFactory _128tFactory = [](BhopPlayer *player) -> BhopModeService * { return new Bhop64tModeService(player); };
	modeManager.RegisterMode(g_PLID, "128t", "128tick", _128tFactory);
	BhopDatabaseService::RegisterEventListener(&databaseEventListener);
	BhopOptionService::RegisterEventListener(&optionEventListener);
	initialized = true;
}

void Bhop::mode::LoadModePlugins()
{
	char buffer[1024];
	g_SMAPI->PathFormat(buffer, sizeof(buffer), "addons/cs2bhop/modes/*%s", MODULE_EXT);
	FileFindHandle_t findHandle = {};
	const char *output = g_pFullFileSystem->FindFirstEx(buffer, "GAME", &findHandle);
	if (output)
	{
		int ret;
		ISmmPluginManager *pluginManager = (ISmmPluginManager *)g_SMAPI->MetaFactory(MMIFACE_PLMANAGER, &ret, 0);
		if (ret == META_IFACE_FAILED)
		{
			return;
		}
		char error[256];
		char fullPath[1024];
		do
		{
			g_SMAPI->PathFormat(fullPath, sizeof(fullPath), "%s/addons/cs2bhop/modes/%s", g_SMAPI->GetBaseDir(), output);
			bool already = false;
			pluginManager->Load(fullPath, g_PLID, already, error, sizeof(error));
			output = g_pFullFileSystem->FindNext(findHandle);
		} while (output);

		g_pFullFileSystem->FindClose(findHandle);
	}
}

void Bhop::mode::UpdateModeDatabaseID(CUtlString name, i32 id, CUtlString shortName)
{
	// Check if the mode already exists in the list, if yes, we update it.
	FOR_EACH_VEC(modeInfos, i)
	{
		if (!V_stricmp(modeInfos[i].longModeName, name))
		{
			modeInfos[i].databaseID = id;
			if (!shortName.IsEmpty())
			{
				modeInfos[i].shortModeName = shortName;
			}
			return;
		}
	}
	// If the code reaches here, that means the mode is not in the list yet.
	modeInfos.AddToTail({-1, name.Get(), shortName.Get()});
}

void Bhop::mode::InitModeService(BhopPlayer *player)
{
	delete player->modeService;
	player->modeService = new Bhop64tModeService(player);
}

void Bhop::mode::DisableReplicatedModeCvars()
{
	for (u32 i = 0; i < MODECVAR_COUNT; i++)
	{
		assert(modeCvarRefs[i]->IsValidRef() && modeCvarRefs[i]->IsConVarDataAvailable());
		modeCvarRefs[i]->GetConVarData()->RemoveFlags(FCVAR_REPLICATED);
	}
	sv_maxvelocity.GetConVarData()->RemoveFlags(FCVAR_REPLICATED);
}

void Bhop::mode::EnableReplicatedModeCvars()
{
	for (u32 i = 0; i < MODECVAR_COUNT; i++)
	{
		assert(modeCvarRefs[i]->IsValidRef() && modeCvarRefs[i]->IsConVarDataAvailable());
		modeCvarRefs[i]->GetConVarData()->AddFlags(FCVAR_REPLICATED);
	}
	sv_maxvelocity.GetConVarData()->AddFlags(FCVAR_REPLICATED);
}

void Bhop::mode::ApplyModeSettings(BhopPlayer *player)
{
	for (u32 i = 0; i < MODECVAR_COUNT; i++)
	{
		auto &value = player->modeService->GetModeConVarValues()[i];
		auto original = modeCvarRefs[i]->GetConVarData()->Value(-1);
		auto traits = modeCvarRefs[i]->TypeTraits();
		traits->Copy(original, value);
	}
	player->enableWaterFix = player->modeService->EnableWaterFix();

	// Handle max velocity
	if (sv_maxvelocity.IsValidRef() && sv_maxvelocity.IsConVarDataAvailable())
	{
		// First check for maxvel style
		for (i32 i = 0; i < player->styleServices.Count(); i++)
		{
			CUtlString shortName(Bhop::style::GetStyleInfo(player->styleServices[i]).shortName);
			if (shortName.MatchesPattern("*vel"))
			{
				// Maxvel enforce will be handled by the style
				return;
			}
		}
		const BhopCourseDescriptor *course = player->timerService->GetCourse();
		if (course)
		{
			const CVValue_t &maxVel = (float)course->maxVel;
			auto original = sv_maxvelocity.GetConVarData()->Value(-1);
			auto traits = sv_maxvelocity.TypeTraits();
			traits->Copy(original, maxVel);
		}
	}
}

bool BhopModeManager::RegisterMode(PluginId id, const char *shortModeName, const char *longModeName, ModeServiceFactory factory)
{
	// clang-format off
	if (!shortModeName || V_strlen(shortModeName) == 0 || V_strlen(shortModeName) > 64
		 || !longModeName || V_strlen(longModeName) == 0 || V_strlen(longModeName) > 64)
	//clang-format on
	{
		return false;
	}
	// Update the info list if already exists
	ModePluginInfo *info = nullptr;
	FOR_EACH_VEC(modeInfos, i)
	{
		if (!V_stricmp(modeInfos[i].shortModeName, shortModeName) || !V_stricmp(modeInfos[i].longModeName, longModeName))
		{
			if (modeInfos[i].id > 0)
			{
				return false;
			}
			info = &modeInfos[i];
			break;
		}
	}

	char shortModeCmd[64];
	char shortModeDescription[64];
	V_snprintf(shortModeCmd, 64, "bhop_%s", shortModeName);
	V_snprintf(shortModeDescription, 64, "Command Description - bhop_%s", shortModeName);
	bool shortCmdRegistered = scmd::RegisterCmd(V_strlower(shortModeCmd), Command_BhopModeShort, shortModeDescription, SCFL_MODESTYLE);

	// Add to the list otherwise, and update the database for ID.
	if (!info)
	{
		info = modeInfos.AddToTailGetPtr();
		// If there is already information about this mode while the ID is -1, that means it has to come from the database, so no need to update it.
		BhopDatabaseService::InsertAndUpdateModeIDs(longModeName, shortModeName);
	}
	*info = {id, shortModeName, longModeName, factory, shortCmdRegistered};
	if (id)
	{
		ISmmPluginManager *pluginManager = (ISmmPluginManager *)g_SMAPI->MetaFactory(MMIFACE_PLMANAGER, nullptr, nullptr);
		const char *path;
		pluginManager->Query(id, &path, nullptr, nullptr);
		g_pBhopUtils->GetFileMD5(path, info->md5, sizeof(info->md5));
	}
	return true;
}

void BhopModeManager::UnregisterMode(PluginId id)
{
	// Cannot unregister the built-in 128t mode.
	if (id = g_PLID)
	{
		return;
	}

	FOR_EACH_VEC(modeInfos, i)
	{
		if (id == modeInfos[i].id)
		{
			for (u32 i = 0; i < MAXPLAYERS + 1; i++)
			{
				BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(i);
				if (!player->IsInGame())
				{
					continue;
				}
				if (!V_strcmp(player->modeService->GetModeName(), modeInfos[i].longModeName)
					|| !V_strcmp(player->modeService->GetModeShortName(), modeInfos[i].shortModeName))
				{
					this->SwitchToMode(player, BHOP_DEFAULT_MODE);
				}
			}

			char shortModeCmd[64];
			V_snprintf(shortModeCmd, 64, "bhop_%s", modeInfos[i].shortModeName.Get());
			scmd::UnregisterCmd(shortModeCmd);

			modeInfos[i].id = -1;
			modeInfos[i].md5[0] = 0;
			modeInfos[i].factory = nullptr;
			modeInfos[i].shortCmdRegistered = false;

			break;
		}
	}
}

bool BhopModeManager::SwitchToMode(BhopPlayer *player, const char *modeName, bool silent, bool force, bool updatePreference)
{
	// Don't change mode if it doesn't exist. Instead, print a list of modes to the client.
	if (!modeName || !V_stricmp("", modeName))
	{
		player->languageService->PrintChat(true, false, "Mode Command Usage");
		player->languageService->PrintConsole(false, false, "Possible & Current Modes", player->modeService->GetModeName());
		FOR_EACH_VEC(modeInfos, i)
		{
			if (modeInfos[i].id < 0)
			{
				continue;
			}
			// clang-format off
			player->PrintConsole(false, false,
				"%s (bhop_mode %s / bhop_mode %s)",
				modeInfos[i].longModeName.Get(),
				modeInfos[i].longModeName.Get(),
				modeInfos[i].shortModeName.Get()
			);
			// clang-format on
		}
		return false;
	}

	// If it's the same style, do nothing, unless it's forced.
	if (!force && (V_stricmp(player->modeService->GetModeName(), modeName) == 0 || V_stricmp(player->modeService->GetModeShortName(), modeName) == 0))
	{
		return false;
	}

	ModeServiceFactory factory = nullptr;

	FOR_EACH_VEC(modeInfos, i)
	{
		if (V_stricmp(modeInfos[i].shortModeName, modeName) == 0 || V_stricmp(modeInfos[i].longModeName, modeName) == 0)
		{
			factory = modeInfos[i].factory;
			break;
		}
	}
	if (!factory)
	{
		if (!silent)
		{
			player->languageService->PrintChat(true, false, "Mode Not Available", modeName);
		}
		return false;
	}
	player->modeService->Cleanup();
	delete player->modeService;
	player->modeService = factory(player);
	player->timerService->TimerStop();
	player->modeService->Init();

	if (!silent)
	{
		player->languageService->PrintChat(true, false, "Switched Mode", player->modeService->GetModeName());
	}

	utils::SendMultipleConVarValues(player->GetPlayerSlot(), Bhop::mode::modeCvarRefs, player->modeService->GetModeConVarValues(), MODECVAR_COUNT);

	player->SetVelocity({0, 0, 0});

	player->profileService->currentRating = -1.0f;
	player->profileService->RequestRating();
	player->profileService->UpdateClantag();
	if (updatePreference)
	{
		player->optionService->SetPreferenceStr("preferredMode", modeName);
	}
	return true;
}

void BhopModeManager::Cleanup()
{
	int ret;
	ISmmPluginManager *pluginManager = (ISmmPluginManager *)g_SMAPI->MetaFactory(MMIFACE_PLMANAGER, &ret, 0);
	if (ret == META_IFACE_FAILED)
	{
		return;
	}
	char error[256];
	FOR_EACH_VEC(modeInfos, i)
	{
		if (modeInfos[i].id == g_PLID)
		{
			continue;
		}
		pluginManager->Unload(modeInfos[i].id, true, error, sizeof(error));
	}
	// Restore cvars to normal values.
	for (u32 i = 0; i < MODECVAR_COUNT; i++)
	{
		CBufferStringN<32> defaultValue;
		Bhop::mode::modeCvarRefs[i]->GetDefaultAsString(defaultValue);
		Bhop::mode::modeCvarRefs[i]->SetString(defaultValue);
	}
}

SCMD(bhop_mode, SCFL_MODESTYLE)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	modeManager.SwitchToMode(player, args->Arg(1));
	return MRES_SUPERCEDE;
}

static_function SCMD_CALLBACK(Command_BhopModeShort)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	// Strip bhop_ prefix if exist.
	size_t len = 0;
	bool stripped = false;
	if (!stripped)
	{
		len = strlen("bhop_");
		stripped = strncmp(args->Arg(0), "bhop_", len) == 0;
	}
	if (!stripped)
	{
		len = 1;
		stripped = args->Arg(0)[0] == SCMD_CHAT_TRIGGER || args->Arg(0)[0] == SCMD_CHAT_SILENT_TRIGGER;
	}

	if (stripped)
	{
		const char *mode = args->Arg(0) + len;
		modeManager.SwitchToMode(player, mode);
	}
	return MRES_SUPERCEDE;
}

BhopModeManager::ModePluginInfo Bhop::mode::GetModeInfo(BhopModeService *mode)
{
	BhopModeManager::ModePluginInfo emptyInfo;
	if (!mode)
	{
		META_CONPRINTF("[Bhop] Warning: Getting mode info from a nullptr!\n");
		return emptyInfo;
	}
	FOR_EACH_VEC(modeInfos, i)
	{
		if (!V_stricmp(mode->GetModeName(), modeInfos[i].longModeName))
		{
			return modeInfos[i];
		}
	}
	return emptyInfo;
}

BhopModeManager::ModePluginInfo Bhop::mode::GetModeInfo(Bhop::API::Mode mode)
{
	switch (mode)
	{
		case Bhop::API::Mode::_128tick:
		{
			return Bhop::mode::GetModeInfo("128tick");
		}
		case Bhop::API::Mode::CSS66:
		{
			return Bhop::mode::GetModeInfo("CSS66tick");
		}
	}
	return BhopModeManager::ModePluginInfo();
}

BhopModeManager::ModePluginInfo Bhop::mode::GetModeInfo(CUtlString modeName)
{
	BhopModeManager::ModePluginInfo emptyInfo;
	if (modeName.IsEmpty())
	{
		META_CONPRINTF("[Bhop] Warning: Getting mode info from an empty string!\n");
		return emptyInfo;
	}
	FOR_EACH_VEC(modeInfos, i)
	{
		if (modeName.IsEqual_FastCaseInsensitive(modeInfos[i].shortModeName) || modeName.IsEqual_FastCaseInsensitive(modeInfos[i].longModeName))
		{
			return modeInfos[i];
		}
	}
	return emptyInfo;
}

BhopModeManager::ModePluginInfo Bhop::mode::GetModeInfoFromDatabaseID(i32 id)
{
	FOR_EACH_VEC(modeInfos, i)
	{
		if (modeInfos[i].databaseID == id)
		{
			return modeInfos[i];
		}
	}
	return BhopModeManager::ModePluginInfo();
}

void BhopDatabaseServiceEventListener_Modes::OnDatabaseSetup()
{
	FOR_EACH_VEC(modeInfos, i)
	{
		if (modeInfos[i].databaseID == -1)
		{
			BhopDatabaseService::InsertAndUpdateModeIDs(modeInfos[i].longModeName, modeInfos[i].shortModeName);
		}
	}
	BhopDatabaseService::UpdateModeIDs();
}

void BhopOptionServiceEventListener_Modes::OnPlayerPreferencesLoaded(BhopPlayer *player)
{
	const char *mode = player->optionService->GetPreferenceStr("preferredMode", BhopOptionService::GetOptionStr("defaultMode", BHOP_DEFAULT_MODE));
	// Give up changing modes if the player is already in the server for a while.
	if (player->telemetryService->GetTimeInServer() < 30.0f && !player->timerService->GetTimerRunning())
	{
		modeManager.SwitchToMode(player, mode, false, false);
	}
}
