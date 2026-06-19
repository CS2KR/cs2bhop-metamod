#include "bhop_db.h"
#include "bhop/style/bhop_style.h"
#include "queries/styles.h"
#include "vendor/sql_mm/src/public/sql_mm.h"

using namespace Bhop::Database;

void BhopDatabaseService::UpdateStyleIDs()
{
	if (!BhopDatabaseService::IsReady())
	{
		return;
	}
	// clang-format off
	BhopDatabaseService::GetDatabaseConnection()->Query(sql_styles_fetch_all,
		[](ISQLQuery *query)
		{
			auto resultSet = query->GetResultSet();
			while (resultSet->FetchRow())
			{
				Bhop::style::UpdateStyleDatabaseID(query->GetResultSet()->GetString(1), query->GetResultSet()->GetInt(0));
			}
		});
	// clang-format on
}

void BhopDatabaseService::InsertAndUpdateStyleIDs(CUtlString styleName, CUtlString shortName)
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
			V_snprintf(query, sizeof(query), sqlite_styles_insert, styleName.Get(), shortName.Get());
			break;
		}
		case DatabaseType::MySQL:
		{
			V_snprintf(query, sizeof(query), mysql_styles_insert, styleName.Get(), shortName.Get());
			break;
		}
		default:
		{
			// Should never happen.
			query[0] = 0;
		}
	}
	txn.queries.push_back(query);

	V_snprintf(query, sizeof(query), sql_styles_findid, styleName.Get());
	txn.queries.push_back(query);
	// clang-format off
	BhopDatabaseService::GetDatabaseConnection()->ExecuteTransaction(
		txn, 
		[styleName](std::vector<ISQLQuery *> queries) 
		{
			auto resultSet = queries[1]->GetResultSet();
			if (resultSet->FetchRow())
			{
				Bhop::style::UpdateStyleDatabaseID(styleName, queries[1]->GetResultSet()->GetInt(0));
			}
		},
		OnGenericTxnFailure);
	// clang-format on
}
