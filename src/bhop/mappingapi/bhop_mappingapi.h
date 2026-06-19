
#pragma once

#define BHOP_MAPPING_INTERFACE "BhopMappingInterface"

#define BHOP_NO_MAPAPI_VERSION           0
#define BHOP_NO_MAPAPI_COURSE_DESCRIPTOR "Default"
#define BHOP_NO_MAPAPI_COURSE_NAME       "Main"

#define BHOP_MAPAPI_VERSION 2

#define BHOP_MAX_CHECKPOINT_ZONES   100
#define BHOP_MAX_STAGE_ZONES        100
#define BHOP_MAX_COURSE_COUNT       128
#define BHOP_MAX_COURSE_NAME_LENGTH 65

#define INVALID_CHECKPOINT_NUMBER 0
#define INVALID_STAGE_NUMBER      0
#define INVALID_COURSE_NUMBER     0
#define INVALID_MAXVEL_NUMBER     0

struct BhopCourse;
class BhopPlayer;

enum BhopTriggerType
{
	BHOPTRIGGER_DISABLED = 0,
	BHOPTRIGGER_MODIFIER,

	BHOPTRIGGER_ZONE_START,
	BHOPTRIGGER_ZONE_END,
	BHOPTRIGGER_ZONE_CHECKPOINT,
	BHOPTRIGGER_ZONE_STAGE,
	BHOPTRIGGER_ZONE_BONUS_START,
	BHOPTRIGGER_ZONE_BONUS_END,

	BHOPTRIGGER_TELEPORT,

	BHOPTRIGGER_DESTINATION,

	BHOPTRIGGER_PUSH,
	BHOPTRIGGER_COUNT,
};

// BHOPTRIGGER_MODIFIER
struct BhopMapModifier
{
	f32 gravity;
	f32 jumpFactor;
};

// BHOPTRIGGER_ZONE_*
struct BhopMapZone
{
	char courseDescriptor[128];
	i32 number; // not used on start/end zones
	i32 bonus;
};

// BHOPTRIGGER_TELEPORT
struct BhopMapTeleport
{
	char destination[128];
	char landmark[128];
	f32 delay;
	bool useDestinationAngles;
	bool resetSpeed;
	bool reorientPlayer;
	bool relative;
};

// BHOPTRIGGER_PUSH
struct BhopMapPush
{
	// Cannot use Vector here as it is not a POD type.
	f32 impulse[3];

	enum BhopMapPushCondition : u32
	{
		BHOP_PUSH_START_TOUCH = 1,
		BHOP_PUSH_TOUCH = 2,
		BHOP_PUSH_END_TOUCH = 4,
		BHOP_PUSH_JUMP_EVENT = 8,
		BHOP_PUSH_JUMP_BUTTON = 16,
		BHOP_PUSH_ATTACK = 32,
		BHOP_PUSH_ATTACK2 = 64,
		BHOP_PUSH_USE = 128,
		BHOP_PUSH_LEGACY = 256,
	};

	u32 pushConditions;
	bool setSpeed[3];
	bool cancelOnTeleport;
	f32 cooldown;
	f32 delay;
	int speed;
};

struct BhopCourseDescriptor

{
	BhopCourseDescriptor(i32 hammerId = -1, const char *targetName = "", u32 guid = 0, i32 courseID = INVALID_COURSE_NUMBER,
						 const char *courseName = "", u32 courseMaxVelocity = INVALID_MAXVEL_NUMBER)
		: hammerId(hammerId), guid(guid), id(courseID), maxVel(courseMaxVelocity)
	{
		V_snprintf(entityTargetname, sizeof(entityTargetname), "%s", targetName);
		V_snprintf(name, sizeof(name), "%s", courseName);
	}

	char entityTargetname[128] {};
	i32 hammerId = -1;

	bool hasStartPosition = false;
	Vector startPosition;
	QAngle startAngles;

	void SetStartPosition(Vector origin, QAngle angles)
	{
		hasStartPosition = true;
		startPosition = origin;
		startAngles = angles;
	}

	bool hasEndPosition = false;
	Vector endPosition;
	QAngle endAngles;

	i32 checkpointCount {};
	i32 stageCount {};

	// Shared identifiers
	u32 guid {};

	// Mapper assigned course ID.
	i32 id;
	// Mapper assigned course name.
	char name[BHOP_MAX_COURSE_NAME_LENGTH] {};
	// Mapper assigned course max velocity.
	u32 maxVel;

