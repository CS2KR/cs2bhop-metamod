#pragma once

#include "bhop/bhop.h"

class BhopTelemetryService : public BhopBaseService
{
public:
	using BhopBaseService::BhopBaseService;

private:
	static f64 lastActiveCheckTime;

	struct ActiveStats
	{
		f32 lastActionTime {};
		f64 activeTime {};
		f64 afkDuration {};
		f64 timeSpentInServer {};
	} activeStats;

public:
	virtual void Reset() override
	{
		this->activeStats = {};
	}

	static void ActiveCheck();

	f64 GetActiveTime() const
	{
		return activeStats.activeTime;
	}

	f64 GetAFKTime() const
	{
		return activeStats.afkDuration;
	}

	f64 GetSpectatingTime() const
	{
		return activeStats.timeSpentInServer - activeStats.activeTime - activeStats.afkDuration;
	}

	f64 GetTimeInServer() const
	{
		return activeStats.timeSpentInServer;
	}

	void OnPhysicsSimulatePost();
};
