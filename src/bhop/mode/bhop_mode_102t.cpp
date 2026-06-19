#include "bhop_mode_102t.h"

#define MODE_NAME_SHORT "102t"
#define MODE_NAME       "102tick"

Bhop102tModePlugin g_Bhop102tModePlugin;

CGameConfig *g_pGameConfig = NULL;
BhopUtils *g_pBhopUtils = NULL;
BhopModeManager *g_pModeManager = NULL;
MappingInterface *g_pMappingApi = NULL;
ModeServiceFactory g_ModeFactory = [](BhopPlayer *player) -> BhopModeService * { return new Bhop102tModeService(player); };
PLUGIN_EXPOSE(Bhop102tModePlugin, g_Bhop102tModePlugin);

bool Bhop102tModePlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();
	// Load mode
	int success;
	g_pModeManager = (BhopModeManager *)g_SMAPI->MetaFactory(BHOP_MODE_MANAGER_INTERFACE, &success, 0);
	if (success == META_IFACE_FAILED)
	{
		V_snprintf(error, maxlen, "Failed to find %s interface", BHOP_MODE_MANAGER_INTERFACE);
		return false;
	}
	g_pBhopUtils = (BhopUtils *)g_SMAPI->MetaFactory(BHOP_UTILS_INTERFACE, &success, 0);
	if (success == META_IFACE_FAILED)
	{
		V_snprintf(error, maxlen, "Failed to find %s interface", BHOP_UTILS_INTERFACE);
		return false;
	}
	g_pMappingApi = (MappingInterface *)g_SMAPI->MetaFactory(BHOP_MAPPING_INTERFACE, &success, 0);
	if (success == META_IFACE_FAILED)
	{
		V_snprintf(error, maxlen, "Failed to find %s interface", BHOP_MAPPING_INTERFACE);
		return false;
	}
	modules::Initialize();
	if (!interfaces::Initialize(ismm, error, maxlen))
	{
		V_snprintf(error, maxlen, "Failed to initialize interfaces");
		return false;
	}

	if (nullptr == (g_pGameConfig = g_pBhopUtils->GetGameConfig()))
	{
		V_snprintf(error, maxlen, "Failed to get game config");
		return false;
	}

	if (!g_pModeManager->RegisterMode(g_PLID, MODE_NAME_SHORT, MODE_NAME, g_ModeFactory))
	{
		V_snprintf(error, maxlen, "Failed to register mode");
		return false;
	}

	ConVar_Register();
	return true;
}

bool Bhop102tModePlugin::Unload(char *error, size_t maxlen)
{
	g_pModeManager->UnregisterMode(g_PLID);
	return true;
}

bool Bhop102tModePlugin::Pause(char *error, size_t maxlen)
{
	g_pModeManager->UnregisterMode(g_PLID);
	return true;
}

bool Bhop102tModePlugin::Unpause(char *error, size_t maxlen)
{
	if (!g_pModeManager->RegisterMode(g_PLID, MODE_NAME_SHORT, MODE_NAME, g_ModeFactory))
	{
		return false;
	}
	return true;
}

CGameEntitySystem *GameEntitySystem()
{
	return g_pBhopUtils->GetGameEntitySystem();
}

void Bhop102tModeService::Reset()
{
	this->hasValidDesiredViewAngle = {};
	this->lastValidDesiredViewAngle = vec3_angle;
	this->lastJumpReleaseTime = {};
	this->oldDuckPressed = {};
	this->forcedUnduck = {};
	this->postProcessMovementZSpeed = {};

	this->angleHistory.RemoveAll();
	this->leftPreRatio = {};
	this->rightPreRatio = {};
	this->bonusSpeed = {};
	this->maxPre = {};

	this->didTPM = {};
	this->overrideTPM = {};
	this->tpmVelocity = vec3_origin;
	this->tpmOrigin = vec3_origin;
	this->lastValidPlane = vec3_origin;

	this->airMoving = {};
	this->tpmTriggerFixOrigins.RemoveAll();
}

void Bhop102tModeService::Cleanup()
{
	auto pawn = this->player->GetPlayerPawn();
	if (pawn)
	{
		pawn->m_flVelocityModifier(1.0f);
	}
}

