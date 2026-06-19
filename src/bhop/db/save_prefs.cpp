#include "bhop_db.h"
#include "bhop/option/bhop_option.h"

#include "vendor/sql_mm/src/public/sql_mm.h"

#include "queries/players.h"

void BhopDatabaseService::SavePrefs(CUtlString prefs)
{
	if (!BhopDatabaseService::IsReady() || !this->IsSetup())
	{
		return;
	}
	u64 steamID64 = this->player->GetSteamId64();
	std::string cleanedPrefs = BhopDatabaseService::GetDatabaseConnection()->Escape(prefs);

	Transaction txn;

	CUtlString query;
	query.Format(sql_players_set_prefs, prefs.Get(), steamID64);

	txn.queries.push_back(query.Get());

	BhopDatabaseService::GetDatabaseConnection()->ExecuteTransaction(txn, OnGenericTxnSuccess, OnGenericTxnFailure);
}
