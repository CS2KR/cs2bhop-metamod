/*
	Keeps track of course descriptors along with various types of triggers, applying effects to player when necessary.
*/

#include <string>
#include <regex>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <vector>

#include "bhop/bhop.h"
#include "bhop/mode/bhop_mode.h"
#include "bhop/trigger/bhop_trigger.h"
#include "bhop/beam/bhop_zone_beam.h"
#include "movement/movement.h"
#include "bhop_mappingapi.h"
#include "entity2/entitykeyvalues.h"
#include "sdk/entity/cbasetrigger.h"
#include "utils/ctimer.h"
#include "bhop/db/bhop_db.h"
#include "bhop/language/bhop_language.h"
#include "utils/simplecmds.h"
#include "utils/utils.h"
#include "UtlSortVector.h"
#include "vendor/json/single_include/nlohmann/json.hpp"

#include "tier0/memdbgon.h"

#define KEY_TRIGGER_TYPE         "timer_trigger_type"
#define KEY_IS_COURSE_DESCRIPTOR "timer_course_descriptor"

using namespace Bhop::course;

class CourseLessFunc
{
public:
	bool Less(const BhopCourseDescriptor *src1, const BhopCourseDescriptor *src2, void *pCtx)
	{
		return src1->id < src2->id;
	}
};

enum
{
	MAPI_ERR_TOO_MANY_TRIGGERS = 1 << 0,
	MAPI_ERR_TOO_MANY_COURSES = 1 << 1,
};

static_global struct
{
	CUtlVectorFixed<BhopCourseDescriptor, BHOP_MAX_COURSE_COUNT> courseDescriptors;
	i32 mapApiVersion;
	bool apiVersionLoaded;
	bool fatalFailure;

	CUtlVectorFixed<BhopTrigger, 2048> triggers;
	CUtlVector<CUtlString> fakeStartTriggerNames;
	CUtlVector<CUtlString> fakeEndTriggerNames;
	CUtlVector<CUtlString> detectedFakeStartTriggerNames;
	CUtlVector<CUtlString> detectedFakeEndTriggerNames;
	bool roundIsStarting;
	i32 errorFlags;
	i32 errorCount;
	char errors[32][256];

	bool hasJumpstatArea;
	Vector jumpstatAreaPos;
	QAngle jumpstatAreaAngles;
} g_mappingApi;

static_global u32 g_mapCfgMaxVelocity = 0;
static_global CTimer<> *g_errorTimer;
static_global const char *g_errorPrefix = "{darkred} ERROR: ";
static_global const char *g_triggerNames[] = {"Disabled",   "Modifier",         "Start zone",     "End zone", "Checkpoint zone",
											  "Stage zone", "Bonus start zone", "Bonus end zone", "Teleport", "Destination",
											  "Push",       "Action stop",      "Action reset"};

static_function MappingInterface g_mappingInterface;

MappingInterface *g_pMappingApi = &g_mappingInterface;
static_global CUtlSortVector<BhopCourseDescriptor *, CourseLessFunc> g_sortedCourses(BHOP_MAX_COURSE_COUNT, BHOP_MAX_COURSE_COUNT);

// TODO: add error check to make sure a course has at least 1 start zone and 1 end zone

static_function void Mapi_Error(const char *format, ...)
{
	i32 errorIndex = g_mappingApi.errorCount;
	if (errorIndex >= BHOP_ARRAYSIZE(g_mappingApi.errors))
	{
		return;
	}
	else if (errorIndex == BHOP_ARRAYSIZE(g_mappingApi.errors) - 1)
	{
		snprintf(g_mappingApi.errors[errorIndex], sizeof(g_mappingApi.errors[errorIndex]), "Too many errors to list!");
		return;
	}

	va_list args;
	va_start(args, format);
	vsnprintf(g_mappingApi.errors[errorIndex], sizeof(g_mappingApi.errors[errorIndex]), format, args);
	va_end(args);

	g_mappingApi.errorCount++;
}

static_function bool Mapi_HasConfiguredTriggerName(const CUtlVector<CUtlString> &names, const std::string &triggerName)
{
	FOR_EACH_VEC(names, i)
	{
		if (names[i].IsEqual_FastCaseInsensitive(triggerName.c_str()))
		{
			return true;
		}
	}
	return false;
}

static_function std::string Mapi_NormalizeTriggerName(const char *name)
{
	std::string normalized(name ? name : "");
	std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) { return std::tolower(c); });
	return normalized;
}

static_function void Mapi_AddUniqueTriggerName(CUtlVector<CUtlString> &names, const char *name)
{
	if (!name || !name[0] || Mapi_HasConfiguredTriggerName(names, name))
	{
		return;
	}
	names.AddToTail(name);
}

static_function void Mapi_AddConfiguredTriggerName(CUtlVector<CUtlString> &names, const nlohmann::json &json, const char *key)
{
	if (!json.contains(key))
	{
		return;
	}
	const nlohmann::json &value = json[key];
	if (value.is_string())
	{
		std::string name = value.get<std::string>();
		Mapi_AddUniqueTriggerName(names, name.c_str());
		return;
	}
	if (!value.is_array())
	{
		return;
	}
	for (const nlohmann::json &entry : value)
	{
		if (!entry.is_string())
		{
			continue;
		}
		std::string name = entry.get<std::string>();
		Mapi_AddUniqueTriggerName(names, name.c_str());
	}
}

static_function void Mapi_AddZoneExportKeys(nlohmann::json &json, const char *singleKey, const char *arrayKey, const CUtlVector<CUtlString> &names)
{
	if (names.Count() <= 0)
	{
		return;
	}

	json[singleKey] = names[0].Get();

	if (names.Count() <= 1)
	{
		return;
	}

	json[arrayKey] = nlohmann::json::array();
	FOR_EACH_VEC(names, i)
	{
		json[arrayKey].push_back(names[i].Get());
	}
}

static_function void Mapi_LoadFakeZoneTriggerAliases(const char *path)
{
	std::ifstream file(path);
	if (!file)
	{
		return;
	}

	std::stringstream buffer;
	buffer << file.rdbuf();
	auto json = nlohmann::json::parse(buffer.str(), nullptr, false);
	if (json.is_discarded() || !json.is_object())
	{
		META_CONPRINTF("[Bhop::MapAPI] Failed to parse fake-zone trigger config: %s\n", path);
		return;
	}

	Mapi_AddConfiguredTriggerName(g_mappingApi.fakeStartTriggerNames, json, "MapStartTrigger");
	Mapi_AddConfiguredTriggerName(g_mappingApi.fakeEndTriggerNames, json, "MapEndTrigger");
	Mapi_AddConfiguredTriggerName(g_mappingApi.fakeStartTriggerNames, json, "MapStartTriggers");
	Mapi_AddConfiguredTriggerName(g_mappingApi.fakeEndTriggerNames, json, "MapEndTriggers");
	Mapi_AddConfiguredTriggerName(g_mappingApi.fakeStartTriggerNames, json, "startTrigger");
	Mapi_AddConfiguredTriggerName(g_mappingApi.fakeEndTriggerNames, json, "endTrigger");
	Mapi_AddConfiguredTriggerName(g_mappingApi.fakeStartTriggerNames, json, "startTriggers");
	Mapi_AddConfiguredTriggerName(g_mappingApi.fakeEndTriggerNames, json, "endTriggers");
}

