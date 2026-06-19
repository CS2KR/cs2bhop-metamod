#include "bhop_db.h"
#include "bhop/mode/bhop_mode.h"
#include "bhop/style/bhop_style.h"
#include "bhop/timer/bhop_timer.h"
#include "queries/save_time.h"
#include "queries/times.h"
#include "utils/uuid.h"
#include "vendor/sql_mm/src/public/sql_mm.h"

using namespace Bhop::Database;

void BhopDatabaseService::SaveTime(const char *runUUID, u64 steamID, u32 courseID, i32 modeID, f64 time, u64 styleIDs, std::string_view metadata,
								   TransactionSuccessCallbackFunc onSuccess, TransactionFailureCallbackFunc onFailure)
{
	if (!BhopDatabaseService::IsReady())
	{
		return;
	}

	char query[2048];
	Transaction txn;

	// Always use UUID insert since all migrations must be applied for the plugin to run
	V_snprintf(query, sizeof(query), sql_times_insert, runUUID, steamID, courseID, modeID, styleIDs, time, metadata.data());
	txn.queries.push_back(query);
	if (styleIDs != 0)
	{
		BhopDatabaseService::GetDatabaseConnection()->ExecuteTransaction(txn, OnGenericTxnSuccess, OnGenericTxnFailure);
	}
	else
	{
		// Get Top 2 PBs
		V_snprintf(query, sizeof(query), sql_getpb, courseID, steamID, modeID, styleIDs, 2);
		txn.queries.push_back(query);
		// Get Rank
		V_snprintf(query, sizeof(query), sql_getmaprank, courseID, modeID, steamID, courseID, modeID);
		txn.queries.push_back(query);
		// Get Number of Players with Times
		V_snprintf(query, sizeof(query), sql_getlowestmaprank, courseID, modeID);
		txn.queries.push_back(query);

		BhopDatabaseService::GetDatabaseConnection()->ExecuteTransaction(txn, onSuccess, onFailure);
	}
}
