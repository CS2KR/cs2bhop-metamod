#include "bhop_trigger.h"
#include "bhop/checkpoint/bhop_checkpoint.h"
#include "bhop/language/bhop_language.h"
#include "bhop/mode/bhop_mode.h"
#include "bhop/style/bhop_style.h"
#include "bhop/timer/bhop_timer.h"

void BhopTriggerService::TouchModifierTrigger(TriggerTouchTracker tracker)
{
	const BhopTrigger *trigger = tracker.bhopTrigger;

	if (trigger->modifier.gravity != 1)
	{
		// No gravity while paused.
		if (this->player->timerService->GetPaused())
		{
			this->player->GetPlayerPawn()->SetGravityScale(0);
			return;
		}
		this->player->GetPlayerPawn()->SetGravityScale(trigger->modifier.gravity);
	}
	this->modifiers.jumpFactor = trigger->modifier.jumpFactor;
}

bool BhopTriggerService::TouchTeleportTrigger(TriggerTouchTracker tracker)
{
	bool shouldTeleport = false;

	CEntityHandle destinationHandle = GameEntitySystem()->FindFirstEntityHandleByName(tracker.bhopTrigger->teleport.destination);
	CBaseEntity *destination = dynamic_cast<CBaseEntity *>(GameEntitySystem()->GetEntityInstance(destinationHandle));
	if (!destinationHandle.IsValid() || !destination)
	{
		META_CONPRINTF("Invalid teleport destination \"%s\" on trigger with hammerID %i.\n", tracker.bhopTrigger->teleport.destination,
					   tracker.bhopTrigger->hammerId);
		if (strcmp(tracker.bhopTrigger->teleport.destination, "!self") == 0)
		{
			// very very rare edgecase but shouldnt cause issue?
			this->player->SetVelocity(vec3_origin);
		}
		return false;
	}

	Vector destOrigin = destination->m_CBodyComponent()->m_pSceneNode()->m_vecAbsOrigin();
	QAngle destAngles = destination->m_CBodyComponent()->m_pSceneNode()->m_angRotation();
	CBaseEntity *trigger = dynamic_cast<CBaseTrigger *>(GameEntitySystem()->GetEntityInstance(tracker.bhopTrigger->entity));
	Vector triggerOrigin = Vector(0, 0, 0);
	if (trigger)
	{
		triggerOrigin = trigger->m_CBodyComponent()->m_pSceneNode()->m_vecAbsOrigin();
	}

	// NOTE: We only use the landmarks's origin if we're using a relative destination, so if
	// we're not using a relative destination and don't have it, then it's fine.
	CEntityHandle landmarkHandle = GameEntitySystem()->FindFirstEntityHandleByName(tracker.bhopTrigger->teleport.landmark);
	CBaseEntity *landmark = dynamic_cast<CBaseEntity *>(GameEntitySystem()->GetEntityInstance(landmarkHandle));
	if (!landmark && tracker.bhopTrigger->teleport.relative)
	{
		return false;
	}

	if (tracker.bhopTrigger->type == BHOPTRIGGER_TELEPORT)
	{
		f32 touchingTime = g_pBhopUtils->GetServerGlobals()->curtime - tracker.startTouchTime;
		shouldTeleport = touchingTime > tracker.bhopTrigger->teleport.delay || tracker.bhopTrigger->teleport.delay <= 0;
	}

	if (!shouldTeleport)
	{
		return false;
	}

	bool shouldReorientPlayer = tracker.bhopTrigger->teleport.reorientPlayer && destAngles[YAW] != 0;
	Vector finalOrigin = destOrigin;

	if (tracker.bhopTrigger->teleport.relative)
	{
		Vector playerOrigin;
		this->player->GetOrigin(&playerOrigin);
		Vector landmarkOrigin = landmark->m_CBodyComponent()->m_pSceneNode()->m_vecAbsOrigin();
		Vector playerOffsetFromLandmark = playerOrigin - landmarkOrigin;

		if (shouldReorientPlayer)
		{
			VectorRotate(playerOffsetFromLandmark, QAngle(0, destAngles[YAW], 0), playerOffsetFromLandmark);
		}
		finalOrigin = destOrigin + playerOffsetFromLandmark;
	}
	QAngle finalPlayerAngles;
	this->player->GetAngles(&finalPlayerAngles);
	Vector finalVelocity;
	this->player->GetVelocity(&finalVelocity);
	if (shouldReorientPlayer)
	{
		// TODO: BUG: sometimes when getting reoriented and holding a movement key
		//  the player's speed will get reduced, almost like velocity rotation
		//  and angle rotation is out of sync leading to counterstrafing.
		// Maybe we should check m_nHighestGeneratedServerViewAngleChangeIndex for angles overridding...
		VectorRotate(finalVelocity, QAngle(0, destAngles[YAW], 0), finalVelocity);
		finalPlayerAngles[YAW] -= destAngles[YAW];
		this->player->SetAngles(finalPlayerAngles);
	}
	else if (!tracker.bhopTrigger->teleport.reorientPlayer && tracker.bhopTrigger->teleport.useDestinationAngles)
	{
		this->player->SetAngles(destAngles);
	}

	if (tracker.bhopTrigger->teleport.resetSpeed)
	{
		this->player->SetVelocity(vec3_origin);
	}
	else
	{
		this->player->SetVelocity(finalVelocity);
	}

	const BhopTrigger *zoneDestination = Bhop::mapapi::IsPositionInOrAboveTimerZone(finalOrigin);
	if (zoneDestination)
	{
		// set velo to 0 for bonus/main/stage zones
		this->player->SetVelocity(vec3_origin);
		if (zoneDestination->type != BHOPTRIGGER_ZONE_STAGE)
		{
			// only stop timer for bonus/main startzones
			this->player->timerService->TimerStop(false);
		}
	}

	// We need to call teleport hook because we don't use teleport function directly.
	if (this->player->processingMovement && this->player->currentMoveData)
	{
		this->player->OnTeleport(&finalOrigin, nullptr, nullptr);
	}
	this->player->SetOrigin(finalOrigin);

	return true;
}

