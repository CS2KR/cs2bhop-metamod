#include "bhop_mode.h"
#include "sdk/datatypes.h"

CConVarRef<f32> sv_standable_normal("sv_standable_normal");

void BhopModeService::OnStopTouchGround()
{
	Vector velocity;
	this->player->GetVelocity(&velocity);
	this->player->takeoffVelocity = velocity;
}

void BhopModeService::OnStartTouchGround()
{
	this->SlopeFix();
	bbox_t bounds;
	this->player->GetBBoxBounds(&bounds);
	Vector ground = this->player->landingOrigin;
	ground.z = this->player->GetGroundPosition() - 0.03125f;
	this->player->TouchTriggersAlongPath(this->player->landingOrigin, ground, bounds);
}

void BhopModeService::OnProcessMovement()
{
	this->didTPM = false;
	if (this->player->GetPlayerPawn()->m_flVelocityModifier() != 1.0f)
	{
		this->player->GetPlayerPawn()->m_flVelocityModifier(1.0f);
	}
	this->CheckVelocityQuantization();
	this->ReduceDuckSlowdown();
	this->InterpolateViewAngles();
	this->UpdateAngleHistory();
}

void BhopModeService::OnPlayerMove()
{
	this->originalMaxSpeed = this->player->currentMoveData->m_flMaxSpeed;
	this->player->currentMoveData->m_flMaxSpeed = SPEED_NORMAL;
}

void BhopModeService::OnProcessMovementPost()
{
	this->player->UpdateTriggerTouchList();
	this->RestoreInterpolatedViewAngles();
	this->oldDuckPressed = this->forcedUnduck || this->player->IsButtonPressed(IN_DUCK, true);
	this->oldJumpPressed = this->player->IsButtonPressed(IN_JUMP);
	Vector velocity;
	this->player->GetVelocity(&velocity);
	this->postProcessMovementZSpeed = velocity.z;
	if (!this->didTPM)
	{
		this->lastValidPlane = vec3_origin;
	}
}

void BhopModeService::InterpolateViewAngles()
{
	// Second half of the movement, no change.
	CGlobalVars *globals = g_pBhopUtils->GetGlobals();
	f64 subtickFraction, whole;
	subtickFraction = modf((f64)globals->curtime * ENGINE_FIXED_TICK_RATE, &whole);
	if (subtickFraction < 0.001)
	{
		return;
	}

	// First half of the movement, tweak the angle to be the middle of the desired angle and the last angle
	QAngle newAngles = player->currentMoveData->m_vecViewAngles;
	QAngle oldAngles = this->hasValidDesiredViewAngle ? this->lastValidDesiredViewAngle : this->player->moveDataPost.m_vecViewAngles;
	if (newAngles[YAW] - oldAngles[YAW] > 180)
	{
		newAngles[YAW] -= 360.0f;
	}
	else if (newAngles[YAW] - oldAngles[YAW] < -180)
	{
		newAngles[YAW] += 360.0f;
	}

	for (u32 i = 0; i < 3; i++)
	{
		newAngles[i] += oldAngles[i];
		newAngles[i] *= 0.5f;
	}
	player->currentMoveData->m_vecViewAngles = newAngles;
}

void BhopModeService::RestoreInterpolatedViewAngles()
{
	player->currentMoveData->m_vecViewAngles = player->moveDataPre.m_vecViewAngles;
	if (g_pBhopUtils->GetGlobals()->frametime > 0.0f)
	{
		this->hasValidDesiredViewAngle = true;
		this->lastValidDesiredViewAngle = player->currentMoveData->m_vecViewAngles;
	}
}

void BhopModeService::ReduceDuckSlowdown()
{
	if (!this->player->GetMoveServices()->m_bDucking && this->player->GetMoveServices()->m_flDuckSpeed < DUCK_SPEED_NORMAL - EPSILON)
	{
		this->player->GetMoveServices()->m_flDuckSpeed = DUCK_SPEED_NORMAL;
	}
	else if (this->player->GetMoveServices()->m_flDuckSpeed < DUCK_SPEED_MINIMUM - EPSILON)
	{
		this->player->GetMoveServices()->m_flDuckSpeed = DUCK_SPEED_MINIMUM;
	}
}

