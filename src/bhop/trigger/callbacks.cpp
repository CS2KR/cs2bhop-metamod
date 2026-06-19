#include "bhop_trigger.h"
#include "bhop/mode/bhop_mode.h"
#include "bhop/style/bhop_style.h"
#include "bhop/checkpoint/bhop_checkpoint.h"
#include "bhop/noclip/bhop_noclip.h"
#include "bhop/timer/bhop_timer.h"
#include "bhop/language/bhop_language.h"
#include "bhop/replays/bhop_replaysystem.h"

/*
	Note: Whether touching is allowed is set determined by the mode, while Mapping API effects will be applied after touching events.
*/

static_function void TeleportToBestStartOrRespawn(BhopPlayer *player)
{
	const BhopCourseDescriptor *course = player->timerService->GetCourse();
	if (!course && Bhop::course::GetCourseCount() == 1)
	{
		course = Bhop::course::GetFirstCourse();
	}

	if (course && course->hasStartPosition)
	{
		player->Teleport(&course->startPosition, &course->startAngles, &vec3_origin);
		return;
	}

	player->GetPlayerPawn()->Respawn();
}

// Whether we allow interaction from happening.
bool BhopTriggerService::OnTriggerStartTouchPre(CBaseTrigger *trigger)
{
	bool retValue = this->player->modeService->OnTriggerStartTouch(trigger);
	FOR_EACH_VEC(this->player->styleServices, i)
	{
		retValue &= this->player->styleServices[i]->OnTriggerStartTouch(trigger);
	}
	retValue &= Bhop::replaysystem::CanTouchTrigger(this->player, trigger);
	return retValue;
}

bool BhopTriggerService::OnTriggerTouchPre(CBaseTrigger *trigger, TriggerTouchTracker tracker)
{
	bool retValue = this->player->modeService->OnTriggerTouch(trigger);
	FOR_EACH_VEC(this->player->styleServices, i)
	{
		retValue &= this->player->styleServices[i]->OnTriggerTouch(trigger);
	}
	retValue &= Bhop::replaysystem::CanTouchTrigger(this->player, trigger);
	return retValue;
}

bool BhopTriggerService::OnTriggerEndTouchPre(CBaseTrigger *trigger, TriggerTouchTracker tracker)
{
	bool retValue = this->player->modeService->OnTriggerEndTouch(trigger);
	FOR_EACH_VEC(this->player->styleServices, i)
	{
		retValue &= this->player->styleServices[i]->OnTriggerEndTouch(trigger);
	}
	retValue &= Bhop::replaysystem::CanTouchTrigger(this->player, trigger);
	return retValue;
}

// Mapping API stuff.
void BhopTriggerService::OnTriggerStartTouchPost(CBaseTrigger *trigger, TriggerTouchTracker &tracker)
{
	if (!tracker.bhopTrigger || !trigger->PassesTriggerFilters(this->player->GetPlayerPawn()))
	{
		return;
	}
	tracker.mappingApiStartedTouch = true;
	this->OnMappingApiTriggerStartTouchPost(tracker);
}

void BhopTriggerService::OnTriggerTouchPost(CBaseTrigger *trigger, TriggerTouchTracker &tracker)
{
	if (!tracker.bhopTrigger)
	{
		return;
	}
	bool filterPasses = trigger->PassesTriggerFilters(this->player->GetPlayerPawn());
	if (!filterPasses)
	{
		// Filter lost while inside the trigger - treat as a virtual end touch.
		if (tracker.mappingApiStartedTouch)
		{
			tracker.mappingApiStartedTouch = false;
			this->OnMappingApiTriggerEndTouchPost(tracker);
		}
		return;
	}
	// The filter wasn't active when the player entered this trigger, but it is now.
	// We need to retroactively fire StartTouch for this trigger so that modifiers get applied correctly.
	if (!tracker.mappingApiStartedTouch)
	{
		tracker.mappingApiStartedTouch = true;
		this->OnMappingApiTriggerStartTouchPost(tracker);
	}
	this->OnMappingApiTriggerTouchPost(tracker);
}

void BhopTriggerService::OnTriggerEndTouchPost(CBaseTrigger *trigger, TriggerTouchTracker &tracker)
{
	// Only fire the mapping API end touch if the mapping API start touch was actually fired,
	// so that counter increments/decrements stay symmetric regardless of when the filter became active.
	if (!tracker.bhopTrigger || !tracker.mappingApiStartedTouch)
	{
		return;
	}
	this->OnMappingApiTriggerEndTouchPost(tracker);
}

