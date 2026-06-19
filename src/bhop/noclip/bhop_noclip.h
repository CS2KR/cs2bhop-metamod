#pragma once
#include "../bhop.h"

#define BHOP_JUST_NOCLIP_TIME 0.05f;

class BhopNoclipService : public BhopBaseService
{
	using BhopBaseService::BhopBaseService;

private:
	f32 lastNoclipTime {};
	bool inNoclip {};

public:
	void DisableNoclip()
	{
		this->inNoclip = false;
	}

	void ToggleNoclip()
	{
		this->inNoclip = !this->inNoclip;
	}

	bool IsNoclipping()
	{
		return this->inNoclip;
	}

	bool JustNoclipped()
	{
		return g_pBhopUtils->GetServerGlobals()->curtime - lastNoclipTime < BHOP_JUST_NOCLIP_TIME;
	}

	virtual void Reset() override;
	void HandleMoveCollision();
	void HandleNoclip();

	void OnTeleport(const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity);
};