enum class FakeZoneExportResult
{
	Success,
	NoMapName,
	OfficialMappingApi,
	NoDetectedPair,
	WriteFailed,
};

static_function FakeZoneExportResult Mapi_ExportFakeZoneConfig(char *relativePath, size_t relativePathSize)
{
	bool hasMapName = false;
	CUtlString currentMap = g_pBhopUtils->GetCurrentMapName(&hasMapName);
	if (!hasMapName)
	{
		return FakeZoneExportResult::NoMapName;
	}
	if (g_mappingApi.mapApiVersion != BHOP_NO_MAPAPI_VERSION)
	{
		return FakeZoneExportResult::OfficialMappingApi;
	}

	if (g_mappingApi.detectedFakeStartTriggerNames.Count() <= 0 || g_mappingApi.detectedFakeEndTriggerNames.Count() <= 0)
	{
		return FakeZoneExportResult::NoDetectedPair;
	}

	nlohmann::json json;
	json["schema"] = "cs2bhop.fake-zones";
	json["schemaVersion"] = 1;
	json["map"] = currentMap.Get();
	json["source"] = "trigger_multiple.targetname";
	json["compatibility"] = {{"sharpTimerTriggerAliases", true}, {"coordinateBoxes", false}, {"officialMappingApi", false}};
	Mapi_AddZoneExportKeys(json, "MapStartTrigger", "MapStartTriggers", g_mappingApi.detectedFakeStartTriggerNames);
	Mapi_AddZoneExportKeys(json, "MapEndTrigger", "MapEndTriggers", g_mappingApi.detectedFakeEndTriggerNames);

	char path[1024];
	g_SMAPI->PathFormat(path, sizeof(path), "addons/cs2bhop/zones/%s.json", currentMap.Get());
	std::string output = json.dump(2);
	output.push_back('\n');
	std::vector<char> buffer(output.begin(), output.end());

	if (relativePath && relativePathSize > 0)
	{
		V_snprintf(relativePath, relativePathSize, "%s", path);
	}
	return utils::WriteBufferToFile(path, buffer) ? FakeZoneExportResult::Success : FakeZoneExportResult::WriteFailed;
}

static_function void Mapi_LoadFakeZoneConfig()
{
	g_mappingApi.fakeStartTriggerNames.RemoveAll();
	g_mappingApi.fakeEndTriggerNames.RemoveAll();

	bool hasMapName = false;
	CUtlString currentMap = g_pBhopUtils->GetCurrentMapName(&hasMapName);
	if (!hasMapName)
	{
		return;
	}

	char cs2bhopPath[1024];
	g_SMAPI->PathFormat(cs2bhopPath, sizeof(cs2bhopPath), "%s/addons/cs2bhop/zones/%s.json", g_SMAPI->GetBaseDir(), currentMap.Get());
	Mapi_LoadFakeZoneTriggerAliases(cs2bhopPath);

	char sharpTimerPath[1024];
	g_SMAPI->PathFormat(sharpTimerPath, sizeof(sharpTimerPath), "%s/cfg/SharpTimer/MapData/%s.json", g_SMAPI->GetBaseDir(), currentMap.Get());
	Mapi_LoadFakeZoneTriggerAliases(sharpTimerPath);
}

static_function bool Mapi_IsFakeStartZoneName(const std::string &name)
{
	if (name == "map_start" || name == "s1_start" || name == "stage1_start" || name == "timer_startzone" || name == "trigger_startzone"
		|| name == "zone_start")
	{
		return true;
	}
	if (Mapi_HasConfiguredTriggerName(g_mappingApi.fakeStartTriggerNames, name))
	{
		return true;
	}
	static const std::regex stage1Pattern(R"(^(?:s|stage)1_start$)", std::regex_constants::icase);
	return std::regex_match(name, stage1Pattern);
}

static_function bool Mapi_IsFakeEndZoneName(const std::string &name)
{
	if (name == "map_end" || name == "timer_endzone" || name == "trigger_endzone" || name == "zone_end")
	{
		return true;
	}
	return Mapi_HasConfiguredTriggerName(g_mappingApi.fakeEndTriggerNames, name);
}

static_function bool Mapi_IsFakeStopTriggerName(const std::string &name)
{
	return name == "st_stop" || name == "surftimer_stop" || name == "timer_stop";
}

static_function bool Mapi_IsFakeResetTriggerName(const std::string &name)
{
	return name == "st_reset" || name == "surftimer_reset" || name == "timer_reset";
}

static_function f64 Mapi_PrintErrors()
{
	if (g_mappingApi.errorFlags & MAPI_ERR_TOO_MANY_TRIGGERS)
	{
		utils::CPrintChatAll("%sToo many Mapping API triggers! Maximum is %i!", g_errorPrefix, g_mappingApi.triggers.Count());
	}
	if (g_mappingApi.errorFlags & MAPI_ERR_TOO_MANY_COURSES)
	{
		utils::CPrintChatAll("%sToo many Courses! Maximum is %i!", g_errorPrefix, g_mappingApi.courseDescriptors.Count());
	}
	for (i32 i = 0; i < g_mappingApi.errorCount; i++)
	{
		utils::CPrintChatAll("%s%s", g_errorPrefix, g_mappingApi.errors[i]);
	}

	return 60.0;
}

static_function bool Mapi_CreateCourse(i32 courseNumber = 1, const char *courseName = BHOP_NO_MAPAPI_COURSE_NAME, i32 hammerId = -1,
									   const char *targetName = BHOP_NO_MAPAPI_COURSE_DESCRIPTOR, u32 courseMaxVelocity = INVALID_MAXVEL_NUMBER)
{
	// Make sure we don't exceed this ridiculous value.
	// If we do, it is most likely that something went wrong, or it is caused by the mapper.
	if (g_mappingApi.courseDescriptors.Count() >= BHOP_MAX_COURSE_COUNT)
	{
		assert(0);
		Mapi_Error("Failed to register course name '%s' (hammerId %i): Too many courses!", courseName, hammerId);
		return false;
	}

	auto &currentCourses = g_mappingApi.courseDescriptors;
	FOR_EACH_VEC(currentCourses, i)
	{
		if (currentCourses[i].hammerId == hammerId)
		{
			// This should only happen during start/end zone backwards compat where hammer IDs are BHOP_NO_MAPAPI_VERSION, so this is not an error.
			return false;
		}
		if (BHOP_STREQI(targetName, currentCourses[i].entityTargetname))
		{
			Mapi_Error("Course descriptor '%s' already existed! (registered by Hammer ID %i)", targetName, currentCourses[i].hammerId);
			return false;
		}
	}
	u32 guid = (u32)g_mappingApi.courseDescriptors.Count() + 1;

	i32 index = g_mappingApi.courseDescriptors.AddToTail({hammerId, targetName, guid, courseNumber, courseName, courseMaxVelocity});
	g_sortedCourses.Insert(&g_mappingApi.courseDescriptors[index]);
	return true;
}

static_function bool Mapi_EnsureFakeBonusCourse(i32 bonusNumber, char *bonusDescriptor, size_t bonusDescriptorSize)
{
	if (bonusNumber <= 0)
	{
		return false;
	}

	V_snprintf(bonusDescriptor, bonusDescriptorSize, "B%d", bonusNumber);
	FOR_EACH_VEC(g_mappingApi.courseDescriptors, i)
	{
		if (BHOP_STREQI(g_mappingApi.courseDescriptors[i].entityTargetname, bonusDescriptor))
		{
			return true;
		}
	}

	char bonusName[128];
	V_snprintf(bonusName, sizeof(bonusName), "B%d", bonusNumber);
	return Mapi_CreateCourse(bonusNumber + 1, bonusName, -200000 - bonusNumber, bonusDescriptor, g_mapCfgMaxVelocity);
}