void BhopTriggerService::AddPushEvent(const BhopTrigger *trigger)
{
	f32 curtime = g_pBhopUtils->GetGlobals()->curtime;
	PushEvent event {trigger, curtime + trigger->push.delay};
	if (this->pushEvents.Find(event) == -1)
	{
		this->pushEvents.AddToTail(event);
	}
}

void BhopTriggerService::CleanupPushEvents()
{
	f32 frametime = g_pBhopUtils->GetGlobals()->frametime;
	// Don't remove push events since these push events are not fired yet.
	if (frametime == 0.0f)
	{
		return;
	}
	f32 curtime = g_pBhopUtils->GetGlobals()->curtime;
	FOR_EACH_VEC_BACK(this->pushEvents, i)
	{
		if (!this->pushEvents[i].applied)
		{
			continue;
		}
		if (curtime - frametime >= this->pushEvents[i].pushTime + this->pushEvents[i].source->push.cooldown)
		{
			this->pushEvents.Remove(i);
		}
	}
}

void BhopTriggerService::ApplyPushes()
{
	f32 frametime = g_pBhopUtils->GetGlobals()->frametime;
	// There's no point applying any push if player isn't going to move anyway.
	if (frametime == 0.0f)
	{
		return;
	}
	f32 curtime = g_pBhopUtils->GetGlobals()->curtime;
	bool setSpeed[3] {};

	if (this->pushEvents.Count() == 0)
	{
		return;
	}
	bool useBaseVelocity = this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND && this->player->processingMovement;
	FOR_EACH_VEC(this->pushEvents, i)
	{
		if (curtime - frametime >= this->pushEvents[i].pushTime || curtime < this->pushEvents[i].pushTime || this->pushEvents[i].applied)
		{
			continue;
		}
		this->pushEvents[i].applied = true;
		auto &push = const_cast<BhopMapPush &>(this->pushEvents[i].source->push);
		if (push.pushConditions & BhopMapPush::BHOP_PUSH_LEGACY)
		{
			Vector forward;
			QAngle rotation = this->pushEvents[i].source->rotation;
			AngleVectors(rotation, &forward);
			VectorNormalize(forward);
			Vector pushImpulse = forward * push.speed;

			push.impulse[0] = pushImpulse.x;
			push.impulse[1] = pushImpulse.y;
			push.impulse[2] = pushImpulse.z;
		}
		for (u32 i = 0; i < 3; i++)
		{
			Vector vel;
			if (useBaseVelocity && i != 2)
			{
				this->player->GetBaseVelocity(&vel);
			}
			else
			{
				this->player->GetVelocity(&vel);
			}
			// Set speed overrides add speed.
			if (push.setSpeed[i])
			{
				vel[i] = push.impulse[i];
				setSpeed[i] = true;
			}
			else if (!setSpeed[i])
			{
				vel[i] += push.impulse[i];
			}
			// If we are pushing the player up, make sure they cannot re-ground themselves.
			if (i == 2 && vel[i] > 0 && useBaseVelocity)
			{
				this->player->GetPlayerPawn()->m_hGroundEntity().FromIndex(INVALID_EHANDLE_INDEX);
				this->player->GetPlayerPawn()->m_fFlags() &= ~FL_ONGROUND;
				this->player->currentMoveData->m_groundNormal = vec3_origin;
			}
			if (useBaseVelocity && i != 2)
			{
				this->player->SetBaseVelocity(vel);
				this->player->GetPlayerPawn()->m_fFlags() |= FL_BASEVELOCITY;
			}
			else
			{
				this->player->SetVelocity(vel);
			}
		}
	}
	// Try to nullify velocity if needed.
	if (useBaseVelocity)
	{
		Vector velocity, newVelocity;
		this->player->GetVelocity(&velocity);
		newVelocity = velocity;
		for (u32 i = 0; i < 2; i++)
		{
			if (setSpeed[i])
			{
				newVelocity[i] = 0;
			}
		}
		if (velocity != newVelocity)
		{
			this->player->SetVelocity(newVelocity);
		}
	}
}

