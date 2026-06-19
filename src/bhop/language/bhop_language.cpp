#include "bhop_language.h"
#include "utils/utils.h"
#include "utils/simplecmds.h"
#include "KeyValues.h"
#include "interfaces/interfaces.h"
#include "filesystem.h"
#include "utils/ctimer.h"
#include "bhop/option/bhop_option.h"
#include "bhop/checkpoint/bhop_checkpoint.h"
#include "bhop/timer/bhop_timer.h"

#include <vendor/ClientCvarValue/public/iclientcvarvalue.h>
#include <vendor/MultiAddonManager/public/imultiaddonmanager.h>

extern IMultiAddonManager *g_pMultiAddonManager;

static_global class BhopOptionServiceEventListener_Language : public BhopOptionServiceEventListener
{
	virtual void OnPlayerPreferencesLoaded(BhopPlayer *player)
	{
		player->languageService->OnPlayerPreferencesLoaded();
	}
} optionEventListener;

extern IClientCvarValue *g_pClientCvarValue;

static_global KeyValues *translationKV;
static_global KeyValues *languagesKV;
static_global KeyValues *addonsKV;

void BhopLanguageService::Init()
{
	BhopLanguageService::LoadConfigFiles();
	BhopOptionService::RegisterEventListener(&optionEventListener);
}

void BhopLanguageService::LoadConfigFiles()
{
	if (translationKV)
	{
		delete translationKV;
	}
	if (languagesKV)
	{
		delete languagesKV;
	}
	if (addonsKV)
	{
		delete addonsKV;
	}
	translationKV = new KeyValues("Phrases");
	translationKV->UsesEscapeSequences(true);
	languagesKV = new KeyValues("Languages");
	languagesKV->UsesEscapeSequences(true);
	addonsKV = new KeyValues("Addons");
	addonsKV->UsesEscapeSequences(true);
	BhopLanguageService::LoadTranslations();
	BhopLanguageService::LoadLanguages();
}

void BhopLanguageService::Cleanup()
{
	if (translationKV)
	{
		delete translationKV;
	}
	if (languagesKV)
	{
		delete languagesKV;
		if (addonsKV)
		{
			delete addonsKV;
		}
	}
}

void BhopLanguageService::LoadLanguages()
{
	char fullPath[1024];
	g_SMAPI->PathFormat(fullPath, sizeof(fullPath), "%s/addons/cs2bhop/translations/config.txt", g_SMAPI->GetBaseDir());
	if (!languagesKV->LoadFromFile(g_pFullFileSystem, fullPath, nullptr))
	{
		META_CONPRINT("Failed to load translation config file.\n");
	}
	g_SMAPI->PathFormat(fullPath, sizeof(fullPath), "%s/addons/cs2bhop/translations/menu-addons.txt", g_SMAPI->GetBaseDir());
	if (!addonsKV->LoadFromFile(g_pFullFileSystem, fullPath, nullptr))
	{
		META_CONPRINT("Failed to load addon config file.\n");
	}
}

void BhopLanguageService::LoadTranslations()
{
	char buffer[1024];
	g_SMAPI->PathFormat(buffer, sizeof(buffer), "addons/cs2bhop/translations/*.phrases.txt");
	FileFindHandle_t findHandle = {};
	const char *fileName = g_pFullFileSystem->FindFirst(buffer, &findHandle);
	if (fileName)
	{
		do
		{
			char fullPath[1024];
			g_SMAPI->PathFormat(fullPath, sizeof(fullPath), "%s/addons/cs2bhop/translations/%s", g_SMAPI->GetBaseDir(), fileName);
			if (!translationKV->LoadFromFile(g_pFullFileSystem, fullPath, nullptr))
			{
				META_CONPRINTF("Failed to load %s\n", fileName);
			}
			fileName = g_pFullFileSystem->FindNext(findHandle);
		} while (fileName);
		g_pFullFileSystem->FindClose(findHandle);
	}
}

void BhopLanguageService::OnPlayerPreferencesLoaded()
{
	const char *language = this->player->optionService->GetPreferenceStr("preferredLanguage");
	bool shouldReconnect = !(this->player->checkpointService->GetCheckpointCount() || this->player->timerService->GetTimerRunning());
	if (language[0])
	{
		BhopLanguageService::UpdateLanguage(this->player->GetSteamId64(false), language, LanguageInfo::CacheLevel::CACHE_PREF, shouldReconnect);
		if (!shouldReconnect)
		{
			this->player->languageService->PrintChat(false, false, "Language Change - Manual Menu Change Required");
		}
	}
}

const char *BhopLanguageService::GetLanguage()
{
	return BhopLanguageService::clientLanguageInfos[this->player->GetSteamId64(false)].language;
}