// Example keyvalues:
/*
	timer_anti_bhop_time: 0.2
	timer_teleport_relative: true
	timer_teleport_reorient_player: false
	timer_teleport_reset_speed: false
	timer_teleport_use_dest_angles: false
	timer_teleport_delay: 0
	timer_teleport_destination: landmark_teleport
	timer_zone_stage_number: 1
	timer_modifier_enable_slide: false
	timer_modifier_disable_jumpstats: false
	timer_modifier_disable_teleports: false
	timer_modifier_disable_checkpoints: false
	timer_modifier_disable_pause: false
	timer_trigger_type: 10
	wait: 1
	spawnflags: 4097
	StartDisabled: false
	useLocalOffset: false
	classname: trigger_multiple
	origin: 1792.000000 768.000000 -416.000000
	angles: 0.000000 0.000000 0.000000
	scales: 1.000000 1.000000 1.000000
	hammerUniqueId: 48
	model: maps\bhop_mapping_api\entities\unnamed_48.vmdl
*/
static_function void Mapi_OnTriggerMultipleSpawn(const EntitySpawnInfo_t *info)
{
	const CEntityKeyValues *ekv = info->m_pKeyValues;
	i32 hammerId = ekv->GetInt("hammerUniqueId", -1);
	Vector origin = ekv->GetVector("origin");

	BhopTriggerType type = (BhopTriggerType)ekv->GetInt(KEY_TRIGGER_TYPE, BHOPTRIGGER_DISABLED);

	if (!g_mappingApi.roundIsStarting)
	{
		// Only allow triggers and zones that were spawned during the round start phase.
		return;
	}

	if (type < BHOPTRIGGER_DISABLED || type >= BHOPTRIGGER_COUNT)
	{
		assert(0);
		Mapi_Error("Trigger type %i is invalid and out of range (%i-%i) for trigger with Hammer ID %i, origin (%.0f %.0f %.0f)!", type,
				   BHOPTRIGGER_DISABLED, BHOPTRIGGER_COUNT - 1, hammerId, origin.x, origin.y, origin.z);
		return;
	}

	BhopTrigger trigger = {};
	trigger.type = type;
	trigger.hammerId = hammerId;
	trigger.entity = info->m_pEntity->GetRefEHandle();

	switch (type)
	{
		case BHOPTRIGGER_MODIFIER:
		{
			trigger.modifier.gravity = ekv->GetFloat("timer_modifier_gravity", 1);
			trigger.modifier.jumpFactor = ekv->GetFloat("timer_modifier_jump_impulse", 1.0f);
		}
		break;
		case BHOPTRIGGER_ZONE_START:
		case BHOPTRIGGER_ZONE_END:
		case BHOPTRIGGER_ZONE_BONUS_START:
		case BHOPTRIGGER_ZONE_BONUS_END:
		case BHOPTRIGGER_ZONE_CHECKPOINT:
		case BHOPTRIGGER_ZONE_STAGE:
		{
			const char *courseDescriptor = ekv->GetString("timer_zone_course_descriptor");

			if (!courseDescriptor || !courseDescriptor[0])
			{
				Mapi_Error("Course descriptor targetname of %s trigger is empty! Hammer ID %i, origin (%.0f %.0f %.0f)", g_triggerNames[type],
						   hammerId, origin.x, origin.y, origin.z);
				assert(0);
				return;
			}

			snprintf(trigger.zone.courseDescriptor, sizeof(trigger.zone.courseDescriptor), "%s", courseDescriptor);
			if (type == BHOPTRIGGER_ZONE_CHECKPOINT)
			{
				trigger.zone.number = ekv->GetInt("timer_zone_checkpoint_number", INVALID_CHECKPOINT_NUMBER);

				if (trigger.zone.number <= INVALID_CHECKPOINT_NUMBER)
				{
					Mapi_Error("Checkpoint zone number \"%i\" is invalid! Hammer ID %i, origin (%.0f %.0f %.0f)", trigger.zone.number, hammerId,
							   origin.x, origin.y, origin.z);
					assert(0);
					return;
				}
			}
			else if (type == BHOPTRIGGER_ZONE_STAGE)
			{
				trigger.zone.number = ekv->GetInt("timer_zone_stage_number", INVALID_STAGE_NUMBER);

				if (trigger.zone.number <= INVALID_STAGE_NUMBER)
				{
					Mapi_Error("Stage zone number \"%i\" is invalid! Hammer ID %i, origin (%.0f %.0f %.0f)", trigger.zone.number, hammerId, origin.x,
							   origin.y, origin.z);
					assert(0);
					return;
				}
			}
			else // Start/End zones
			{
				// Note: Triggers shouldn't be rotated most of the time anyway. If that ever happens for timer triggers, it's probably unintentional.
				QAngle angles = ekv->GetQAngle("angles");

				if (angles != vec3_angle)
				{
					Mapi_Error(
						"Warning: Unexpected rotation for timer trigger, some functionalities might not work properly! Hammer ID %i, origin (%.0f "
						"%.0f %.0f)",
						hammerId, origin.x, origin.y, origin.z);
				}
			}
		}
		break;
		case BHOPTRIGGER_PUSH:
		{
			Vector impulse = ekv->GetVector("timer_push_amount");
			trigger.push.impulse[0] = impulse.x;
			trigger.push.impulse[1] = impulse.y;
			trigger.push.impulse[2] = impulse.z;
			trigger.push.setSpeed[0] = ekv->GetBool("timer_push_abs_speed_x");
			trigger.push.setSpeed[1] = ekv->GetBool("timer_push_abs_speed_y");
			trigger.push.setSpeed[2] = ekv->GetBool("timer_push_abs_speed_z");
			trigger.push.cancelOnTeleport = ekv->GetBool("timer_push_cancel_on_teleport");
			trigger.push.cooldown = ekv->GetFloat("timer_push_cooldown", 0.1f);
			trigger.push.delay = ekv->GetFloat("timer_push_delay", 0.0f);

			trigger.push.pushConditions |= ekv->GetBool("timer_push_condition_start_touch") ? BhopMapPush::BHOP_PUSH_START_TOUCH : 0;
			trigger.push.pushConditions |= ekv->GetBool("timer_push_condition_touch") ? BhopMapPush::BHOP_PUSH_TOUCH : 0;
			trigger.push.pushConditions |= ekv->GetBool("timer_push_condition_end_touch") ? BhopMapPush::BHOP_PUSH_END_TOUCH : 0;
			trigger.push.pushConditions |= ekv->GetBool("timer_push_condition_jump_event") ? BhopMapPush::BHOP_PUSH_JUMP_EVENT : 0;
			trigger.push.pushConditions |= ekv->GetBool("timer_push_condition_jump_button") ? BhopMapPush::BHOP_PUSH_JUMP_BUTTON : 0;
			trigger.push.pushConditions |= ekv->GetBool("timer_push_condition_attack") ? BhopMapPush::BHOP_PUSH_ATTACK : 0;
			trigger.push.pushConditions |= ekv->GetBool("timer_push_condition_attack2") ? BhopMapPush::BHOP_PUSH_ATTACK2 : 0;
			trigger.push.pushConditions |= ekv->GetBool("timer_push_condition_use") ? BhopMapPush::BHOP_PUSH_USE : 0;
		}
		break;
		case BHOPTRIGGER_DISABLED:
		{
			// Check for pre-mapping api triggers for backwards compatibility.
			if (g_mappingApi.mapApiVersion == BHOP_NO_MAPAPI_VERSION)
			{
				std::string name = Mapi_NormalizeTriggerName(ekv->GetString("targetname", ""));
				if (name.empty())
				{
					CUtlString triggerName = info->m_pEntity->m_name.String();
					name = Mapi_NormalizeTriggerName(triggerName.Get());
				}

				// START/END HOOKS
				if (Mapi_IsFakeStartZoneName(name) || Mapi_IsFakeEndZoneName(name))
				{
					snprintf(trigger.zone.courseDescriptor, sizeof(trigger.zone.courseDescriptor), BHOP_NO_MAPAPI_COURSE_DESCRIPTOR);
					if (Mapi_IsFakeStartZoneName(name))
					{
						trigger.type = BHOPTRIGGER_ZONE_START;
						Mapi_AddUniqueTriggerName(g_mappingApi.detectedFakeStartTriggerNames, name.c_str());
					}
					else
					{
						trigger.type = BHOPTRIGGER_ZONE_END;
						Mapi_AddUniqueTriggerName(g_mappingApi.detectedFakeEndTriggerNames, name.c_str());
					}
				}
				else if (Mapi_IsFakeStopTriggerName(name))
				{
					trigger.type = BHOPTRIGGER_ACTION_STOP;
				}
				else if (Mapi_IsFakeResetTriggerName(name))
				{
					trigger.type = BHOPTRIGGER_ACTION_RESET;
				}

				// STAGE HOOK
				std::smatch match;
				if (trigger.type == BHOPTRIGGER_DISABLED)
				{
					static const std::regex stagePattern(R"(^(?:s|stage)([1-9][0-9]?)_start$)");
					int stageNum = 0;

					if (std::regex_search(name, match, stagePattern))
					{
						stageNum = std::stoi(match.str(1));
					}

					if (stageNum != 0)
					{
						snprintf(trigger.zone.courseDescriptor, sizeof(trigger.zone.courseDescriptor), BHOP_NO_MAPAPI_COURSE_DESCRIPTOR);

						if (stageNum == 1)
						{
							trigger.type = BHOPTRIGGER_ZONE_START;
							trigger.zone.number = 1;
						}
						else
						{
							trigger.type = BHOPTRIGGER_ZONE_STAGE;
							trigger.zone.number = stageNum;
						}
					}
				}

				// CHECKPOINT HOOK
				if (trigger.type == BHOPTRIGGER_DISABLED)
				{
					static const std::regex cpPattern(R"(^(?:map_cp|map_checkpoint)([1-9][0-9]?)$)");
					int cpNum = 0;

					if (std::regex_search(name, match, cpPattern))
					{
						cpNum = std::stoi(match.str(1));
						snprintf(trigger.zone.courseDescriptor, sizeof(trigger.zone.courseDescriptor), BHOP_NO_MAPAPI_COURSE_DESCRIPTOR);
						trigger.type = BHOPTRIGGER_ZONE_CHECKPOINT;
						trigger.zone.number = cpNum;
					}
				}

				// BONUS HOOK
				if (trigger.type == BHOPTRIGGER_DISABLED)
				{
					char bonusDescriptor[128] {};
					bool isBonusStart = false;
					bool isBonusEnd = false;
					bool isBonusCheckpoint = false;

					static const std::regex bStartPattern(R"(^(?:b|bonus)([1-9][0-9]?)_start$|^timer_bonus([1-9][0-9]?)_startzone$)");
					static const std::regex bEndPattern(R"(^(?:b|bonus)([1-9][0-9]?)_end$|^timer_bonus([1-9][0-9]?)_endzone$)");
					static const std::regex bCpPattern(R"(^bonus_cp([1-9][0-9]?)$|^bonus_checkpoint([1-9][0-9]?)$)");
					int bonusNum = 0;
					int bonusCheckpointNum = 0;

					if (std::regex_search(name, match, bStartPattern))
					{
						std::string bonusText = match.str(1).empty() ? match.str(2) : match.str(1);
						bonusNum = std::stoi(bonusText);
						Mapi_EnsureFakeBonusCourse(bonusNum, bonusDescriptor, sizeof(bonusDescriptor));

						isBonusStart = true;
					}

					if (std::regex_search(name, match, bEndPattern))
					{
						std::string bonusText = match.str(1).empty() ? match.str(2) : match.str(1);
						bonusNum = std::stoi(bonusText);
						Mapi_EnsureFakeBonusCourse(bonusNum, bonusDescriptor, sizeof(bonusDescriptor));

						isBonusEnd = true;
					}

					if (std::regex_search(name, match, bCpPattern))
					{
						std::string cpText = match.str(1).empty() ? match.str(2) : match.str(1);
						bonusNum = 1;
						bonusCheckpointNum = std::stoi(cpText);
						Mapi_EnsureFakeBonusCourse(bonusNum, bonusDescriptor, sizeof(bonusDescriptor));

						isBonusCheckpoint = true;
					}

					if (isBonusStart || isBonusEnd)
					{
						snprintf(trigger.zone.courseDescriptor, sizeof(trigger.zone.courseDescriptor), "%s", bonusDescriptor);
						trigger.type = isBonusStart ? BHOPTRIGGER_ZONE_BONUS_START : BHOPTRIGGER_ZONE_BONUS_END;
						trigger.zone.bonus = bonusNum;
					}

					if (isBonusCheckpoint)
					{
						snprintf(trigger.zone.courseDescriptor, sizeof(trigger.zone.courseDescriptor), "%s", bonusDescriptor);
						trigger.type = BHOPTRIGGER_ZONE_CHECKPOINT;
						trigger.zone.bonus = bonusNum;
						trigger.zone.number = bonusCheckpointNum;
					}
				}
			}
			break;
			// Otherwise these are just regular trigger_multiple.
		}
		default:
		{
			// technically impossible to happen, leave an assert here anyway for debug builds.
			assert(0);
			return;
		}
		break;
	}

	if (trigger.type == BHOPTRIGGER_DISABLED)
	{
		return;
	}

	g_mappingApi.triggers.AddToTail(trigger);
}

