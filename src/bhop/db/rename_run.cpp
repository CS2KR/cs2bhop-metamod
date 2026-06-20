#include "bhop_db.h"
#include "queries/times.h"
#include "vendor/sql_mm/src/public/sql_mm.h"

using namespace Bhop::Database;

void BhopDatabaseService::UpdateRunUUID(const char *oldUUID, const char *newUUID, TransactionSuccessCallbackFunc onSuccess,
										TransactionFailureCallbackFunc onFailure)
{
	if (!BhopDatabaseService::IsReady())
	{
		return;
	}

	char query[256];
	Transaction txn;
	V_snprintf(query, sizeof(query), sql_times_update_id, newUUID, oldUUID);
	txn.queries.push_back(query);
	BhopDatabaseService::GetDatabaseConnection()->ExecuteTransaction(txn, onSuccess ? onSuccess : OnGenericTxnSuccess,
																	 onFailure ? onFailure : OnGenericTxnFailure);
}
