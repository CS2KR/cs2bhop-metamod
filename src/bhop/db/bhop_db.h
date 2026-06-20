#pragma once
#include "../bhop.h"
#include "bhop/timer/bhop_timer.h"

class ISQLConnection;
class ISQLQuery;
typedef std::function<void(std::vector<ISQLQuery *>)> TransactionSuccessCallbackFunc;
typedef std::function<void(std::string, int)> TransactionFailureCallbackFunc;

namespace Bhop
{
	namespace Database
	{
		enum class DatabaseType
		{
			None = -1,
			SQLite,
			MySQL
		};
	} // namespace Database
} // namespace Bhop

class BhopDatabaseServiceEventListener
{
public:
	virtual void OnDatabaseSetup() {}

	virtual void OnClientSetup(Player *player, u64 steamID64, bool isCheater) {}

	virtual void OnMapSetup() {}

	virtual void OnCoursesSetup() {}

	virtual void OnTimeInserted(Player *player, u64 steamID64, u32 mapID, u32 course, u64 mode, u64 styles, u64 runtimeMS) {}

	virtual void OnTimeProcessed(Player *player, u64 steamID64, u32 mapID, u32 course, u64 mode, u64 styles, u64 runtimeMS, bool firstTime,
								 f64 pbDiff, u32 rank, u32 maxRank)
	{
	}

	virtual void OnNewRecord(Player *player, u64 steamID64, u32 mapID, u32 course, u64 mode, u64 styles) {}

	virtual void OnRecordMissed(Player *player, f64 recordTime, u32 course, u64 mode, u64 styles) {}

	virtual void OnPBMissed(Player *player, f64 pbTime, u32 course, u64 mode, u64 styles) {}
};

class BhopDatabaseService : public BhopBaseService
{
	using BhopBaseService::BhopBaseService;

public:
	static bool RegisterEventListener(BhopDatabaseServiceEventListener *eventListener);
	static bool UnregisterEventListener(BhopDatabaseServiceEventListener *eventListener);

	static void Init();
	static void Cleanup();
	static bool IsReady();

private:
	static Bhop::Database::DatabaseType databaseType;
	static ISQLConnection *databaseConnection;

	static i32 currentMapID;

private:
	static CUtlVector<BhopDatabaseServiceEventListener *> eventListeners;

public:
	static Bhop::Database::DatabaseType GetDatabaseType()
	{
		return databaseType;
	}

	static ISQLConnection *GetDatabaseConnection()
	{
		return databaseConnection;
	}

	static void OnGenericTxnSuccess(std::vector<ISQLQuery *> queries)
	{
		ConMsg("[Bhop::DB] Transaction successful.\n");
	}

	static void OnGenericTxnFailure(std::string error, int failIndex)
	{
		ConMsg("[Bhop::DB] Transaction failed at %i (%s).\n", failIndex, error.c_str());
	}

	static void OnGenericQuerySuccess(ISQLQuery *query) {}

	static void SetupDatabase();
	static void OnDatabaseConnected(bool connect);

	static void RunMigrations();

private:
	static void CheckMigrations(std::vector<ISQLQuery *> queries);

public:
	static bool IsMapSetUp();
	static void SetupMap();

	static i32 GetMapID()
	{
		return currentMapID;
	}

	// Course
	static bool AreCoursesSetUp();
	static void SetupCourses(CUtlVector<BhopCourseDescriptor *> &courses);
	static void FindFirstCourseByMapName(CUtlString mapName, TransactionSuccessCallbackFunc onSuccess, TransactionFailureCallbackFunc onFailure);

	// Client/Player
	void SetupClient();
	void SavePrefs(CUtlString prefs);
	bool isCheater {};

private:
	bool isSetUp {};

public:
	bool IsSetup()
	{
		return isSetUp;
	}

	static void FindPlayerByAlias(CUtlString playerName, TransactionSuccessCallbackFunc onSuccess, TransactionFailureCallbackFunc onFailure);

	// Mode
	static void UpdateModeIDs();
	static void InsertAndUpdateModeIDs(CUtlString modeName, CUtlString shortName);

	// Styles
	static void UpdateStyleIDs();
	static void InsertAndUpdateStyleIDs(CUtlString styleName, CUtlString shortName);

	// Times
	static void SaveTime(const char *runUUID, u64 steamID, u32 courseID, i32 modeID, f64 time, u64 styleIDs, std::string_view metadata,
						 TransactionSuccessCallbackFunc onSuccess, TransactionFailureCallbackFunc onFailure);
	static void UpdateRunUUID(const char *oldUUID, const char *newUUID, TransactionSuccessCallbackFunc onSuccess,
							  TransactionFailureCallbackFunc onFailure);
	static void QueryAllPBs(u64 steamID64, CUtlString mapName, TransactionSuccessCallbackFunc onSuccess, TransactionFailureCallbackFunc onFailure);
	static void QueryPB(u64 steamID64, CUtlString mapName, CUtlString courseName, u32 modeID, TransactionSuccessCallbackFunc onSuccess,
						TransactionFailureCallbackFunc onFailure);
	static void QueryPBRankless(u64 steamID64, CUtlString mapName, CUtlString courseName, u32 modeID, u64 styleIDFlags,
								TransactionSuccessCallbackFunc onSuccess, TransactionFailureCallbackFunc onFailure);

	static void QueryAllRecords(CUtlString mapName, TransactionSuccessCallbackFunc onSuccess, TransactionFailureCallbackFunc onFailure);
	static void QueryRecords(CUtlString mapName, CUtlString courseName, u32 modeID, u32 count, u32 offset, TransactionSuccessCallbackFunc onSuccess,
							 TransactionFailureCallbackFunc onFailure);
};
