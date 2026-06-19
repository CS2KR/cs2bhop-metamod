#include <string_view>

#include "common.h"
#include "bhop_global.h"
#include "bhop/language/bhop_language.h"
#include "bhop/mode/bhop_mode.h"
#include "bhop/style/bhop_style.h"
#include "bhop/option/bhop_option.h"
#include "utils/http.h"
#include "utils/simplecmds.h"

static_function std::string_view MakeStatusString(bool checkmark)
{
	using namespace std::literals::string_view_literals;
	return checkmark ? "{green}✓{default}"sv : "{darkred}✗{default}"sv;
}

/* TODO: global
SCMD(bhop_globalcheck, SCFL_GLOBAL | SCFL_MAP | SCFL_PLAYER)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);

	if (BhopGlobalService::IsAvailable())
	{
		bool apiStatus = true;
		bool serverStatus = true;
		bool mapStatus = BhopGlobalService::WithCurrentMap([](const Bhop::API::Map *currentMap) { return currentMap != nullptr; });
		bool playerStatus = !player->globalService->playerInfo.isBanned;

		// clang-format off
		bool modeStatus = BhopGlobalService::WithGlobalModes([&](const auto& globalModes)
		{
			Bhop::API::Mode mode;
			if (Bhop::API::DecodeModeString(player->modeService->GetModeShortName(), mode))
			{
				BhopModeManager::ModePluginInfo modeInfo = Bhop::mode::GetModeInfo(player->modeService->GetModeShortName());

				for (const auto &globalMode : globalModes)
				{
#ifdef _WIN32
					const std::string& checksum = globalMode.windowsChecksum;
#else
					const std::string& checksum = globalMode.linuxChecksum;
#endif

					if (mode == globalMode.mode && BHOP_STREQ(modeInfo.md5, checksum.c_str()))
					{
						return true;
					}
				}
			}

			return false;
		});
		// clang-format on

		// clang-format off
		bool styleStatus = BhopGlobalService::WithGlobalStyles([&](const auto& globalStyles)
		{
			FOR_EACH_VEC(player->styleServices, i)
			{
				if (!styleStatus)
				{
					return false;
				}

				Bhop::API::Style style;

				if (!Bhop::API::DecodeStyleString(player->styleServices[i]->GetStyleShortName(), style))
				{
					return false;
				}

				auto styleInfo = Bhop::style::GetStyleInfo(player->styleServices[i]);
				bool found = false;

				for (const auto &globalStyle : globalStyles)
				{
#ifdef _WIN32
					const std::string& checksum = globalStyle.windowsChecksum;
#else
					const std::string& checksum = globalStyle.linuxChecksum;
#endif

					if (style == globalStyle.style && BHOP_STREQ(styleInfo.md5, checksum.c_str()))
					{
						found = true;
						break;
					}
				}

				if (!found)
				{
					return false;
				}
			}

			return true;
		});
		// clang-format on

		// clang-format off
		player->languageService->PrintChat(true, false, "Global Check",
				MakeStatusString(apiStatus),
				MakeStatusString(serverStatus),
				MakeStatusString(mapStatus),
				MakeStatusString(playerStatus),
				MakeStatusString(modeStatus),
				MakeStatusString(styleStatus));
		// clang-format on
	}
	else
	{
		HTTP::Request request(HTTP::Method::GET, BhopOptionService::GetOptionStr("apiUrl", "https://api.placeholder.org"));
		auto callback = [player](HTTP::Response response)
		{
			std::string_view apiStatus = MakeStatusString(response.status == 200);
			std::string_view serverStatus = MakeStatusString(false);
			std::string_view mapStatus = MakeStatusString(false);
			std::string_view playerStatus = MakeStatusString(false);
			std::string_view modeStatus = MakeStatusString(false);
			std::string_view styleStatus = MakeStatusString(false);

			if (BhopGlobalService::MayBecomeAvailable())
			{
				mapStatus = "{yellow}?";
				playerStatus = "{yellow}?";
				modeStatus = "{yellow}?";
				styleStatus = "{yellow}?";
			}

			// clang-format off
			player->languageService->PrintChat(true, false, "Global Check",
					apiStatus,
					serverStatus,
					mapStatus,
					playerStatus,
					modeStatus,
					styleStatus);
			// clang-format on
		};
		request.Send(callback);
	}

	return MRES_SUPERCEDE;
}

SCMD_LINK(bhop_gc, bhop_globalcheck);
*/
