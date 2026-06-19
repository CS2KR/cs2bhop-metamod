#include "../bhop.h"
#include "cs2bhop.h"
#include "bhop_hud.h"
#include "sdk/datatypes.h"
#include "utils/utils.h"
#include "utils/simplecmds.h"

#include "bhop/option/bhop_option.h"
#include "bhop/timer/bhop_timer.h"
#include "bhop/language/bhop_language.h"
#include "bhop/replays/bhop_replaysystem.h"

#include "tier0/memdbgon.h"

#define HUD_ON_GROUND_THRESHOLD 0.07f

static_global class BhopTimerServiceEventListener_HUD : public BhopTimerServiceEventListener
{
	virtual void OnTimerStopped(BhopPlayer *player, u32 courseGUID) override;
	virtual void OnTimerEndPost(BhopPlayer *player, u32 courseGUID, f32 time) override;
} timerEventListener;

static_global class BhopOptionServiceEventListener_HUD : public BhopOptionServiceEventListener
{
	virtual void OnPlayerPreferencesLoaded(BhopPlayer *player)
	{
		player->hudService->ResetShowPanel();
	}
} optionEventListener;

void BhopHUDService::Init()
{
	BhopTimerService::RegisterEventListener(&timerEventListener);
	BhopOptionService::RegisterEventListener(&optionEventListener);
}

void BhopHUDService::Reset()
{
	this->showPanel = this->player->optionService->GetPreferenceBool("showPanel", true);
	this->timerStoppedTime = {};
	this->currentTimeWhenTimerStopped = {};
}

std::string BhopHUDService::GetSpeedText(const char *language)
{
	Vector velocity, baseVelocity;
	this->player->GetVelocity(&velocity);
	this->player->GetBaseVelocity(&baseVelocity);
	velocity += baseVelocity;
	// Keep the takeoff velocity on for a while after landing so the speed values flicker less.
	if ((this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND
		 && g_pBhopUtils->GetServerGlobals()->curtime - this->player->landingTime > HUD_ON_GROUND_THRESHOLD)
		|| (this->player->GetPlayerPawn()->m_MoveType == MOVETYPE_LADDER && !player->IsButtonPressed(IN_JUMP)))
	{
		return BhopLanguageService::PrepareMessageWithLang(language, "HUD - Speed Text", velocity.Length2D());
	}
	return BhopLanguageService::PrepareMessageWithLang(language, "HUD - Speed Text (Takeoff)", velocity.Length2D(),
													   this->player->takeoffVelocity.Length2D());
}

std::string BhopHUDService::GetKeyText(const char *language)
{
	// clang-format off

	return BhopLanguageService::PrepareMessageWithLang(language, "HUD - Key Text",
		this->player->IsButtonPressed(IN_MOVELEFT) ? 'A' : '_',
		this->player->IsButtonPressed(IN_FORWARD) ? 'W' : '_',
		this->player->IsButtonPressed(IN_BACK) ? 'S' : '_',
		this->player->IsButtonPressed(IN_MOVERIGHT) ? 'D' : '_',
		this->player->IsButtonPressed(IN_DUCK) ? 'C' : '_',
		this->jumpedThisTick ? 'J' : '_'
	);

	// clang-format on
}

std::string BhopHUDService::GetStageText(const char *language)
{
	// clang-format off

	int stage = this->player->timerService->GetStage();
	return stage > 0 
		? BhopLanguageService::PrepareMessageWithLang(language, "HUD - Stage Text", stage)
		: "";

	// clang-format on
}

std::string BhopHUDService::GetTimerText(const char *language)
{
	if (Bhop::replaysystem::IsReplayBot(this->player))
	{
		char timeText[128];

		f64 time = Bhop::replaysystem::GetTime();
		bool paused = Bhop::replaysystem::GetPaused();
		bool timerRunning = Bhop::replaysystem::GetEndTime() == 0.0f;
		// Show timer if time is not 0 or end time is not 0.
		if (time == 0.0f && Bhop::replaysystem::GetEndTime() == 0.0f)
		{
			return std::string("");
		}
		if (!timerRunning)
		{
			time = Bhop::replaysystem::GetEndTime();
		}
		utils::FormatTime(time, timeText, sizeof(timeText));
		// clang-format off
		return BhopLanguageService::PrepareMessageWithLang(language, "HUD - Timer Text",
			timeText,
			timerRunning ? "" : BhopLanguageService::PrepareMessageWithLang(language, "HUD - Stopped Text").c_str(),
			paused ? BhopLanguageService::PrepareMessageWithLang(language, "HUD - Paused Text").c_str() : ""
		);
		// clang-format on
	}
	if (this->player->timerService->GetTimerRunning() || this->ShouldShowTimerAfterStop())
	{
		char timeText[128];

		// clang-format off

		f64 time = this->player->timerService->GetTimerRunning()
			? player->timerService->GetTime()
			: this->currentTimeWhenTimerStopped;

		bool timerRunning = this->player->timerService->GetTimerRunning();
		bool paused = this->player->timerService->GetPaused();

		utils::FormatTime(time, timeText, sizeof(timeText));
		return BhopLanguageService::PrepareMessageWithLang(language, "HUD - Timer Text",
			timeText,
			timerRunning ? "" : BhopLanguageService::PrepareMessageWithLang(language, "HUD - Stopped Text").c_str(),
			paused ? BhopLanguageService::PrepareMessageWithLang(language, "HUD - Paused Text").c_str() : ""
		);
		// clang-format on
	}
	return std::string("");
}

