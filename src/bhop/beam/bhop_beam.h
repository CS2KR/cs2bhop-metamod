#pragma once
#include "bhop/bhop.h"
#include "tier1/circularbuffer.h"

class BhopBeamService : public BhopBaseService
{
public:
	using BhopBaseService::BhopBaseService;

	static void Init();
	virtual void Reset();
	static void UpdateBeams();
	void Update();
	CEntityHandle playerBeam;
	CEntityHandle playerBeamNew;
	static inline const Vector defaultOffset {0.0f, 0.0f, 1.75f};
	Vector playerBeamOffset {defaultOffset};

	BhopPlayer *target {};
	bool validBeam {};

	enum BeamType : u8
	{
		BEAM_NONE = 0,
		BEAM_FEET,
		BEAM_COUNT
	} desiredBeamType;

	void SetBeamType(u8 type)
	{
		desiredBeamType = (BeamType)type;
	}

	void UpdatePlayerBeam();

	bool teleportedThisTick = false;

	void OnTeleport()
	{
		teleportedThisTick = true;
	}

	void OnPlayerPreferencesLoaded();
};