const char *BhopLanguageService::GetTranslatedFormat(const char *language, const char *phrase)
{
	if (!translationKV->FindKey(phrase))
	{
		// META_CONPRINTF("Warning: Phrase '%s' not found, returning orignal message!\n", phrase);
		return phrase;
	}
	const char *outFormat = translationKV->FindKey(phrase)->GetString(language);
	if (outFormat[0] == '\0')
	{
		if (!V_stricmp(language, "#format"))
		{
			// It is fine to have no format.
			return NULL;
		}
		// META_CONPRINTF("Warning: Phrase '%s' not found for language %s!\n", phrase, language);
		return translationKV->FindKey(phrase)->GetString(BHOP_DEFAULT_LANGUAGE);
	}
	return outFormat;
}

void BhopLanguageService::UpdateLanguage(u64 xuid, const char *langKey, LanguageInfo::CacheLevel cacheLevel, bool shouldReconnect)
{
	// Manual override > Loaded preference > Queried ConVar
	auto &langInfo = BhopLanguageService::clientLanguageInfos[xuid];
	const char *addon = addonsKV->GetString(langKey, addonsKV->GetString(BHOP_DEFAULT_LANGUAGE));
	if (langInfo.cacheLevel > cacheLevel)
	{
		return;
	}
	langInfo.cacheLevel = cacheLevel;
	if (!BHOP_STREQILEN(langInfo.lastAddon, addon, sizeof(langInfo.lastAddon)))
	{
		if (BHOP_STREQI(langInfo.lastAddon, BHOP_WORKSHOP_ADDON_ID))
		{
			META_CONPRINTF("[Bhop::Language] Adding %s for client %lli\n", addon, xuid);
		}
		else
		{
			META_CONPRINTF("[Bhop::Language] Adding %s and removing %s for client %lli\n", addon, langInfo.lastAddon, xuid);
		}
		if (g_pMultiAddonManager)
		{
			g_pMultiAddonManager->RemoveClientAddon(langInfo.lastAddon, xuid);
			g_pMultiAddonManager->AddClientAddon(addon, xuid, true);
		}
		V_strncpy(langInfo.lastAddon, addon, sizeof(langInfo.lastAddon));
	}
	V_strncpy(langInfo.language, langKey, sizeof(langInfo.language));
}

void BhopLanguageService::OnPlayerConnect(u64 steamID64)
{
	if (!steamID64 || BhopLanguageService::clientLanguageInfos[steamID64].cacheLevel > LanguageInfo::CacheLevel::CACHE_NONE)
	{
		return;
	}
	this->UpdateLanguage(steamID64, BhopOptionService::GetOptionStr("defaultLanguage", BHOP_DEFAULT_LANGUAGE), LanguageInfo::CacheLevel::CACHE_NONE,
						 false);
	if (g_pClientCvarValue)
	{
		// clang-format off
		g_pClientCvarValue->QueryCvarValue(this->player->GetPlayerSlot(), "cl_language",
			[steamID64](CPlayerSlot nSlot, ECvarValueStatus eStatus, const char *pszCvarName, const char *pszCvarValue)
			{
				if (eStatus == ECvarValueStatus::ValueIntact)
				{
					const char* langKey = languagesKV->GetString(pszCvarValue, pszCvarValue);
					META_CONPRINTF("[Bhop::Language] Received client convar value: %s\n", langKey);
					BhopLanguageService::UpdateLanguage(steamID64, langKey, LanguageInfo::CacheLevel::CACHE_CVAR, true);
				}
		});
		// clang-format on
	}
}

BhopLanguageService::LanguageInfo::LanguageInfo()
{
	V_strncpy(this->language, BhopOptionService::GetOptionStr("defaultLanguage", BHOP_DEFAULT_LANGUAGE), sizeof(this->language));
}

SCMD(bhop_language, SCFL_PREFERENCE)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	char language[32] {};
	V_snprintf(language, sizeof(language), "%s", args->Arg(1));
	V_strlower(language);
	bool shouldReconnect = !(player->checkpointService->GetCheckpointCount() || player->timerService->GetTimerRunning());
	BhopLanguageService::UpdateLanguage(player->GetSteamId64(false), language, BhopLanguageService::LanguageInfo::CacheLevel::CACHE_OVERRIDE, true);
	player->optionService->SetPreferenceStr("preferredLanguage", language);
	if (!shouldReconnect)
	{
		player->languageService->PrintChat(true, false, "Switch Language", language);
		player->languageService->PrintChat(false, false, "Language Change - Manual Menu Change Required");
	}
	return MRES_SUPERCEDE;
}

CON_COMMAND_F(bhop_reload_translations, "Reload translation configuration files", FCVAR_NONE)
{
	BhopLanguageService::LoadConfigFiles();
}