static_function void Mapi_OnInfoTargetSpawn(const CEntityKeyValues *ekv)
{
	if (!ekv->GetBool(KEY_IS_COURSE_DESCRIPTOR))
	{
		return;
	}

	i32 hammerId = ekv->GetInt("hammerUniqueId", -1);
	Vector origin = ekv->GetVector("origin");

	i32 courseNumber = ekv->GetInt("timer_course_number", INVALID_COURSE_NUMBER);
	u32 courseMaxVelocity = ekv->GetInt("timer_course_max_velocity", INVALID_MAXVEL_NUMBER);
	const char *courseName = ekv->GetString("timer_course_name");
	const char *targetName = ekv->GetString("targetname");
	constexpr static_persist const char *targetNamePrefix = "[PR#]";
	if (BHOP_STREQLEN(targetName, targetNamePrefix, strlen(targetNamePrefix)))
	{
		targetName = targetName + strlen(targetNamePrefix);
	}

	if (courseNumber <= INVALID_COURSE_NUMBER)
	{
		Mapi_Error("Course number must be bigger than %i! Course descriptor Hammer ID %i, origin (%.0f %.0f %.0f)", INVALID_COURSE_NUMBER, hammerId,
				   origin.x, origin.y, origin.z);
		return;
	}

	if (courseMaxVelocity <= INVALID_MAXVEL_NUMBER)
	{
		Mapi_Error("Course max velocity must be more than %i! Course descriptor Hammer ID %i, origin (%.0f %.0f %.0f)", INVALID_MAXVEL_NUMBER,
				   hammerId, origin.x, origin.y, origin.z);
		return;
	}

	if (!courseName[0])
	{
		Mapi_Error("Course name is empty! Course number %i. Course descriptor Hammer ID %i, origin (%.0f %.0f %.0f)", courseNumber, hammerId,
				   origin.x, origin.y, origin.z);
		return;
	}

	if (!targetName[0])
	{
		Mapi_Error("Course targetname is empty! Course name \"%s\". Course number %i. Course descriptor Hammer ID %i, origin (%.0f %.0f %.0f)",
				   courseName, courseNumber, hammerId, origin.x, origin.y, origin.z);
		return;
	}

	Mapi_CreateCourse(courseNumber, courseName, hammerId, targetName, courseMaxVelocity);
}