void BhopHUDService::DrawPanels(BhopPlayer *player, BhopPlayer *target)
{
	if (!target->hudService->IsShowingPanel())
	{
		return;
	}
	const char *language = target->languageService->GetLanguage();

	std::string keyText = player->hudService->GetKeyText(language);
	std::string timerText = player->hudService->GetTimerText(language);
	std::string speedText = player->hudService->GetSpeedText(language);
	std::string stageText = player->hudService->GetStageText(language);

	// clang-format off
	std::string centerText = BhopLanguageService::PrepareMessageWithLang(language, "HUD - Center Text", 
		keyText.c_str(), stageText.c_str(), timerText.c_str(), speedText.c_str());
	std::string alertText = BhopLanguageService::PrepareMessageWithLang(language, "HUD - Alert Text", 
		keyText.c_str(), stageText.c_str(), timerText.c_str(), speedText.c_str());
	std::string htmlText = BhopLanguageService::PrepareMessageWithLang(language, "HUD - Html Center Text",
		keyText.c_str(), stageText.c_str(), timerText.c_str(), speedText.c_str());

	// clang-format on

	auto trimNewlines = [](std::string &str)
	{
		// Remove leading newlines
		size_t start = str.find_first_not_of('\n');
		if (start == std::string::npos)
		{
			str.clear();
			return;
		}
		// Remove trailing newlines
		size_t end = str.find_last_not_of('\n');
		str = str.substr(start, end - start + 1);
	};

	trimNewlines(centerText);
	trimNewlines(alertText);
	trimNewlines(htmlText);

	// Remove leading & trailing newlines just in case a line is empty.
	if (!centerText.empty())
	{
		target->PrintCentre(false, false, centerText.c_str());
	}
	if (!alertText.empty())
	{
		target->PrintAlert(false, false, alertText.c_str());
	}
	if (!htmlText.empty())
	{
		target->PrintHTMLCentre(false, false, htmlText.c_str());
	}
}

void BhopHUDService::ResetShowPanel()
{
	this->showPanel = this->player->optionService->GetPreferenceBool("showPanel", true);
}

void BhopHUDService::TogglePanel()
{
	this->showPanel = !this->showPanel;
	this->player->optionService->SetPreferenceBool("showPanel", this->showPanel);
	if (!this->showPanel)
	{
		utils::PrintAlert(this->player->GetController(), "#SFUI_EmptyString");
		utils::PrintCentre(this->player->GetController(), "#SFUI_EmptyString");
		this->player->languageService->PrintHTMLCentre(false, false, "HUD - HTML Panel Disabled");
	}
}

void BhopHUDService::OnTimerStopped(f64 currentTimeWhenTimerStopped)
{
	// g_pBhopUtils->GetServerGlobals() becomes invalid when the plugin is unloading.
	if (g_BhopPlugin.unloading)
	{
		return;
	}
	this->timerStoppedTime = g_pBhopUtils->GetServerGlobals()->curtime;
	this->currentTimeWhenTimerStopped = currentTimeWhenTimerStopped;
}

void BhopTimerServiceEventListener_HUD::OnTimerStopped(BhopPlayer *player, u32 courseGUID)
{
	player->hudService->OnTimerStopped(player->timerService->GetTime());
}

void BhopTimerServiceEventListener_HUD::OnTimerEndPost(BhopPlayer *player, u32 courseGUID, f32 time)
{
	player->hudService->OnTimerStopped(time);
}

SCMD(bhop_panel, SCFL_HUD)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	player->hudService->TogglePanel();
	if (player->hudService->IsShowingPanel())
	{
		player->languageService->PrintChat(true, false, "HUD Option - Info Panel - Enable");
	}
	else
	{
		player->languageService->PrintChat(true, false, "HUD Option - Info Panel - Disable");
	}
	return MRES_SUPERCEDE;
}
