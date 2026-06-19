#include "bhop_db.h"
#include "bhop/mode/bhop_mode.h"
#include "queries/modes.h"
#include "vendor/sql_mm/src/public/sql_mm.h"

using namespace Bhop::Database;

void BhopDatabaseService::UpdateModeIDs()
{
	if (!BhopDatabaseService::IsReady())
	{
		return;
	}
	// clang-format off
	BhopDatabaseService::GetDatabaseConnection()->Query(sql_modes_fetch_all,
		[](ISQLQuery *query)
		{
			auto resultSet = query->GetResultSet();
			while (resultSet->FetchRow())
			{
				Bhop::mode::UpdateModeDatabaseID(query->GetResultSet()->GetString(1), query->GetResultSet()->GetInt(0));
			}
		});
	// clang-format on
}

void BhopDatabaseService::InsertAndUpdateModeIDs(CUtlString modeName, CUtlString shortName)
{
	if (!BhopDatabaseService::IsReady())
	{
		return;
	}
	Transaction txn;
	char query[2048];
	switch (BhopDatabaseService::GetDatabaseType())
	{
		case DatabaseType::SQLite:
		{
			V_snprintf(query, sizeof(query), sqlite_modes_insert, modeName.Get(), shortName.Get());
			break;
		}
		case DatabaseType::MySQL:
		{
			V_snprintf(query, sizeof(query), mysql_modes_insert, modeName.Get(), shortName.Get());
			break;
		}
		default:
		{
			// Should never happen.
			query[0] = 0;
		}
	}
	txn.queries.push_back(query);

	V_snprintf(query, sizeof(query), sql_modes_findid, modeName.Get());
	txn.queries.push_back(query);
	// clang-format off
	BhopDatabaseService::GetDatabaseConnection()->ExecuteTransaction(
		txn, 
		[modeName](std::vector<ISQLQuery *> queries) 
		{
			auto resultSet = queries[1]->GetResultSet();
			while (resultSet->FetchRow())
			{
				Bhop::mode::UpdateModeDatabaseID(modeName, queries[1]->GetResultSet()->GetInt(0));
			}
		},
		OnGenericTxnFailure);
	// clang-format on
}