static_function BhopTrigger *Mapi_FindBhopTrigger(CBaseTrigger *trigger)
{
	if (!trigger->m_pEntity)
	{
		return nullptr;
	}

	CEntityHandle triggerHandle = trigger->GetRefEHandle();
	if (!trigger || !triggerHandle.IsValid() || trigger->m_pEntity->m_flags & EF_IS_INVALID_EHANDLE)
	{
		return nullptr;
	}

	FOR_EACH_VEC(g_mappingApi.triggers, i)
	{
		if (triggerHandle == g_mappingApi.triggers[i].entity)
		{
			return &g_mappingApi.triggers[i];
		}
	}

	return nullptr;
}

static_function BhopTrigger *Mapi_FindBhopDestination(CBaseEntity *entity)
{
	if (!entity || !entity->m_pEntity)
	{
		return nullptr;
	}
	CEntityHandle entityHandle = entity->GetRefEHandle();
	if (!entityHandle.IsValid() || entity->m_pEntity->m_flags & EF_IS_INVALID_EHANDLE)
	{
		return nullptr;
	}
	int entityHammerId = atoi(entity->m_sUniqueHammerID.Get());
	FOR_EACH_VEC(g_mappingApi.triggers, i)
	{
		if (g_mappingApi.triggers[i].type == BHOPTRIGGER_DESTINATION && entityHandle == g_mappingApi.triggers[i].entity)
		{
			return &g_mappingApi.triggers[i];
		}
	}
	return nullptr;
}

static_function BhopCourseDescriptor *Mapi_FindCourse(const char *targetname)
{
	BhopCourseDescriptor *result = nullptr;
	if (!targetname)
	{
		return result;
	}

	FOR_EACH_VEC(g_mappingApi.courseDescriptors, i)
	{
		if (BHOP_STREQI(g_mappingApi.courseDescriptors[i].entityTargetname, targetname))
		{
			result = &g_mappingApi.courseDescriptors[i];
			break;
		}
	}

	return result;
}

static_function bool Mapi_FindStartPositionForTrigger(const BhopTrigger *trigger, Vector &originDest, QAngle &anglesDest)
{
	if (!trigger)
	{
		return false;
	}

	Vector mins = trigger->mins + trigger->origin;
	Vector maxs = trigger->maxs + trigger->origin;

	FOR_EACH_VEC(g_mappingApi.triggers, i)
	{
		const BhopTrigger *destination = &g_mappingApi.triggers[i];
		if (destination->type != BHOPTRIGGER_DESTINATION)
		{
			continue;
		}

		if (utils::IsVectorInBox(destination->origin, mins, maxs))
		{
			originDest = destination->origin;
			anglesDest = destination->rotation;
			return true;
		}
	}

	CBaseTrigger *entity = reinterpret_cast<CBaseTrigger *>(trigger->entity.Get());
	return utils::FindValidPositionForTrigger(entity, originDest, anglesDest);
}

static_function void Mapi_OnInfoTeleportDestinationSpawn(const EntitySpawnInfo_t *info)
{
	const CEntityKeyValues *ekv = info->m_pKeyValues;
	i32 hammerId = ekv->GetInt("hammerUniqueId", -1);

	BhopTrigger trigger = {};
	trigger.type = BHOPTRIGGER_DESTINATION;
	trigger.hammerId = hammerId;
	trigger.entity = info->m_pEntity->GetRefEHandle();

	g_mappingApi.triggers.AddToTail(trigger);
};

static_function void Mapi_OnTriggerTeleportSpawn(const EntitySpawnInfo_t *info)
{
	const CEntityKeyValues *ekv = info->m_pKeyValues;
	i32 hammerId = ekv->GetInt("hammerUniqueId", -1);

	BhopTrigger trigger = {};
	trigger.type = BHOPTRIGGER_TELEPORT;
	trigger.hammerId = hammerId;
	trigger.entity = info->m_pEntity->GetRefEHandle();

	const char *destination = ekv->GetString("target", "");
	const char *landmark = ekv->GetString("landmark", "");

	if (!destination || !destination[0])
	{
		META_CONPRINTF("Warning: Teleport trigger (hammerID %i) has no target specified!\n", hammerId);
		return;
	}
	snprintf(trigger.teleport.destination, sizeof(trigger.teleport.destination), "%s", destination);
	snprintf(trigger.teleport.landmark, sizeof(trigger.teleport.landmark), "%s", landmark);

	if (landmark && landmark[0])
	{
		trigger.teleport.relative = true;
		trigger.teleport.useDestinationAngles = false;
	}
	else
	{
		trigger.teleport.relative = false;
		trigger.teleport.useDestinationAngles = true;
	}

	g_mappingApi.triggers.AddToTail(trigger);
};

static_function void Mapi_OnTriggerPushSpawn(const EntitySpawnInfo_t *info)
{
	const CEntityKeyValues *ekv = info->m_pKeyValues;
	i32 hammerId = ekv->GetInt("hammerUniqueId", -1);
	QAngle pushDir = ekv->GetQAngle("pushdir", QAngle(0, 0, 0));
	int speed = ekv->GetInt("speed", 0);

	BhopTrigger trigger = {};
	trigger.type = BHOPTRIGGER_PUSH;
	trigger.hammerId = hammerId;
	trigger.rotation = pushDir;

	trigger.push.pushConditions |= ekv->GetBool("triggeronstarttouch") ? BhopMapPush::BHOP_PUSH_START_TOUCH : BhopMapPush::BHOP_PUSH_TOUCH;
	trigger.push.pushConditions |= BhopMapPush::BHOP_PUSH_LEGACY;
	trigger.push.impulse[0] = 0.0f;
	trigger.push.impulse[1] = 0.0f;
	trigger.push.impulse[2] = 0.0f;
	trigger.push.speed = speed;

	CBaseEntity *baseEntity = static_cast<CBaseEntity *>(info->m_pEntity->m_pInstance);
	if (baseEntity)
	{
		uint32 flags = baseEntity->m_fFlags;

		// Once Only (set velocity rather than apply acceleration) : [128]
		bool pushOnce = (flags & 128) != 0;

		trigger.push.setSpeed[0] = pushOnce ? true : false;
		trigger.push.setSpeed[1] = pushOnce ? true : false;
		trigger.push.setSpeed[2] = pushOnce ? true : false;
	}

	trigger.push.cancelOnTeleport = false;
	trigger.push.cooldown = 0.1f;
	trigger.push.delay = 0.0f;

	trigger.entity = info->m_pEntity->GetRefEHandle();

	g_mappingApi.triggers.AddToTail(trigger);
};