void BhopModeService::UpdateAngleHistory()
{
	CMoveData *mv = this->player->currentMoveData;
	u32 oldEntries = 0;
	FOR_EACH_VEC(this->angleHistory, i)
	{
		if (this->angleHistory[i].when < g_pBhopUtils->GetGlobals()->curtime)
		{
			oldEntries++;
			continue;
		}
		break;
	}
	this->angleHistory.RemoveMultipleFromHead(oldEntries);
	if ((this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND) == 0)
	{
		return;
	}

	AngleHistory *angHist = this->angleHistory.AddToTailGetPtr();
	angHist->when = g_pBhopUtils->GetGlobals()->curtime;
	angHist->duration = g_pBhopUtils->GetGlobals()->frametime;

	// Not turning if velocity is null.
	if (mv->m_vecVelocity.Length2D() == 0)
	{
		angHist->rate = 0;
		return;
	}

	// Copying from WalkMove
	Vector forward, right, up;
	AngleVectors(mv->m_vecViewAngles, &forward, &right, &up);

	f32 fmove = mv->m_flForwardMove;
	f32 smove = -mv->m_flSideMove;

	if (forward[2] != 0)
	{
		forward[2] = 0;
		forward = g_pBhopUtils->NormalizeVector(forward);
	}

	if (right[2] != 0)
	{
		right[2] = 0;
		right = g_pBhopUtils->NormalizeVector(right);
	}

	Vector wishdir;
	for (int i = 0; i < 2; i++)
	{
		wishdir[i] = forward[i] * fmove + right[i] * smove;
	}
	wishdir[2] = 0;

	wishdir = g_pBhopUtils->NormalizeVector(wishdir);

	if (wishdir.Length() == 0)
	{
		angHist->rate = 0;
		return;
	}

	Vector velocity = mv->m_vecVelocity;
	velocity[2] = 0;
	velocity = g_pBhopUtils->NormalizeVector(velocity);
	QAngle accelAngle;
	QAngle velAngle;
	VectorAngles(wishdir, accelAngle);
	VectorAngles(velocity, velAngle);
	accelAngle.y = g_pBhopUtils->NormalizeDeg(accelAngle.y);
	velAngle.y = g_pBhopUtils->NormalizeDeg(velAngle.y);
	angHist->rate = g_pBhopUtils->GetAngleDifference(velAngle.y, accelAngle.y, 180.0, true);
}

void BhopModeService::CheckVelocityQuantization()
{
	if (this->postProcessMovementZSpeed > this->player->currentMoveData->m_vecVelocity.z
		&& this->postProcessMovementZSpeed - this->player->currentMoveData->m_vecVelocity.z < 0.03125f
		// Colliding with a flat floor can result in a velocity of +0.0078125u/s, and this breaks ladders.
		// The quantization accidentally fixed this bug...
		&& fabs(this->player->currentMoveData->m_vecVelocity.z) > 0.03125f)
	{
		this->player->currentMoveData->m_vecVelocity.z = this->postProcessMovementZSpeed;
	}
}

// ORIGINAL AUTHORS : Mev & Blacky
// URL: https://forums.alliedmods.net/showthread.php?p=2322788
void BhopModeService::SlopeFix()
{
	CTraceFilterPlayerMovementCS filter(this->player->GetPlayerPawn());

	Vector ground = this->player->currentMoveData->m_vecAbsOrigin;
	ground.z -= 2;

	f32 standableZ = 0.7f; // Equal to the mode's cvar.

	if (sv_standable_normal.IsValidRef() && sv_standable_normal.IsConVarDataAvailable())
	{
		standableZ = sv_standable_normal.Get();
	}
	bbox_t bounds;
	this->player->GetBBoxBounds(&bounds);
	trace_t trace;

	INavPhysicsInterface::TraceShape(Ray_t(bounds.mins, bounds.maxs), this->player->currentMoveData->m_vecAbsOrigin, ground, &filter, &trace);

	// Doesn't hit anything, fall back to the original ground
	if (trace.m_bStartInSolid || trace.m_flFraction == 1.0f)
	{
		return;
	}

	if (standableZ <= trace.m_vHitNormal.z && trace.m_vHitNormal.z < 1.0f)
	{
		// Copy the ClipVelocity function from sdk2013
		float backoff;
		float change;
		Vector newVelocity;

		backoff = DotProduct(this->player->landingVelocity, trace.m_vHitNormal) * 1;

		for (u32 i = 0; i < 3; i++)
		{
			change = trace.m_vHitNormal[i] * backoff;
			newVelocity[i] = this->player->landingVelocity[i] - change;
		}

		f32 adjust = DotProduct(newVelocity, trace.m_vHitNormal);
		if (adjust < 0.0f)
		{
			newVelocity -= (trace.m_vHitNormal * adjust);
		}
		// Make sure the player is going down a ramp by checking if they actually will gain speed from the boost.
		if (newVelocity.Length2D() >= this->player->landingVelocity.Length2D())
		{
			this->player->currentMoveData->m_vecVelocity.x = newVelocity.x;
			this->player->currentMoveData->m_vecVelocity.y = newVelocity.y;
			this->player->landingVelocity.x = newVelocity.x;
			this->player->landingVelocity.y = newVelocity.y;
		}
	}
}

