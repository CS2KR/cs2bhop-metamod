#include "common.h"
#include "api.h"

bool Bhop::API::DecodeModeString(std::string_view modeString, Mode &mode)
{
	if (BHOP_STREQI(modeString.data(), "128tick") || BHOP_STREQI(modeString.data(), "128t"))
	{
		mode = Mode::_128tick;
		return true;
	}
	else if (BHOP_STREQI(modeString.data(), "css") || BHOP_STREQI(modeString.data(), "css66tick") || BHOP_STREQI(modeString.data(), "css66"))
	{
		mode = Mode::CSS66;
		return true;
	}
	else
	{
		return false;
	}
}

bool Bhop::API::DecodeStyleString(std::string_view styleString, Style &style)
{
	if (BHOP_STREQI(styleString.data(), "lowgrav") || BHOP_STREQI(styleString.data(), "lg"))
	{
		style = Style::LowGrav;
		return true;
	}
	else
	{
		return false;
	}
}

bool Bhop::API::PlayerInfo::FromJson(const Json &json)
{
	if (!json.Get("id", this->id))
	{
		std::string id;

		if (!json.Get("id", id))
		{
			return false;
		}

		if (!utils::ParseSteamID2(id, this->id))
		{
			META_CONPRINTF("[Bhop::Global] Failed to parse SteamID2.\n");
			return false;
		}
	}

	return json.Get("name", this->name);
}

bool Bhop::API::Player::FromJson(const Json &json)
{
	bool success = true;

	if (!json.Get("id", this->id))
	{
		std::string id;

		if (!json.Get("id", id))
		{
			return false;
		}

		if (!utils::ParseSteamID2(id, this->id))
		{
			META_CONPRINTF("[Bhop::Global] Failed to parse SteamID2.\n");
			return false;
		}
	}

	success &= json.Get("name", this->name);
	success &= json.Get("is_banned", this->isBanned);

	return success;
}

bool Bhop::API::Map::FromJson(const Json &json)
{
	if (!json.Get("id", this->id))
	{
		return false;
	}

	if (!json.Get("workshop_id", this->workshopId))
	{
		return false;
	}

	if (!json.Get("name", this->name))
	{
		return false;
	}

	if (!json.Get("description", this->description))
	{
		return false;
	}

	std::string state;

	if (!json.Get("state", state))
	{
		return false;
	}

	if (!DecodeStateString(state, this->state))
	{
		return false;
	}

	if (!json.Get("vpk_checksum", this->vpkChecksum))
	{
		return false;
	}

	if (!json.Get("mappers", this->mappers))
	{
		return false;
	}

	if (!json.Get("courses", this->courses))
	{
		return false;
	}

	if (!json.Get("approved_at", this->approvedAt))
	{
		return false;
	}

	return true;
}

bool Bhop::API::Map::DecodeStateString(std::string_view stateString, State &state)
{
	if (stateString == "invalid")
	{
		state = Bhop::API::Map::State::Invalid;
	}
	else if (stateString == "in-testing")
	{
		state = Bhop::API::Map::State::InTesting;
	}
	else if (stateString == "approved")
	{
		state = Bhop::API::Map::State::Approved;
	}
	else
	{
		META_CONPRINTF("[Bhop::Global] `state` field has an unknown value.\n");
		return false;
	}

	return true;
}

bool Bhop::API::Map::Course::FromJson(const Json &json)
{
	// clang-format off
	return json.Get("id", this->id)
		&& json.Get("name", this->name)
		&& json.Get("description", this->description)
		&& json.Get("mappers", this->mappers)
		&& json.Get("filters", this->filters);
	// clang-format on
}

bool Bhop::API::Map::Course::Filters::FromJson(const Json &json)
{
	bool success = json.Get("128tick", this->_128tick);
	json.Get("CSS", this->css66) || json.Get("CSS66tick", this->css66);
	return success;
}

bool Bhop::API::Map::Course::Filter::FromJson(const Json &json)
{
	if (!json.Get("id", this->id))
	{
		return false;
	}

	std::string tier;

	if (!json.Get("tier", tier))
	{
		return false;
	}

	if (!DecodeTierString(tier, this->tier))
	{
		return false;
	}

	std::string state;

	if (!json.Get("state", state))
	{
		return false;
	}

	if (!DecodeStateString(state, this->state))
	{
		return false;
	}

	if (!json.Get("notes", this->notes))
	{
		return false;
	}

	return true;
}

bool Bhop::API::Map::Course::Filter::DecodeTierString(std::string_view tierString, Tier &tier)
{
	if (tierString == "very-easy")
	{
		tier = Bhop::API::Map::Course::Filter::Tier::VeryEasy;
	}
	else if (tierString == "easy")
	{
		tier = Bhop::API::Map::Course::Filter::Tier::Easy;
	}
	else if (tierString == "medium")
	{
		tier = Bhop::API::Map::Course::Filter::Tier::Medium;
	}
	else if (tierString == "advanced")
	{
		tier = Bhop::API::Map::Course::Filter::Tier::Advanced;
	}
	else if (tierString == "hard")
	{
		tier = Bhop::API::Map::Course::Filter::Tier::Hard;
	}
	else if (tierString == "very-hard")
	{
		tier = Bhop::API::Map::Course::Filter::Tier::VeryHard;
	}
	else if (tierString == "extreme")
	{
		tier = Bhop::API::Map::Course::Filter::Tier::Extreme;
	}
	else if (tierString == "death")
	{
		tier = Bhop::API::Map::Course::Filter::Tier::Death;
	}
	else if (tierString == "unfeasible")
	{
		tier = Bhop::API::Map::Course::Filter::Tier::Unfeasible;
	}
	else if (tierString == "impossible")
	{
		tier = Bhop::API::Map::Course::Filter::Tier::Impossible;
	}
	else
	{
		META_CONPRINTF("[Bhop::Global] `tier` field has an unknown tierString.\n");
		return false;
	}

	return true;
}

bool Bhop::API::Map::Course::Filter::DecodeStateString(std::string_view stateString, State &state)
{
	if (stateString == "unranked")
	{
		state = Bhop::API::Map::Course::Filter::State::Unranked;
	}
	else if (stateString == "pending")
	{
		state = Bhop::API::Map::Course::Filter::State::Pending;
	}
	else if (stateString == "ranked")
	{
		state = Bhop::API::Map::Course::Filter::State::Ranked;
	}
	else
	{
		META_CONPRINTF("[Bhop::Global] `state` field has an unknown value.\n");
		return false;
	}

	return true;
}

bool Bhop::API::Record::FromJson(const Json &json)
{
	if (!json.Get("id", this->id))
	{
		return false;
	}

	if (!json.Get("player", this->player))
	{
		return false;
	}

	if (!json.Get("map", this->map))
	{
		return false;
	}

	if (!json.Get("course", this->course))
	{
		return false;
	}

	std::string mode = "";

	if (!json.Get("mode", mode))
	{
		return false;
	}

	if (!DecodeModeString(mode, this->mode))
	{
		return false;
	}

	if (!json.Get("time", this->time))
	{
		return false;
	}

	if (json.Get("rank", this->rank))
	{
		if (!json.Get("points", this->points))
		{
			return false;
		}
		if (!json.Get("max_rank", this->maxRank))
		{
			return false;
		}
	}

	return true;
}

bool Bhop::API::Record::MapInfo::FromJson(const Json &json)
{
	return json.Get("id", this->id) && json.Get("name", this->name);
}

bool Bhop::API::Record::CourseInfo::FromJson(const Json &json)
{
	return json.Get("id", this->id) && json.Get("name", this->name);
}
