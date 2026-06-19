#include "bhop_db.h"
#include "vendor/sql_mm/src/public/sql_mm.h"

#include "queries/players.h"

void BhopDatabaseService::FindPlayerByAlias(CUtlString playerName, TransactionSuccessCallbackFunc onSuccess, TransactionFailureCallbackFunc onFailure)
{
	if (!BhopDatabaseService::IsReady())
	{
		return;
	}

	Transaction txn;
	char query[2048];

	// Get player's steamID through their alias.
	std::string cleanedPlayerName = BhopDatabaseService::GetDatabaseConnection()->Escape(playerName.Get());
	V_snprintf(query, sizeof(query), sql_players_searchbyalias, cleanedPlayerName.c_str(), cleanedPlayerName.c_str());
	txn.queries.push_back(query);

	BhopDatabaseService::GetDatabaseConnection()->ExecuteTransaction(txn, onSuccess, onFailure);
}