// 1:1 with CS2.
void BhopModeService::ClipVelocity(Vector &in, Vector &normal, Vector &out)
{
	f32 backoff = -((in.x * normal.x) + ((normal.z * in.z) + (in.y * normal.y))) * 1;
	backoff = fmaxf(backoff, 0.0) + 0.03125;

	out = normal * backoff + in;
}

bool BhopModeService::IsValidMovementTrace(trace_t &tr, bbox_t bounds, CTraceFilterPlayerMovementCS *filter)
{
	trace_t stuck;
	// Maybe we don't need this one.
	// if (tr.m_flFraction < FLT_EPSILON)
	//{
	//	return false;
	//}

	if (tr.m_bStartInSolid)
	{
		return false;
	}

	// We hit something but no valid plane data?
	if (tr.m_flFraction < 1.0f && fabs(tr.m_vHitNormal.x) < FLT_EPSILON && fabs(tr.m_vHitNormal.y) < FLT_EPSILON
		&& fabs(tr.m_vHitNormal.z) < FLT_EPSILON)
	{
		return false;
	}

	// Is the plane deformed?
	if (fabs(tr.m_vHitNormal.x) > 1.0f || fabs(tr.m_vHitNormal.y) > 1.0f || fabs(tr.m_vHitNormal.z) > 1.0f)
	{
		return false;
	}

	// Do an unswept trace and a backward trace just to be sure.
	INavPhysicsInterface::TraceShape(Ray_t(bounds.mins, bounds.maxs), tr.m_vEndPos, tr.m_vEndPos, filter, &stuck);
	if (stuck.m_bStartInSolid || stuck.m_flFraction < 1.0f - FLT_EPSILON)
	{
		return false;
	}

	INavPhysicsInterface::TraceShape(Ray_t(bounds.mins, bounds.maxs), tr.m_vEndPos, tr.m_vStartPos, filter, &stuck);
	// For whatever reason if you can hit something in only one direction and not the other way around.
	// Only happens since Call to Arms update, so this fraction check is commented out until it is fixed.
	if (stuck.m_bStartInSolid /*|| stuck.m_flFraction < 1.0f - FLT_EPSILON*/)
	{
		return false;
	}

	return true;
}

