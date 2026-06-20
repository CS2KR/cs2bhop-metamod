#pragma once

#include <string>

#include "common.h"
#include "vendor/json/single_include/nlohmann/json.hpp"

namespace Bhop::zoning
{
	enum class FakeZoneExportResult
	{
		Success,
		NoMapName,
		OfficialMappingApi,
		NoDetectedPair,
		WriteFailed,
	};

	class BhopFakeZoning
	{
	public:
		static bool HasConfiguredTriggerName(const CUtlVector<CUtlString> &names, const std::string &triggerName);
		static std::string NormalizeTriggerName(const char *name);
		static void AddUniqueTriggerName(CUtlVector<CUtlString> &names, const char *name);
		static void LoadConfig(CUtlVector<CUtlString> &startNames, CUtlVector<CUtlString> &endNames);
		static bool IsStartZoneName(const std::string &name, const CUtlVector<CUtlString> &configuredNames);
		static bool IsEndZoneName(const std::string &name, const CUtlVector<CUtlString> &configuredNames);
		static bool IsStopTriggerName(const std::string &name);
		static bool IsResetTriggerName(const std::string &name);
		static FakeZoneExportResult ExportConfig(char *relativePath, size_t relativePathSize, i32 mapApiVersion,
												 const CUtlVector<CUtlString> &detectedStartNames,
												 const CUtlVector<CUtlString> &detectedEndNames);

	private:
		static void AddConfiguredTriggerName(CUtlVector<CUtlString> &names, const nlohmann::json &json, const char *key);
		static void AddZoneExportKeys(nlohmann::json &json, const char *singleKey, const char *arrayKey,
									  const CUtlVector<CUtlString> &names);
		static void LoadTriggerAliases(const char *path, CUtlVector<CUtlString> &startNames, CUtlVector<CUtlString> &endNames);
	};
} // namespace Bhop::zoning