void Bhop::mapapi::Init()
{
	g_mappingApi = {};

	g_errorTimer = g_errorTimer ? g_errorTimer : StartTimer(Mapi_PrintErrors, true);
}

void Bhop::mapapi::OnCreateLoadingSpawnGroupHook(const CUtlVector<const CEntityKeyValues *> *pKeyValues)
{
	if (!pKeyValues)
	{
		return;
	}

	if (g_mappingApi.apiVersionLoaded)
	{
		return;
	}

	for (i32 i = 0; i < pKeyValues->Count(); i++)
	{
		auto ekv = (*pKeyValues)[i];

		if (!ekv)
		{
			continue;
		}
		const char *classname = ekv->GetString("classname");
		if (BHOP_STREQI(classname, "worldspawn"))
		{
			// We only care about the first spawn group's worldspawn because the rest might use prefabs compiled outside of mapping API.
			g_mappingApi.apiVersionLoaded = true;
			g_mappingApi.mapApiVersion = ekv->GetInt("timer_mapping_api_version", BHOP_NO_MAPAPI_VERSION);
			// NOTE(GameChaos): When a new mapping api version comes out, this will change
			//  for backwards compatibility.
			if (g_mappingApi.mapApiVersion == BHOP_NO_MAPAPI_VERSION)
			{
				META_CONPRINTF("Warning: Map is not compiled with Mapping API. Reverting to default behavior.\n");

				g_mapCfgMaxVelocity = g_pBhopUtils->GetCurrentMapMaxVelocity();
				if (g_mapCfgMaxVelocity <= INVALID_MAXVEL_NUMBER)
				{
					g_mapCfgMaxVelocity = 3500;
				}

				// Manually create a BHOP_NO_MAPAPI_COURSE_NAME course here because there shouldn't be any info_target_server_only around.
				Mapi_CreateCourse(1, BHOP_NO_MAPAPI_COURSE_NAME, -1, BHOP_NO_MAPAPI_COURSE_DESCRIPTOR, g_mapCfgMaxVelocity);
				Mapi_LoadFakeZoneConfig();
				break;
			}
			if (g_mappingApi.mapApiVersion != BHOP_MAPAPI_VERSION)
			{
				Mapi_Error("FATAL. Mapping API version %i is invalid!", g_mappingApi.mapApiVersion);
				g_mappingApi.fatalFailure = true;
				return;
			}
			break;
		}
	}
	// Do a second pass for course descriptors.
	if (g_mappingApi.mapApiVersion != BHOP_NO_MAPAPI_VERSION)
	{
		for (i32 i = 0; i < pKeyValues->Count(); i++)
		{
			auto ekv = (*pKeyValues)[i];

			if (!ekv)
			{
				continue;
			}
			const char *classname = ekv->GetString("classname");
			if (BHOP_STREQI(classname, "info_target_server_only"))
			{
				Mapi_OnInfoTargetSpawn(ekv);
			}
		}
	}
}

void Bhop::mapapi::OnSpawn(int count, const EntitySpawnInfo_t *info)
{
	if (!info || g_mappingApi.fatalFailure)
	{
		return;
	}

	for (i32 i = 0; i < count; i++)
	{
		auto ekv = info[i].m_pKeyValues;
#if 0
		// Debug print for all keyvalues
		FOR_EACH_ENTITYKEY(ekv, iter)
		{
			auto kv = ekv->GetKeyValue(iter);
			if (!kv)
			{
				continue;
			}
			CBufferStringGrowable<128> bufferStr;
			const char *key = ekv->GetEntityKeyId(iter).GetString();
			const char *value = kv->ToString(bufferStr);
			Msg("\t%s: %s\n", key, value);
		}
#endif

		if (!info[i].m_pEntity || !ekv || !info[i].m_pEntity->GetClassname())
		{
			continue;
		}
		const char *classname = info[i].m_pEntity->GetClassname();
		const char *targetname = ekv->GetString("targetname", "");
		if (BHOP_STREQI(classname, "trigger_multiple"))
		{
			Mapi_OnTriggerMultipleSpawn(&info[i]);
		}
		else if (BHOP_STREQI(classname, "info_teleport_destination"))
		{
			Mapi_OnInfoTeleportDestinationSpawn(&info[i]);
		}
		else if (BHOP_STREQI(classname, "trigger_push"))
		{
			Mapi_OnTriggerPushSpawn(&info[i]);
		}
		else if (BHOP_STREQI(classname, "trigger_teleport"))
		{
			Mapi_OnTriggerTeleportSpawn(&info[i]);
		}
		else if (BHOP_STREQI(classname, "game_player_equip"))
		{
			// allow players to choose their equipment
			g_pBhopUtils->RemoveEntity(info[i].m_pEntity->m_pInstance);
		}
		else if (BHOP_STREQI(classname, "logic_timer") && BHOP_STREQI(targetname, "clash_ad_timer"))
		{
			// fuck gambling you slimy losers
			g_pBhopUtils->RemoveEntity(info[i].m_pEntity->m_pInstance);
		}
		else if (BHOP_STREQI(classname, "func_brush") && BHOP_STREQI(targetname, "clash_ad"))
		{
			g_pBhopUtils->RemoveEntity(info[i].m_pEntity->m_pInstance);
		}
	}

	if (g_mappingApi.fatalFailure)
	{
		g_mappingApi.triggers.RemoveAll();
		g_mappingApi.courseDescriptors.RemoveAll();
	}
}

void Bhop::mapapi::OnRoundPreStart()
{
	g_mappingApi.triggers.RemoveAll();
	g_mappingApi.detectedFakeStartTriggerNames.RemoveAll();
	g_mappingApi.detectedFakeEndTriggerNames.RemoveAll();
	g_mappingApi.roundIsStarting = true;
}