void BhopTriggerService::OnMappingApiTriggerStartTouchPost(TriggerTouchTracker tracker)
{
	const BhopTrigger *trigger = tracker.bhopTrigger;
	const BhopCourseDescriptor *course = Bhop::mapapi::GetCourseDescriptorFromTrigger(trigger);
	if (Bhop::mapapi::IsTimerTrigger(trigger->type) && !course)
	{
		return;
	}

	switch (trigger->type)
	{
		case BHOPTRIGGER_MODIFIER:
		{
			BhopMapModifier modifier = trigger->modifier;
		}
		break;

		case BHOPTRIGGER_ZONE_START:
		case BHOPTRIGGER_ZONE_BONUS_START:
		{
			this->player->timerService->StartZoneStartTouch(course);
		}
		break;

		case BHOPTRIGGER_ZONE_END:
		case BHOPTRIGGER_ZONE_BONUS_END:
		{
			this->player->timerService->TimerEnd(course);
		}
		break;

		case BHOPTRIGGER_ZONE_CHECKPOINT:
		{
			this->player->timerService->CheckpointZoneStartTouch(course, trigger->zone.number);
		}
		break;

		case BHOPTRIGGER_ZONE_STAGE:
		{
			this->player->timerService->StageZoneStartTouch(course, trigger->zone.number);
		}
		break;
		case BHOPTRIGGER_PUSH:
		{
			if (tracker.bhopTrigger->push.pushConditions & BhopMapPush::BHOP_PUSH_START_TOUCH)
			{
				this->AddPushEvent(trigger);
			}
		}
		break;
		case BHOPTRIGGER_ACTION_STOP:
		{
			this->player->timerService->TimerStop(false);
			this->player->timerService->InvalidateRun();
			this->player->checkpointService->ResetCheckpoints(false);
		}
		break;
		case BHOPTRIGGER_ACTION_RESET:
		{
			this->player->timerService->TimerStop(false);
			this->player->timerService->InvalidateRun();
			this->player->checkpointService->ResetCheckpoints(false);
			TeleportToBestStartOrRespawn(this->player);
		}
		break;
		default:
			break;
	}
}

void BhopTriggerService::OnMappingApiTriggerTouchPost(TriggerTouchTracker tracker)
{
	bool shouldRecheckTriggers = false;
	const BhopTrigger *trigger = tracker.bhopTrigger;
	const BhopCourseDescriptor *course = Bhop::mapapi::GetCourseDescriptorFromTrigger(trigger);
	if (Bhop::mapapi::IsTimerTrigger(trigger->type) && !course)
	{
		return;
	}

	switch (tracker.bhopTrigger->type)
	{
		case BHOPTRIGGER_MODIFIER:
		{
			this->TouchModifierTrigger(tracker);
		}
		break;
		case BHOPTRIGGER_TELEPORT:
		{
			if (!tracker.bhopTrigger->teleport.relative)
			{
				this->TouchTeleportTrigger(tracker);
			}
		}
		break;
		case BHOPTRIGGER_PUSH:
		{
			this->TouchPushTrigger(tracker);
		}
		break;
		case BHOPTRIGGER_ZONE_START:
		case BHOPTRIGGER_ZONE_BONUS_START:
		{
			if (!this->player->timerService->GetCourse())
			{
				this->player->timerService->SetCourse(course->guid);
			}
			else if (this->player->timerService->GetCourse()->guid != course->guid)
			{
				this->player->timerService->SetCourse(course->guid);
			}

			// First check for maxvel style
			for (i32 i = 0; i < player->styleServices.Count(); i++)
			{
				CUtlString shortName(Bhop::style::GetStyleInfo(player->styleServices[i]).shortName);
				if (shortName.MatchesPattern("*vel"))
				{
					// Maxvel convar is already sent by style
					break;
				}
			}

			// Send course-specific maxvel if no style applied
			const CVValue_t maxVel = (float)course->maxVel;
			utils::SendConVarValue(player->GetPlayerSlot(), "sv_maxvelocity", &maxVel);
		}
		break;
	}
}

void BhopTriggerService::OnMappingApiTriggerEndTouchPost(TriggerTouchTracker tracker)
{
	const BhopTrigger *trigger = tracker.bhopTrigger;
	const BhopCourseDescriptor *course = Bhop::mapapi::GetCourseDescriptorFromTrigger(tracker.bhopTrigger);
	if (Bhop::mapapi::IsTimerTrigger(tracker.bhopTrigger->type) && !course)
	{
		return;
	}

	switch (tracker.bhopTrigger->type)
	{
		case BHOPTRIGGER_MODIFIER:
		{
			BhopMapModifier modifier = tracker.bhopTrigger->modifier;
		}
		break;

		case BHOPTRIGGER_ZONE_START:
		case BHOPTRIGGER_ZONE_BONUS_START:
		{
			this->player->timerService->StartZoneEndTouch(course);
		}
		break;
		case BHOPTRIGGER_ZONE_STAGE:
		{
			this->player->timerService->StageZoneEndTouch(course, trigger->zone.number);
		}
		break;
		case BHOPTRIGGER_PUSH:
		{
			if (tracker.bhopTrigger->push.pushConditions & BhopMapPush::BHOP_PUSH_END_TOUCH)
			{
				this->AddPushEvent(tracker.bhopTrigger);
			}
		}
		break;
		default:
			break;
	}
}
