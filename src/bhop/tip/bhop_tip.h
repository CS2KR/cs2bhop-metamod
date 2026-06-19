#pragma once
#include "../bhop.h"
#include "utils/utils.h"
#include "utils/simplecmds.h"
#include "KeyValues.h"
#include "interfaces/interfaces.h"
#include "filesystem.h"
#include "utils/ctimer.h"
#include "bhop/option/bhop_option.h"

class BhopTipService : public BhopBaseService
{
	using BhopBaseService::BhopBaseService;

private:
	bool showTips {};
	bool teamJoinedAtLeastOnce {};
	bool timerStartedAtLeastOnce {};

public:
	virtual void Reset() override;
	void ToggleTips();
	static void Init();
	static f64 PrintTips();
	void OnPlayerJoinTeam(i32 team);
	void OnTimerStartPost();
	void QueryBeamCvar();

private:
	bool ShouldPrintTip();
	void PrintTip();
	static void LoadTips();
	static void ShuffleTips();
};
