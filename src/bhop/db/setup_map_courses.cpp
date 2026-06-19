#include "bhop_db.h"

#include "queries/courses.h"

#include "vendor/sql_mm/src/public/sql_mm.h"

using namespace Bhop::Database;

static_global bool coursesSetUp = false;

bool BhopDatabaseService::AreCoursesSetUp()
{
	return coursesSetUp;
}

void BhopDatabaseService::SetupCourses(CUtlVector<BhopCourseDescriptor *> &courses)
{
	coursesSetUp = false;

	char query[2048];
	Transaction txn;
	FOR_EACH_VEC(courses, i)
	{
		BhopCourseDescriptor *course = courses[i];
		std::string cleanCourseName = BhopDatabaseService::GetDatabaseConnection()->Escape(course->GetName());
		switch (databaseType)
		{
			case DatabaseType::SQLite:
			{
				V_snprintf(query, sizeof(query), sqlite_mapcourses_insert, BhopDatabaseService::GetMapID(), cleanCourseName.c_str(), course->id);
				txn.queries.push_back(query);
				break;
			}
			case DatabaseType::MySQL:
			{
				V_snprintf(query, sizeof(query), mysql_mapcourses_insert, BhopDatabaseService::GetMapID(), cleanCourseName.c_str(), course->id);
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
	}
	V_snprintf(query, sizeof(query), sql_mapcourses_findall, BhopDatabaseService::GetMapID());
	txn.queries.push_back(query);
	// clang-format off
	BhopDatabaseService::GetDatabaseConnection()->ExecuteTransaction(
		txn,
		[](std::vector<ISQLQuery *> queries) 
		{
			auto resultSet = queries.back()->GetResultSet();
			while (resultSet->FetchRow())
			{
				const char* name = resultSet->GetString(0);
				if (Bhop::course::UpdateCourseLocalID(name, resultSet->GetInt(1)))
				{
					META_CONPRINTF("[Bhop::DB] Course '%s' registered with ID %i\n", name, resultSet->GetInt(1));
				}
				else
				{
					META_CONPRINTF("[Bhop::DB] Warning: Course '%s' with ID %i has no ingame course registered!\n", name, resultSet->GetInt(1));
				}
			}
			coursesSetUp = true;
			CALL_FORWARD(eventListeners, OnCoursesSetup);
		},
		OnGenericTxnFailure);
	// clang-format on
}
