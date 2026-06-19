#include "bhop_db.h"
#include "queries/maps.h"
#include "vendor/sql_mm/src/public/sql_mm.h"

/*
	Inserts the map information into the database.
	Retrieves the MapID of the map and stores it in a global variable.
*/

using namespace Bhop::Database;

static_global bool mapSetUp = false;
i32 BhopDatabaseService::currentMapID {};

bool BhopDatabaseService::IsMapSetUp()
{
	return mapSetUp;
}

void BhopDatabaseService::SetupMap()
{
	mapSetUp = false;
	if (!BhopDatabaseService::IsReady())
	{
		META_CONPRINTF("[Bhop::DB] Warning: SetupMap called too early.\n");
		return;
	}

	Transaction txn;
	char query[2048];
	CUtlString mapName = g_pBhopUtils->GetServerGlobals()->mapname.ToCStr();
	auto escapedMapName = BhopDatabaseService::GetDatabaseConnection()->Escape(mapName.Get());
	auto databaseType = BhopDatabaseService::GetDatabaseType();
	switch (databaseType)
	{
		case DatabaseType::SQLite:
		{
			V_snprintf(query, sizeof(query), sqlite_maps_insert, escapedMapName.c_str());
			txn.queries.push_back(query);
			V_snprintf(query, sizeof(query), sqlite_maps_update, escapedMapName.c_str());
			txn.queries.push_back(query);
			break;
		}
		case DatabaseType::MySQL:
		{
			V_snprintf(query, sizeof(query), mysql_maps_upsert, escapedMapName.c_str());
			txn.queries.push_back(query);
			break;
		}
		default:
		{
			// This shouldn't happen.
			query[0] = 0;
			break;
		}
	}

	V_snprintf(query, sizeof(query), sql_maps_findid, escapedMapName.c_str(), escapedMapName.c_str());
	txn.queries.push_back(query);
	// clang-format off
	BhopDatabaseService::GetDatabaseConnection()->ExecuteTransaction(
		txn, 
		[databaseType, mapName](std::vector<ISQLQuery *> queries) 
		{
			auto currentMapName = g_pBhopUtils->GetServerGlobals()->mapname.ToCStr();
			if (!BHOP_STREQ(currentMapName, mapName.Get()))
			{
				META_CONPRINTF("[Bhop::DB] Failed to setup map, current map name %s doesn't match %s!\n", currentMapName, mapName.Get());
				return;
			}
			switch (databaseType)
			{
				case DatabaseType::SQLite:
				{
					auto resultSet = queries[2]->GetResultSet();
					if (resultSet->FetchRow())
					{
						BhopDatabaseService::currentMapID = resultSet->GetInt(0);
					}
					break;
				}
				case DatabaseType::MySQL:
				{
					auto resultSet = queries[1]->GetResultSet();
					if (resultSet->FetchRow())
					{
						BhopDatabaseService::currentMapID = resultSet->GetInt(0);
					}
					break;
				}
				default:
				{
					// This shouldn't happen.
					break;
				}
			}
			mapSetUp = true;
			META_CONPRINTF("[Bhop::DB] Map setup successful for %s, current map ID: %i\n", currentMapName, BhopDatabaseService::currentMapID);
			CALL_FORWARD(eventListeners, OnMapSetup);
		},
		OnGenericTxnFailure);
	// clang-format on
}