	bool HasMatchingIdentifiers(i32 id, const char *name) const
	{
		return this->id == id && (!V_stricmp(this->name, name));
	}

	CUtlString GetName() const
	{
		return name;
	}

	// ID used for local database.
	u32 localDatabaseID {};

	// ID used for global database.
	u32 globalDatabaseID {};
};

struct BhopTrigger
{
	BhopTriggerType type;
	CEntityHandle entity;
	i32 hammerId;
	Vector mins;
	Vector maxs;
	Vector origin;
	QAngle rotation;

	union
	{
		BhopMapModifier modifier;
		BhopMapZone zone;
		BhopMapTeleport teleport;
		BhopMapPush push;
	};
};

namespace Bhop::mapapi
{
	// These namespace'd functions are called when relevant game events happen, and are somewhat in order.
	void Init();
	void OnCreateLoadingSpawnGroupHook(const CUtlVector<const CEntityKeyValues *> *pKeyValues);
	void OnSpawn(int count, const EntitySpawnInfo_t *info);
	void OnRoundPreStart();
	void OnRoundStart();
	const BhopTrigger *IsPositionInOrAboveTimerZone(const Vector &position);

	void CheckEndTimerTrigger(CBaseTrigger *trigger);
	// This is const, unlike the trigger returned from Mapi_FindBhopTrigger.
	const BhopTrigger *GetBhopTrigger(CBaseTrigger *trigger);
	const BhopTrigger *GetBhopDestination(CBaseEntity *entity);

	const BhopCourseDescriptor *GetCourseDescriptorFromTrigger(CBaseTrigger *trigger);
	const BhopCourseDescriptor *GetCourseDescriptorFromTrigger(const BhopTrigger *trigger);

	inline bool IsTimerTrigger(BhopTriggerType triggerType)
	{
		static_assert(BHOPTRIGGER_ZONE_START == 2 && BHOPTRIGGER_ZONE_BONUS_END == 7,
					  "Don't forget to change this function when changing the BhopTriggerType enum!!!");
		return triggerType >= BHOPTRIGGER_ZONE_START && triggerType <= BHOPTRIGGER_ZONE_BONUS_END;
	}

	inline bool IsPushTrigger(BhopTriggerType triggerType)
	{
		return triggerType == BHOPTRIGGER_PUSH;
	}

	inline bool IsTeleportTrigger(BhopTriggerType triggerType)
	{
		return triggerType == BHOPTRIGGER_TELEPORT;
	}
} // namespace Bhop::mapapi

// Exposed interface to modes.
class MappingInterface
{
public:
	virtual bool IsTriggerATimerZone(CBaseTrigger *trigger);
	bool GetJumpstatArea(Vector &pos, QAngle &angles);
};

extern MappingInterface *g_pMappingApi;

namespace Bhop::course
{
	// Clear the list of current courses.
	void ClearCourses();

	// Get the number of courses on this map.
	u32 GetCourseCount();

	// Get a course's information given its map-defined course id.
	const BhopCourseDescriptor *GetCourseByCourseID(i32 id);

	// Get a course's information given its local course id.
	const BhopCourseDescriptor *GetCourseByLocalCourseID(u32 id);

	// Get a course's information given its global course id.
	const BhopCourseDescriptor *GetCourseByGlobalCourseID(u32 id);

	// Get a course's information given its name.
	const BhopCourseDescriptor *GetCourse(const char *courseName, bool caseSensitive = true);

	// Get a course's information given its GUID.
	const BhopCourseDescriptor *GetCourse(u32 guid);

	// Get the first course's information sorted by map-defined ID.
	const BhopCourseDescriptor *GetFirstCourse();

	// Setup all the courses to the local database.
	void SetupLocalCourses();

	// Ensure a local-only course exists for systems that provide zones outside Hammer Mapping API.
	const BhopCourseDescriptor *EnsureLocalCourse(const char *courseName = BHOP_NO_MAPAPI_COURSE_NAME);

	// Update the course's database ID given its name.
	bool UpdateCourseLocalID(const char *courseName, u32 databaseID);

	// Update the course's global ID given its map-defined name and ID.
	bool UpdateCourseGlobalID(const char *courseName, u32 globalID);

}; // namespace Bhop::course
