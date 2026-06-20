#include "bhop_zoning.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <vector>

#include "bhop/mappingapi/bhop_mappingapi.h"
#include "utils/utils.h"

using namespace Bhop::zoning;

bool BhopFakeZoning::HasConfiguredTriggerName(const CUtlVector<CUtlString> &names, const std::string &triggerName)
{
	FOR_EACH_VEC(names, i)
	{
		if (names[i].IsEqual_FastCaseInsensitive(triggerName.c_str()))
		{
			return true;
		}
	}
	return false;
}

std::string BhopFakeZoning::NormalizeTriggerName(const char *name)
{
	std::string normalized(name ? name : "");
	std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) { return std::tolower(c); });
	return normalized;
}

void BhopFakeZoning::AddUniqueTriggerName(CUtlVector<CUtlString> &names, const char *name)
{
	if (!name || !name[0] || HasConfiguredTriggerName(names, name))
	{
		return;
	}
	names.AddToTail(name);
}

void BhopFakeZoning::AddConfiguredTriggerName(CUtlVector<CUtlString> &names, const nlohmann::json &json, const char *key)
{
	if (!json.contains(key))
	{
		return;
	}
	const nlohmann::json &value = json[key];
	if (value.is_string())
	{
		std::string name = value.get<std::string>();
		AddUniqueTriggerName(names, name.c_str());
		return;
	}
	if (!value.is_array())
	{
		return;
	}
	for (const nlohmann::json &entry : value)
	{
		if (!entry.is_string())
		{
			continue;
		}
		std::string name = entry.get<std::string>();
		AddUniqueTriggerName(names, name.c_str());
	}
}

void BhopFakeZoning::AddZoneExportKeys(nlohmann::json &json, const char *singleKey, const char *arrayKey,
									   const CUtlVector<CUtlString> &names)
{
	if (names.Count() <= 0)
	{
		return;
	}

	json[singleKey] = names[0].Get();

	if (names.Count() <= 1)
	{
		return;
	}

	json[arrayKey] = nlohmann::json::array();
	FOR_EACH_VEC(names, i)
	{
		json[arrayKey].push_back(names[i].Get());
	}
}

void BhopFakeZoning::LoadTriggerAliases(const char *path, CUtlVector<CUtlString> &startNames, CUtlVector<CUtlString> &endNames)
{
	std::ifstream file(path);
	if (!file)
	{
		return;
	}

	std::stringstream buffer;
	buffer << file.rdbuf();
	auto json = nlohmann::json::parse(buffer.str(), nullptr, false);
	if (json.is_discarded() || !json.is_object())
	{
		META_CONPRINTF("[Bhop::MapAPI] Failed to parse fake-zone trigger config: %s\n", path);
		return;
	}

	AddConfiguredTriggerName(startNames, json, "MapStartTrigger");
	AddConfiguredTriggerName(endNames, json, "MapEndTrigger");
	AddConfiguredTriggerName(startNames, json, "MapStartTriggers");
	AddConfiguredTriggerName(endNames, json, "MapEndTriggers");
	AddConfiguredTriggerName(startNames, json, "startTrigger");
	AddConfiguredTriggerName(endNames, json, "endTrigger");
	AddConfiguredTriggerName(startNames, json, "startTriggers");
	AddConfiguredTriggerName(endNames, json, "endTriggers");
}

void BhopFakeZoning::LoadConfig(CUtlVector<CUtlString> &startNames, CUtlVector<CUtlString> &endNames)
{
	startNames.RemoveAll();
	endNames.RemoveAll();

	bool hasMapName = false;
	CUtlString currentMap = g_pBhopUtils->GetCurrentMapName(&hasMapName);
	if (!hasMapName)
	{
		return;
	}

	char cs2bhopPath[1024];
	g_SMAPI->PathFormat(cs2bhopPath, sizeof(cs2bhopPath), "%s/addons/cs2bhop/zones/%s.json", g_SMAPI->GetBaseDir(), currentMap.Get());
	LoadTriggerAliases(cs2bhopPath, startNames, endNames);

	char sharpTimerPath[1024];
	g_SMAPI->PathFormat(sharpTimerPath, sizeof(sharpTimerPath), "%s/cfg/SharpTimer/MapData/%s.json", g_SMAPI->GetBaseDir(), currentMap.Get());
	LoadTriggerAliases(sharpTimerPath, startNames, endNames);
}

bool BhopFakeZoning::IsStartZoneName(const std::string &name, const CUtlVector<CUtlString> &configuredNames)
{
	if (name == "map_start" || name == "s1_start" || name == "stage1_start" || name == "timer_startzone" || name == "trigger_startzone"
		|| name == "zone_start")
	{
		return true;
	}
	if (HasConfiguredTriggerName(configuredNames, name))
	{
		return true;
	}
	static const std::regex stage1Pattern(R"(^(?:s|stage)1_start$)", std::regex_constants::icase);
	return std::regex_match(name, stage1Pattern);
}

bool BhopFakeZoning::IsEndZoneName(const std::string &name, const CUtlVector<CUtlString> &configuredNames)
{
	if (name == "map_end" || name == "timer_endzone" || name == "trigger_endzone" || name == "zone_end")
	{
		return true;
	}
	return HasConfiguredTriggerName(configuredNames, name);
}

bool BhopFakeZoning::IsStopTriggerName(const std::string &name)
{
	return name == "st_stop" || name == "surftimer_stop" || name == "timer_stop";
}

bool BhopFakeZoning::IsResetTriggerName(const std::string &name)
{
	return name == "st_reset" || name == "surftimer_reset" || name == "timer_reset";
}

FakeZoneExportResult BhopFakeZoning::ExportConfig(char *relativePath, size_t relativePathSize, i32 mapApiVersion,
												  const CUtlVector<CUtlString> &detectedStartNames,
												  const CUtlVector<CUtlString> &detectedEndNames)
{
	bool hasMapName = false;
	CUtlString currentMap = g_pBhopUtils->GetCurrentMapName(&hasMapName);
	if (!hasMapName)
	{
		return FakeZoneExportResult::NoMapName;
	}
	if (mapApiVersion != BHOP_NO_MAPAPI_VERSION)
	{
		return FakeZoneExportResult::OfficialMappingApi;
	}

	if (detectedStartNames.Count() <= 0 || detectedEndNames.Count() <= 0)
	{
		return FakeZoneExportResult::NoDetectedPair;
	}

	nlohmann::json json;
	json["schema"] = "cs2bhop.fake-zones";
	json["schemaVersion"] = 1;
	json["map"] = currentMap.Get();
	json["source"] = "trigger_multiple.targetname";
	json["compatibility"] = {{"sharpTimerTriggerAliases", true}, {"coordinateBoxes", false}, {"officialMappingApi", false}};
	AddZoneExportKeys(json, "MapStartTrigger", "MapStartTriggers", detectedStartNames);
	AddZoneExportKeys(json, "MapEndTrigger", "MapEndTriggers", detectedEndNames);

	char path[1024];
	g_SMAPI->PathFormat(path, sizeof(path), "addons/cs2bhop/zones/%s.json", currentMap.Get());
	std::string output = json.dump(2);
	output.push_back('\n');
	std::vector<char> buffer(output.begin(), output.end());

	if (relativePath && relativePathSize > 0)
	{
		V_snprintf(relativePath, relativePathSize, "%s", path);
	}
	return utils::WriteBufferToFile(path, buffer) ? FakeZoneExportResult::Success : FakeZoneExportResult::WriteFailed;
}
