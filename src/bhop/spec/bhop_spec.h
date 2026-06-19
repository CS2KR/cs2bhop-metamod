#pragma once
#include "../bhop.h"

class BhopSpecService : public BhopBaseService
{
	using BhopBaseService::BhopBaseService;

private:
	bool savedPosition;
	Vector savedOrigin;
	QAngle savedAngles;
	bool savedOnLadder;

public:
	virtual void Reset() override;
	static void Init();
	bool HasSavedPosition();
	void SavePosition();
	void LoadPosition();
	void ResetSavedPosition();

	bool IsSpectating(BhopPlayer *target);
	bool SpectatePlayer(const char *playerName);
	bool SpectatePlayer(BhopPlayer *target);
	bool CanSpectate();

	void GetSpectatorList(CUtlVector<CUtlString> &spectatorList);
	BhopPlayer *GetSpectatedPlayer();
	BhopPlayer *GetNextSpectator(BhopPlayer *current);
};