void BhopModeService::OnTryPlayerMove(Vector *pFirstDest, trace_t *pFirstTrace, bool *bIsBhoping)
{
	this->tpmTriggerFixOrigins.RemoveAll();
	this->overrideTPM = false;
	this->didTPM = true;
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();

	f32 timeLeft = g_pBhopUtils->GetGlobals()->frametime;

	Vector start, velocity, end;
	this->player->GetOrigin(&start);
	this->player->GetVelocity(&velocity);

	this->tpmTriggerFixOrigins.AddToTail(start);
	if (velocity.Length() == 0.0f)
	{
		// No move required.
		return;
	}
	Vector primalVelocity = velocity;

	bool validPlane {};

	f32 allFraction {};
	trace_t pm;
	u32 bumpCount {};
	Vector planes[5];
	u32 numPlanes {};
	trace_t pierce;

	bbox_t bounds;
	this->player->GetBBoxBounds(&bounds);

	CTraceFilterPlayerMovementCS filter(pawn);

	bool potentiallyStuck {};

	for (bumpCount = 0; bumpCount < MAX_BUMPS; bumpCount++)
	{
		// Assume we can move all the way from the current origin to the end point.
		VectorMA(start, timeLeft, velocity, end);
		// See if we can make it from origin to end point.
		// If their velocity Z is 0, then we can avoid an extra trace here during WalkMove.
		if (pFirstDest && end == *pFirstDest)
		{
			pm = *pFirstTrace;
		}
		else
		{
			INavPhysicsInterface::TraceShape(Ray_t(bounds.mins, bounds.maxs), start, end, &filter, &pm);
			if (end == start)
			{
				continue;
			}
			if (IsValidMovementTrace(pm, bounds, &filter) && pm.m_flFraction == 1.0f)
			{
				// Player won't hit anything, nothing to do.
				break;
			}
			if (this->lastValidPlane.Length() > FLT_EPSILON
				&& (!IsValidMovementTrace(pm, bounds, &filter) || pm.m_vHitNormal.Dot(this->lastValidPlane) < RAMP_BUG_THRESHOLD
					|| (potentiallyStuck && pm.m_flFraction == 0.0f)))
			{
				// We hit a plane that will significantly change our velocity. Make sure that this plane is significant
				// enough.
				Vector direction = g_pBhopUtils->NormalizeVector(velocity);
				Vector offsetDirection;
				f32 offsets[] = {0.0f, -1.0f, 1.0f};
				bool success {};
				for (u32 i = 0; i < 3 && !success; i++)
				{
					for (u32 j = 0; j < 3 && !success; j++)
					{
						for (u32 k = 0; k < 3 && !success; k++)
						{
							if (i == 0 && j == 0 && k == 0)
							{
								offsetDirection = this->lastValidPlane;
							}
							else
							{
								offsetDirection = {offsets[i], offsets[j], offsets[k]};
								// Check if this random offset is even valid.
								if (this->lastValidPlane.Dot(offsetDirection) <= 0.0f)
								{
									continue;
								}
								trace_t test;
								INavPhysicsInterface::TraceShape(Ray_t(bounds.mins, bounds.maxs), start + offsetDirection * RAMP_PIERCE_DISTANCE,
																 start, &filter, &test);
								if (!IsValidMovementTrace(test, bounds, &filter))
								{
									continue;
								}
							}
							bool goodTrace {};
							f32 ratio {};
							bool hitNewPlane {};
							for (ratio = 0.25f; ratio <= 1.0f; ratio += 0.25f)
							{
								INavPhysicsInterface::TraceShape(Ray_t(bounds.mins, bounds.maxs),
																 start + offsetDirection * RAMP_PIERCE_DISTANCE * ratio,
																 end + offsetDirection * RAMP_PIERCE_DISTANCE * ratio, &filter, &pierce);
								if (!IsValidMovementTrace(pierce, bounds, &filter))
								{
									continue;
								}
								// Try until we hit a similar plane.
								// clang-format off
								validPlane = pierce.m_flFraction < 1.0f && pierce.m_flFraction > 0.1f 
											 && pierce.m_vHitNormal.Dot(this->lastValidPlane) >= RAMP_BUG_THRESHOLD;

								hitNewPlane = pm.m_vHitNormal.Dot(pierce.m_vHitNormal) < NEW_RAMP_THRESHOLD 
											  && this->lastValidPlane.Dot(pierce.m_vHitNormal) > NEW_RAMP_THRESHOLD;
								// clang-format on
								goodTrace = CloseEnough(pierce.m_flFraction, 1.0f, FLT_EPSILON) || validPlane;
								if (goodTrace)
								{
									break;
								}
							}
							if (goodTrace || hitNewPlane)
							{
								// Trace back to the original end point to find its normal.
								trace_t test;
								INavPhysicsInterface::TraceShape(Ray_t(bounds.mins, bounds.maxs), pierce.m_vEndPos, end, &filter, &test);
								pm = pierce;
								pm.m_vStartPos = start;
								pm.m_flFraction = Clamp((pierce.m_vEndPos - pierce.m_vStartPos).Length() / (end - start).Length(), 0.0f, 1.0f);
								pm.m_vEndPos = test.m_vEndPos;
								if (pierce.m_vHitNormal.Length() > 0.0f)
								{
									pm.m_vHitNormal = pierce.m_vHitNormal;
									this->lastValidPlane = pierce.m_vHitNormal;
								}
								else
								{
									pm.m_vHitNormal = test.m_vHitNormal;
									this->lastValidPlane = test.m_vHitNormal;
								}
								success = true;
								this->overrideTPM = true;
							}
						}
					}
				}
			}
			if (pm.m_vHitNormal.Length() > 0.99f)
			{
				this->lastValidPlane = pm.m_vHitNormal;
			}
			potentiallyStuck = pm.m_flFraction == 0.0f;
		}

		if (pm.m_flFraction * velocity.Length() > 0.03125f || pm.m_flFraction > 0.03125f)
		{
			allFraction += pm.m_flFraction;
			start = pm.m_vEndPos;
			numPlanes = 0;
		}

		this->tpmTriggerFixOrigins.AddToTail(pm.m_vEndPos);

		if (allFraction == 1.0f)
		{
			break;
		}
		timeLeft -= g_pBhopUtils->GetGlobals()->frametime * pm.m_flFraction;

		// 2024-11-07 update also adds a low velocity check... This is only correct as long as you don't collide with other players.
		if (numPlanes >= 5 || (pm.m_vHitNormal.z >= 0.7f && velocity.Length2D() < 1.0f))
		{
			VectorCopy(vec3_origin, velocity);
			break;
		}

		planes[numPlanes] = pm.m_vHitNormal;
		numPlanes++;

		if (numPlanes == 1 && pawn->m_MoveType() == MOVETYPE_WALK && pawn->m_hGroundEntity().Get() == nullptr)
		{
			ClipVelocity(velocity, planes[0], velocity);
		}
		else
		{
			u32 i, j;
			for (i = 0; i < numPlanes; i++)
			{
				ClipVelocity(velocity, planes[i], velocity);
				for (j = 0; j < numPlanes; j++)
				{
					if (j != i)
					{
						// Are we now moving against this plane?
						if (velocity.Dot(planes[j]) < 0)
						{
							break; // not ok
						}
					}
				}

				if (j == numPlanes) // Didn't have to clip, so we're ok
				{
					break;
				}
			}
			// Did we go all the way through plane set
			if (i != numPlanes)
			{ // go along this plane
				// pmove.velocity is set in clipping call, no need to set again.
				;
			}
			else
			{ // go along the crease
				if (numPlanes != 2)
				{
					VectorCopy(vec3_origin, velocity);
					break;
				}
				Vector dir;
				f32 d;
				CrossProduct(planes[0], planes[1], dir);
				dir = g_pBhopUtils->NormalizeVector(dir);
				d = dir.Dot(velocity);
				VectorScale(dir, d, velocity);

				if (velocity.Dot(primalVelocity) <= 0)
				{
					velocity = vec3_origin;
					break;
				}
			}
		}
	}
	this->tpmOrigin = pm.m_vEndPos;
	this->tpmVelocity = velocity;
}