void Bhop::mapapi::OnRoundStart()
{
	g_mappingApi.roundIsStarting = false;
	FOR_EACH_VEC(g_mappingApi.courseDescriptors, courseInd)
	{
		//  Find the number of stage zones that a course has
		//  and make sure that they start from 1 and are consecutive by
		//  XORing the values with a consecutive 1...n sequence.
		//  https://florian.github.io/xor-trick/
		//  Handle checkpoints seperately to support route splits
		i32 stageXor = 0;
		i32 stageCount = 0;
		bool seenCp[BHOP_MAX_CHECKPOINT_ZONES] = {};
		i32 maxCpNum = 0;
		i32 cpTriggerCount = 0;
		bool invalid = false;

		BhopCourseDescriptor *courseDescriptor = &g_mappingApi.courseDescriptors[courseInd];
		FOR_EACH_VEC(g_mappingApi.triggers, i)
		{
			BhopTrigger *trigger = &g_mappingApi.triggers[i];
			if (!Bhop::mapapi::IsTimerTrigger(trigger->type))
			{
				if (trigger->type == BHOPTRIGGER_DESTINATION)
				{
					CBaseEntity *pEntity = reinterpret_cast<CBaseEntity *>(trigger->entity.Get());
					if (!pEntity)
					{
						continue;
					}

					Vector absOrigin = pEntity->m_CBodyComponent()->m_pSceneNode()->m_vecAbsOrigin();
					QAngle absRotation = pEntity->m_CBodyComponent()->m_pSceneNode()->m_angAbsRotation();
					trigger->origin = absOrigin;
					trigger->rotation = absRotation;

					continue;
				}
				continue;
			}

			if (!BHOP_STREQ(trigger->zone.courseDescriptor, courseDescriptor->entityTargetname))
			{
				continue;
			}

			switch (trigger->type)
			{
				case BHOPTRIGGER_ZONE_START:
				case BHOPTRIGGER_ZONE_END:
				case BHOPTRIGGER_ZONE_BONUS_START:
				case BHOPTRIGGER_ZONE_BONUS_END:
				case BHOPTRIGGER_ZONE_STAGE:
				{
					CBaseEntity *pEntity = reinterpret_cast<CBaseEntity *>(trigger->entity.Get());
					Vector absOrigin = pEntity->m_CBodyComponent()->m_pSceneNode()->m_vecAbsOrigin();
					QAngle absRotation = pEntity->m_CBodyComponent()->m_pSceneNode()->m_angRotation();
					Vector mins = pEntity->m_pCollision()->m_vecMins();
					Vector maxs = pEntity->m_pCollision()->m_vecMaxs();

					trigger->mins = mins;
					trigger->maxs = maxs;
					trigger->origin = absOrigin;
					trigger->rotation = absRotation;

					if (trigger->type == BHOPTRIGGER_ZONE_STAGE)
					{
						if (trigger->zone.number < 1 || trigger->zone.number >= BHOP_MAX_STAGE_ZONES)
						{
							Mapi_Error("Course \"%s\" Stage zone has invalid number %d!", courseDescriptor->name, trigger->zone.number);
							invalid = true;
							break;
						}
						stageXor ^= (++stageCount) ^ trigger->zone.number;
						break;
					}

					if (g_pBhopZoneBeamService)
					{
						g_pBhopZoneBeamService->AddZone(trigger);
					}
					break;
				}
				case BHOPTRIGGER_ZONE_CHECKPOINT:
				{
					i32 num = trigger->zone.number;
					cpTriggerCount++;
					if (num < 1 || num >= BHOP_MAX_CHECKPOINT_ZONES)
					{
						Mapi_Error("Course \"%s\" Checkpoint zone has invalid number %d!", courseDescriptor->name, num);
						invalid = true;
						break;
					}
					seenCp[num] = true;
					if (num > maxCpNum)
					{
						maxCpNum = num;
					}
					break;
				}
			}
		}

		for (i32 n = 1; n <= maxCpNum; n++)
		{
			if (!seenCp[n])
			{
				Mapi_Error("Course \"%s\" Checkpoint zones aren't consecutive, missing checkpoint %d!", courseDescriptor->name, n);
				invalid = true;
				break;
			}
		}

		if (maxCpNum > BHOP_MAX_CHECKPOINT_ZONES)
		{
			Mapi_Error("Course \"%s\" Too many checkpoint zones! Maximum is %i.", courseDescriptor->name, BHOP_MAX_CHECKPOINT_ZONES);
			invalid = true;
		}

		if (stageCount > BHOP_MAX_STAGE_ZONES)
		{
			Mapi_Error("Course \"%s\" Too many stage zones! Maximum is %i.", courseDescriptor->name, BHOP_MAX_STAGE_ZONES);
			invalid = true;
		}

		FOR_EACH_VEC(g_mappingApi.triggers, i)
		{
			BhopTrigger *trigger = &g_mappingApi.triggers[i];
			if (trigger->type != BHOPTRIGGER_ZONE_START && trigger->type != BHOPTRIGGER_ZONE_BONUS_START)
			{
				continue;
			}
			if (!BHOP_STREQ(trigger->zone.courseDescriptor, courseDescriptor->entityTargetname))
			{
				continue;
			}

			Vector startPosition;
			QAngle startAngles;
			if (Mapi_FindStartPositionForTrigger(trigger, startPosition, startAngles))
			{
				courseDescriptor->SetStartPosition(startPosition, startAngles);
			}
		}

		if (invalid)
		{
			g_mappingApi.courseDescriptors.FastRemove(courseInd);
			courseInd--;
			break;
		}
		courseDescriptor->checkpointCount = maxCpNum;
		courseDescriptor->stageCount = stageCount;
	}

	Bhop::course::SetupLocalCourses();
}

void Bhop::mapapi::CheckEndTimerTrigger(CBaseTrigger *trigger)
{
	BhopTrigger *bhopTrigger = Mapi_FindBhopTrigger(trigger);
	if (bhopTrigger && (bhopTrigger->type == BHOPTRIGGER_ZONE_END || bhopTrigger->type == BHOPTRIGGER_ZONE_BONUS_END))
	{
		BhopCourseDescriptor *desc = Mapi_FindCourse(bhopTrigger->zone.courseDescriptor);
		if (!desc)
		{
			return;
		}
		desc->hasEndPosition = utils::FindValidPositionForTrigger(trigger, desc->endPosition, desc->endAngles);
	}
}

const BhopTrigger *Bhop::mapapi::IsPositionInOrAboveTimerZone(const Vector &position)
{
	FOR_EACH_VEC(g_mappingApi.triggers, i)
	{
		const BhopTrigger *trigger = &g_mappingApi.triggers[i];
		if (trigger->type != BHOPTRIGGER_ZONE_START && trigger->type != BHOPTRIGGER_ZONE_BONUS_START && trigger->type != BHOPTRIGGER_ZONE_STAGE)
		{
			continue;
		}
		Vector mins = trigger->mins + trigger->origin;
		Vector maxs = trigger->maxs + trigger->origin;

		bool inHorizontalBounds = position.x >= mins.x && position.x <= maxs.x && position.y >= mins.y && position.y <= maxs.y;

		bool inVerticalBounds = position.z >= mins.z && position.z <= maxs.z;

		bool aboveStartZone = position.z > maxs.z && position.z <= maxs.z + 400.0f;

		if (inHorizontalBounds && (inVerticalBounds || aboveStartZone))
		{
			return trigger;
		}
	}
	return nullptr;
}

const BhopTrigger *Bhop::mapapi::GetBhopTrigger(CBaseTrigger *trigger)
{
	return Mapi_FindBhopTrigger(trigger);
}

const BhopTrigger *Bhop::mapapi::GetBhopDestination(CBaseEntity *entity)
{
	return Mapi_FindBhopDestination(entity);
}

const BhopCourseDescriptor *Bhop::mapapi::GetCourseDescriptorFromTrigger(CBaseTrigger *trigger)
{
	BhopTrigger *bhopTrigger = Mapi_FindBhopTrigger(trigger);
	if (!bhopTrigger)
	{
		return nullptr;
	}
	return Bhop::mapapi::GetCourseDescriptorFromTrigger(bhopTrigger);
}

const BhopCourseDescriptor *Bhop::mapapi::GetCourseDescriptorFromTrigger(const BhopTrigger *trigger)
{
	const BhopCourseDescriptor *course = nullptr;
	switch (trigger->type)
	{
		case BHOPTRIGGER_ZONE_START:
		case BHOPTRIGGER_ZONE_END:
		case BHOPTRIGGER_ZONE_BONUS_START:
		case BHOPTRIGGER_ZONE_BONUS_END:
		case BHOPTRIGGER_ZONE_CHECKPOINT:
		case BHOPTRIGGER_ZONE_STAGE:
		{
			course = Mapi_FindCourse(trigger->zone.courseDescriptor);
			if (!course)
			{
				Mapi_Error("%s: Couldn't find course descriptor from name \"%s\"! Trigger's Hammer Id: %i", g_errorPrefix,
						   trigger->zone.courseDescriptor, trigger->hammerId);
			}
		}
		break;
	}
	return course;
}

