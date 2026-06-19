#pragma once
#include "../bhop.h"

class BhopGotoService : public BhopBaseService
{
	using BhopBaseService::BhopBaseService;

private:

public:
	virtual void Reset() override;
	static void Init();

	bool GotoPlayer(const char *playerNamePart);
};
