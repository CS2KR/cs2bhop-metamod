#pragma once
#include "../bhop.h"
#include "../timer/bhop_timer.h"

#define BHOP_HUD_TIMER_STOPPED_GRACE_TIME 3.0f

class BhopHUDService : public BhopBaseService
{
	using BhopBaseService::BhopBaseService;

private:
	bool jumpedThisTick {};
	bool showPanel {};
	f64 timerStoppedTime {};
	f64 currentTimeWhenTimerStopped {};

public:
	virtual void Reset() override;
	static void Init();

	// Draw the panel from a player to a specific target.
	static void DrawPanels(BhopPlayer *player, BhopPlayer *target);

	void ResetShowPanel();
	void TogglePanel();

	void OnPhysicsSimulate()
	{
		jumpedThisTick = false;
	}

	void OnJump()
	{
		jumpedThisTick = true;
	}

	bool IsShowingPanel()
	{
		return this->showPanel;
	}

	void OnTimerStopped(f64 currentTimeWhenTimerStopped);

	bool ShouldShowTimerAfterStop()
	{
		return g_pBhopUtils->GetServerGlobals()->curtime > BHOP_HUD_TIMER_STOPPED_GRACE_TIME
			   && g_pBhopUtils->GetServerGlobals()->curtime - timerStoppedTime < BHOP_HUD_TIMER_STOPPED_GRACE_TIME;
	}

private:
	std::string GetSpeedText(const char *language = BHOP_DEFAULT_LANGUAGE);
	std::string GetKeyText(const char *language = BHOP_DEFAULT_LANGUAGE);
	std::string GetTimerText(const char *language = BHOP_DEFAULT_LANGUAGE);
	std::string GetStageText(const char *language = BHOP_DEFAULT_LANGUAGE);
};
