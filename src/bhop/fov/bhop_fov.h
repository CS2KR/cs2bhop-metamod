#pragma once
#include "bhop/bhop.h"
#include "bhop/option/bhop_option.h"

class BhopFOVService : public BhopBaseService
{
	using BhopBaseService::BhopBaseService;

public:
	static u32 GetMinFOV()
	{
		return BhopOptionService::GetOptionInt("minFOV", 80);
	}

	static u32 GetMaxFOV()
	{
		return BhopOptionService::GetOptionInt("maxFOV", 130);
	}

	static u32 GetDefaultFOV()
	{
		return BhopOptionService::GetOptionInt("defaultFOV", 90);
	}

	void SetFOV(u32 newFOV)
	{
		this->player->optionService->SetPreferenceInt("fov", newFOV);
	}

	u32 GetFOV()
	{
		return this->player->optionService->GetPreferenceInt("fov", this->GetDefaultFOV());
	}

	void OnPhysicsSimulate();
};