const char *Bhop102tModeService::GetModeName()
{
	return MODE_NAME;
}

const char *Bhop102tModeService::GetModeShortName()
{
	return MODE_NAME_SHORT;
}

const CVValue_t *Bhop102tModeService::GetModeConVarValues()
{
	return modeCvarValues;
}

void Bhop102tModeService::OnSetupMove(PlayerCommand *pc)
{
	u32 tickCount = g_pBhopUtils->GetServerGlobals()->tickcount;

	// 39 ticks with 128t timing, 25 with 64t timing, which makes 102.5tick-ish
	bool doubleTickrate = ((tickCount * 39) % 64) < 39;

	for (i32 j = 0; j < pc->mutable_base()->subtick_moves_size(); j++)
	{
		CSubtickMoveStep *subtickMove = pc->mutable_base()->mutable_subtick_moves(j);
		if (subtickMove->button() == IN_ATTACK || subtickMove->button() == IN_ATTACK2 || subtickMove->button() == IN_RELOAD)
		{
			continue;
		}
		float when = subtickMove->when();
		if (subtickMove->button() == IN_JUMP)
		{
			f32 inputTime = (tickCount + when - 1) * ENGINE_FIXED_TICK_INTERVAL;
			if (when != 0)
			{
				if (subtickMove->pressed() && inputTime - this->lastJumpReleaseTime > 0.5 * ENGINE_FIXED_TICK_INTERVAL)
				{
					this->player->GetMoveServices()->m_LegacyJump().m_bOldJumpPressed = false;
				}
				if (!subtickMove->pressed())
				{
					this->lastJumpReleaseTime = (tickCount + when - 1) * ENGINE_FIXED_TICK_INTERVAL;
				}
			}
		}
		if (doubleTickrate)
		{
			subtickMove->set_when(when >= 0.5 ? 0.5f : 0.0f);
		}
		else
		{
			subtickMove->set_when(0.0f);
		}
	}
}

void Bhop102tModeService::OnPhysicsSimulate()
{
	CCSPlayer_MovementServices *moveServices = this->player->GetMoveServices();
	if (!moveServices)
	{
		return;
	}
	u32 tickCount = g_pBhopUtils->GetServerGlobals()->tickcount;
	bool doubleTickrate = ((tickCount * 39) % 64) < 39;
	if (!doubleTickrate)
	{
		return;
	}
	f32 subtickMoveTime = (tickCount - 0.5) * ENGINE_FIXED_TICK_INTERVAL;
	for (u32 i = 0; i < 4; i++)
	{
		if (fabs(subtickMoveTime - moveServices->m_arrForceSubtickMoveWhen[i]) < 0.001)
		{
			return;
		}
		if (subtickMoveTime > moveServices->m_arrForceSubtickMoveWhen[i])
		{
			moveServices->SetForcedSubtickMove(i, subtickMoveTime, false);
			return;
		}
	}
}

void Bhop102tModeService::OnPhysicsSimulatePost()
{
	CCSPlayer_MovementServices *moveServices = this->player->GetMoveServices();
	if (!moveServices)
	{
		return;
	}
	u32 tickCount = g_pBhopUtils->GetServerGlobals()->tickcount;
	bool doubleTickrate = ((tickCount * 39) % 64) < 39;
	if (!doubleTickrate)
	{
		return;
	}
	f32 subtickMoveTime = (tickCount + 0.5) * ENGINE_FIXED_TICK_INTERVAL;
	for (u32 i = 0; i < 4; i++)
	{
		if (fabs(subtickMoveTime - moveServices->m_arrForceSubtickMoveWhen[i]) < 0.001)
		{
			subtickMoveTime += ENGINE_FIXED_TICK_INTERVAL;
			continue;
		}
		if (subtickMoveTime > moveServices->m_arrForceSubtickMoveWhen[i])
		{
			moveServices->SetForcedSubtickMove(i, subtickMoveTime);
			subtickMoveTime += ENGINE_FIXED_TICK_INTERVAL;
		}
	}
}