void BhopModeService::OnTryPlayerMovePost(Vector *pFirstDest, trace_t *pFirstTrace, bool *bIsBhoping)
{
	Vector velocity;
	this->player->GetVelocity(&velocity);
	bool velocityHeavilyModified =
		g_pBhopUtils->NormalizeVector(this->tpmVelocity).Dot(g_pBhopUtils->NormalizeVector(this->tpmVelocity)) < RAMP_BUG_THRESHOLD
		|| (this->tpmVelocity.Length() > 50.0f && velocity.Length() / this->tpmVelocity.Length() < RAMP_BUG_VELOCITY_THRESHOLD);
	if (this->overrideTPM && velocityHeavilyModified && this->tpmOrigin != vec3_invalid && this->tpmVelocity != vec3_invalid)
	{
		this->player->SetOrigin(this->tpmOrigin);
		this->player->SetVelocity(this->tpmVelocity);
	}
	if (this->airMoving)
	{
		if (this->tpmTriggerFixOrigins.Count() > 1)
		{
			bbox_t bounds;
			// We need to shrink the bounds a bit to prevent touching triggers that we shouldn't be touching when doing triggerfix.
			bbox_t offset = {{0.03125f, 0.03125f, 0.0f}, {-0.03125f, -0.03125f, 0.0f}};
			this->player->GetBBoxBounds(&bounds, &offset);
			for (int i = 0; i < this->tpmTriggerFixOrigins.Count() - 1; i++)
			{
				this->player->TouchTriggersAlongPath(this->tpmTriggerFixOrigins[i], this->tpmTriggerFixOrigins[i + 1], bounds);
			}
		}
		this->player->UpdateTriggerTouchList();
	}
}