void BhopTriggerService::TouchPushTrigger(TriggerTouchTracker tracker)
{
	u32 pushConditions = tracker.bhopTrigger->push.pushConditions;
	// clang-format off
	if (pushConditions & BhopMapPush::BHOP_PUSH_TOUCH
		|| (this->player->IsButtonNewlyPressed(IN_ATTACK) && pushConditions & BhopMapPush::BHOP_PUSH_ATTACK)
		|| (this->player->IsButtonNewlyPressed(IN_ATTACK2) && pushConditions & BhopMapPush::BHOP_PUSH_ATTACK2)
		|| (this->player->IsButtonNewlyPressed(IN_JUMP) && pushConditions & BhopMapPush::BHOP_PUSH_JUMP_BUTTON)
		|| (this->player->IsButtonNewlyPressed(IN_USE) && pushConditions & BhopMapPush::BHOP_PUSH_USE))
	// clang-format on
	{
		this->AddPushEvent(tracker.bhopTrigger);
	}
}

void BhopTriggerService::ApplyJumpFactor(bool replicate)
{
	const CVValue_t *impulseModeValue = player->GetCvarValueFromModeStyles("sv_jump_impulse");
	const CVValue_t newImpulseValue = (impulseModeValue->m_fl32Value * this->modifiers.jumpFactor);
	utils::SetConVarValue(player->GetPlayerSlot(), "sv_jump_impulse", &newImpulseValue, replicate);

	const CVValue_t *jumpCostValue = player->GetCvarValueFromModeStyles("sv_staminajumpcost");
	const CVValue_t newJumpCostValue = (jumpCostValue->m_fl32Value / this->modifiers.jumpFactor);
	utils::SetConVarValue(player->GetPlayerSlot(), "sv_staminajumpcost", &newJumpCostValue, replicate);
}
