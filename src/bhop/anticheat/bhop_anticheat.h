#pragma once
#include "../bhop.h"
class BhopBaseService;

class BhopAnticheatService : public BhopBaseService
{
public:
	using BhopBaseService::BhopBaseService;

private:
	bool hasValidCvars = true;

public:
	bool ShouldCheckClientCvars()
	{
		return hasValidCvars;
	}

	void MarkHasInvalidCvars()
	{
		hasValidCvars = false;
	}

	void OnPlayerFullyConnect();
};