void BhopModeService::OnCategorizePosition(bool bStayOnGround)
{
	// Already on the ground?
	// If we are already colliding on a standable valid plane, we don't want to do the check.
	if (bStayOnGround || this->lastValidPlane.Length() < EPSILON || this->lastValidPlane.z > 0.7f)
	{
		return;
	}
	// Only attempt to fix rampbugs while going down significantly enough.
	if (this->player->currentMoveData->m_vecVelocity.z > -64.0f)
	{
		return;
	}
	bbox_t bounds;
	this->player->GetBBoxBounds(&bounds);

	CTraceFilterPlayerMovementCS filter(this->player->GetPlayerPawn());

	trace_t trace;

	Vector origin, groundOrigin;
	this->player->GetOrigin(&origin);
	groundOrigin = origin;
	groundOrigin.z -= 2.0f;

	INavPhysicsInterface::TraceShape(Ray_t(bounds.mins, bounds.maxs), origin, groundOrigin, &filter, &trace);

	if (trace.m_flFraction == 1.0f)
	{
		return;
	}
	// Is this something that you should be able to actually stand on?
	if (trace.m_flFraction < 0.95f && trace.m_vHitNormal.z > 0.7f && this->lastValidPlane.Dot(trace.m_vHitNormal) < RAMP_BUG_THRESHOLD)
	{
		origin += this->lastValidPlane * 0.0625f;
		groundOrigin = origin;
		groundOrigin.z -= 2.0f;
		INavPhysicsInterface::TraceShape(Ray_t(bounds.mins, bounds.maxs), origin, groundOrigin, &filter, &trace);
		if (trace.m_bStartInSolid)
		{
			return;
		}
		if (trace.m_flFraction == 1.0f || this->lastValidPlane.Dot(trace.m_vHitNormal) >= RAMP_BUG_THRESHOLD)
		{
			this->player->SetOrigin(origin);
		}
	}
}

void BhopModeService::OnDuckPost()
{
	this->player->UpdateTriggerTouchList();
}

void BhopModeService::OnAirMove()
{
	this->airMoving = true;
	this->player->currentMoveData->m_flMaxSpeed = SPEED_NORMAL;
}

void BhopModeService::OnAirMovePost()
{
	this->airMoving = false;
	this->player->currentMoveData->m_flMaxSpeed = SPEED_NORMAL;
}

void BhopModeService::OnWaterMove()
{
	this->player->currentMoveData->m_flMaxSpeed = SPEED_NORMAL;
}

void BhopModeService::OnWaterMovePost()
{
	this->player->currentMoveData->m_flMaxSpeed = SPEED_NORMAL;
}

void BhopModeService::OnTeleport(const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity)
{
	if (!this->player->processingMovement)
	{
		return;
	}
	// Only happens when triggerfix happens.
	if (newPosition)
	{
		this->player->currentMoveData->m_vecAbsOrigin = *newPosition;
	}
	if (newVelocity)
	{
		this->player->currentMoveData->m_vecVelocity = *newVelocity;
	}
}

// Only touch timer triggers on tick.
bool BhopModeService::OnTriggerStartTouch(CBaseTrigger *trigger)
{
	if (!g_pMappingApi->IsTriggerATimerZone(trigger))
	{
		return true;
	}
	f64 tick = g_pBhopUtils->GetGlobals()->curtime * ENGINE_FIXED_TICK_RATE;
	if (fabs(roundf(tick) - tick) < 0.001f)
	{
		return true;
	}

	return false;
}

bool BhopModeService::OnTriggerTouch(CBaseTrigger *trigger)
{
	if (!g_pMappingApi->IsTriggerATimerZone(trigger))
	{
		return true;
	}
	f64 tick = g_pBhopUtils->GetGlobals()->curtime * ENGINE_FIXED_TICK_RATE;
	if (fabs(roundf(tick) - tick) < 0.001f)
	{
		return true;
	}
	return false;
}

bool BhopModeService::OnTriggerEndTouch(CBaseTrigger *trigger)
{
	if (!g_pMappingApi->IsTriggerATimerZone(trigger))
	{
		return true;
	}
	f64 tick = g_pBhopUtils->GetGlobals()->curtime * ENGINE_FIXED_TICK_RATE;
	if (fabs(roundf(tick) - tick) < 0.001f)
	{
		return true;
	}
	return false;
}