bool MappingInterface::IsTriggerATimerZone(CBaseTrigger *trigger)
{
	BhopTrigger *bhopTrigger = Mapi_FindBhopTrigger(trigger);
	if (!bhopTrigger)
	{
		return false;
	}
	return Bhop::mapapi::IsTimerTrigger(bhopTrigger->type);
}

bool MappingInterface::GetJumpstatArea(Vector &pos, QAngle &angles)
{
	if (g_mappingApi.hasJumpstatArea)
	{
		pos = g_mappingApi.jumpstatAreaPos;
		angles = g_mappingApi.jumpstatAreaAngles;
	}

	return g_mappingApi.hasJumpstatArea;
}

void Bhop::course::ClearCourses()
{
	g_sortedCourses.RemoveAll();
	BhopTimerService::ClearRecordCache();
}

u32 Bhop::course::GetCourseCount()
{
	return g_sortedCourses.Count();
}

const BhopCourseDescriptor *Bhop::course::GetCourseByCourseID(i32 id)
{
	FOR_EACH_VEC(g_sortedCourses, i)
	{
		if (g_sortedCourses[i]->id == id)
		{
			return g_sortedCourses[i];
		}
	}
	return nullptr;
}

const BhopCourseDescriptor *Bhop::course::GetCourseByLocalCourseID(u32 id)
{
	FOR_EACH_VEC(g_sortedCourses, i)
	{
		if (g_sortedCourses[i]->localDatabaseID == id)
		{
			return g_sortedCourses[i];
		}
	}
	return nullptr;
}

const BhopCourseDescriptor *Bhop::course::GetCourseByGlobalCourseID(u32 id)
{
	FOR_EACH_VEC(g_sortedCourses, i)
	{
		if (g_sortedCourses[i]->globalDatabaseID == id)
		{
			return g_sortedCourses[i];
		}
	}
	return nullptr;
}

const BhopCourseDescriptor *Bhop::course::GetCourse(const char *courseName, bool caseSensitive)
{
	FOR_EACH_VEC(g_sortedCourses, i)
	{
		const char *name = g_sortedCourses[i]->name;
		if (caseSensitive ? BHOP_STREQ(name, courseName) : BHOP_STREQI(name, courseName))
		{
			return g_sortedCourses[i];
		}
	}
	return nullptr;
}

const BhopCourseDescriptor *Bhop::course::GetCourse(u32 guid)
{
	FOR_EACH_VEC(g_sortedCourses, i)
	{
		if (g_sortedCourses[i]->guid == guid)
		{
			return g_sortedCourses[i];
		}
	}
	return nullptr;
}

const BhopCourseDescriptor *Bhop::course::GetFirstCourse()
{
	if (g_sortedCourses.Count() >= 1)
	{
		return g_sortedCourses[0];
	}
	return nullptr;
}

void Bhop::course::SetupLocalCourses()
{
	if (BhopDatabaseService::IsMapSetUp())
	{
		BhopDatabaseService::SetupCourses(g_sortedCourses);
	}
}

const BhopCourseDescriptor *Bhop::course::EnsureLocalCourse(const char *courseName)
{
	const char *name = courseName && courseName[0] ? courseName : BHOP_NO_MAPAPI_COURSE_NAME;
	if (const BhopCourseDescriptor *existing = Bhop::course::GetCourse(name, false))
	{
		return existing;
	}

	i32 nextCourseID = g_sortedCourses.Count() + 1;
	char descriptor[128];
	V_snprintf(descriptor, sizeof(descriptor), "fakezone_%s", name);
	if (!Mapi_CreateCourse(nextCourseID, name, -100000 - nextCourseID, descriptor, INVALID_MAXVEL_NUMBER))
	{
		return Bhop::course::GetCourse(name, false);
	}
	Bhop::course::SetupLocalCourses();
	return Bhop::course::GetCourse(name, false);
}

bool Bhop::course::UpdateCourseLocalID(const char *courseName, u32 databaseID)
{
	FOR_EACH_VEC(g_sortedCourses, i)
	{
		if (g_sortedCourses[i]->GetName() == courseName)
		{
			g_sortedCourses[i]->localDatabaseID = databaseID;
			return true;
		}
	}
	return false;
}

bool Bhop::course::UpdateCourseGlobalID(const char *courseName, u32 globalID)
{
	FOR_EACH_VEC(g_sortedCourses, i)
	{
		if (g_sortedCourses[i]->GetName() == courseName)
		{
			g_sortedCourses[i]->globalDatabaseID = globalID;
			return true;
		}
	}
	return false;
}

static void ListCourses(BhopPlayer *player)
{
	if (player->timerService->GetCourse())
	{
		player->languageService->PrintChat(true, false, "Current Course", player->timerService->GetCourse()->name);
	}
	else
	{
		player->languageService->PrintChat(true, false, "No Current Course");
	}
	player->languageService->PrintConsole(false, false, "Course List Header");
	for (u32 i = 0; i < Bhop::course::GetCourseCount(); i++)
	{
		player->PrintConsole(false, false, "%s", g_sortedCourses[i]->name);
	}
}

SCMD(bhop_courses, SCFL_MAP)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	ListCourses(player);
	return MRES_SUPERCEDE;
}

SCMD(bhop_zoneexport, SCFL_MAP)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	char relativePath[1024] {};
	FakeZoneExportResult result = Mapi_ExportFakeZoneConfig(relativePath, sizeof(relativePath));
	switch (result)
	{
		case FakeZoneExportResult::Success:
		{
			player->PrintChat(true, false, "Saved fake zone trigger aliases to %s.", relativePath);
			player->PrintConsole(false, false, "Saved fake zone trigger aliases to %s.", relativePath);
			return MRES_SUPERCEDE;
		}
		case FakeZoneExportResult::NoMapName:
		{
			player->PrintChat(true, false, "Could not save fake zone aliases because the current map name is unavailable.");
			return MRES_SUPERCEDE;
		}
		case FakeZoneExportResult::OfficialMappingApi:
		{
			player->PrintChat(true, false, "This map uses the official Mapping API, so fake zone aliases are not exported.");
			return MRES_SUPERCEDE;
		}
		case FakeZoneExportResult::NoDetectedPair:
		{
			player->PrintChat(true, false, "No fake start/end trigger pair was detected for this map.");
			return MRES_SUPERCEDE;
		}
		case FakeZoneExportResult::WriteFailed:
		{
			player->PrintChat(true, false, "Failed to write fake zone aliases to %s.", relativePath);
			player->PrintConsole(false, false, "Failed to write fake zone aliases to %s.", relativePath);
			return MRES_SUPERCEDE;
		}
	}

	return MRES_SUPERCEDE;
}

SCMD_LINK(bhop_savezones, bhop_zoneexport);

SCMD(bhop_course, SCFL_MAP)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	if (args->ArgC() < 2)
	{
		ListCourses(player);
	}
	else
	{
		Bhop::misc::HandleTeleportToCourse(player, args);
	}
	return MRES_SUPERCEDE;
}
