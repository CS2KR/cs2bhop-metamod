#include "bhop_db.h"
#include "vendor/sql_mm/src/public/sql_mm.h"
#include "queries/courses.h"

void BhopDatabaseService::FindFirstCourseByMapName(CUtlString mapName, TransactionSuccessCallbackFunc onSuccess,
												   TransactionFailureCallbackFunc onFailure)
{
	auto cleanMapName = BhopDatabaseService::GetDatabaseConnection()->Escape(mapName.Get());

	char query[2048];
	V_snprintf(query, sizeof(query), sql_mapcourses_findfirst_mapname, cleanMapName.c_str(), cleanMapName.c_str());

	Transaction txn;
	txn.queries.push_back(query);

	BhopDatabaseService::GetDatabaseConnection()->ExecuteTransaction(txn, onSuccess, onFailure);
}
