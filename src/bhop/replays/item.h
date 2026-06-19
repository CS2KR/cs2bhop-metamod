#pragma once
#include "bhop_replay.h"

namespace Bhop::replaysystem::item
{
	void InitItemAttributes();
	std::string GetItemAttributeName(u16 id);
	std::string GetWeaponName(u16 id);
	gear_slot_t GetWeaponGearSlot(u16 id);
	bool DoesPaintKitUseLegacyModel(float paintKit);
	void ApplyItemAttributesToWeapon(CBasePlayerWeapon &weapon, const EconInfo &info);
	void ApplyModelAttributesToPawn(CCSPlayerPawn *pawn, const EconInfo &info, const char *modelName);
} // namespace Bhop::replaysystem::item
