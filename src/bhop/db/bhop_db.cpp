#include "bhop_db.h"
#include "vendor/sql_mm/src/public/sql_mm.h"

using namespace Bhop::Database;

Bhop::Database::DatabaseType BhopDatabaseService::databaseType;
ISQLConnection *BhopDatabaseService::databaseConnection;

CUtlVector<BhopDatabaseServiceEventListener *> BhopDatabaseService::eventListeners;

bool BhopDatabaseService::RegisterEventListener(BhopDatabaseServiceEventListener *eventListener)
{
	if (eventListeners.Find(eventListener) >= 0)
	{
		return false;
	}
	eventListeners.AddToTail(eventListener);
	return true;
}

bool BhopDatabaseService::UnregisterEventListener(BhopDatabaseServiceEventListener *eventListener)
{
	return eventListeners.FindAndRemove(eventListener);
}

void BhopDatabaseService::Init()
{
	BhopDatabaseService::SetupDatabase();
}

void BhopDatabaseService::Cleanup()
{
	if (databaseConnection)
	{
		databaseConnection->Destroy();
		databaseConnection = NULL;
	}
}
