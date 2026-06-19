#include "bhop/bhop.h"
#include "bhop/trigger/bhop_trigger.h"
#include "sdk/entity/cbasemodelentity.h"

extern BhopZoneBeamService *g_pBhopZoneBeamService;

struct BhopZoneBeamSet
{
	CHandle<CBeam> beams[12];
};

class BhopZoneBeamService : public BhopBaseService
{
public:
	using BhopBaseService::BhopBaseService;

	CUtlVector<BhopZoneBeamSet> startZoneBeams;
	CUtlVector<BhopZoneBeamSet> endZoneBeams;

	static void Init();

	void AddZone(BhopTrigger *trigger);
	void CreateZoneOutlineBeams(BhopTrigger *trigger, BhopZoneBeamSet &beamSet, Color color);
	CHandle<CBeam> CreatePersistentBeam(const Vector &start, const Vector &end, Color color);
};
