#ifndef BHOP_REPLAYEVENTS_H
#define BHOP_REPLAYEVENTS_H

#include "sdk/datatypes.h"
#include "bhop_replay.h"
#include "data.h"

class BhopPlayer;

namespace Bhop::replaysystem::events
{
	// Event processing functions
	void CheckEvents(BhopPlayer &player);

	// Event reprocessing for navigation
	void ReprocessEventsUpToTick(data::ReplayPlayback *replay, u32 targetTick);

	// Specific event handlers
	void HandleTimerEvent(BhopPlayer &player, const RpEvent *event, data::ReplayPlayback *replay);
	void HandleCheckpointEvent(const RpEvent *event, data::ReplayPlayback *replay);
	void HandleModeChangeEvent(BhopPlayer &player, const RpEvent *event);
	void HandleStyleChangeEvent(BhopPlayer &player, const RpEvent *event);
	void HandleTeleportEvent(BhopPlayer &player, const RpEvent *event);
} // namespace Bhop::replaysystem::events

#endif // BHOP_REPLAYEVENTS_H
