#pragma once

#include <optional>
#include <string_view>

#include "version_gen.h"
#include "bhop/global/api.h"

namespace Bhop::API::handshake
{
	struct Hello
	{
		struct PlayerInfo
		{
			u64 id;
			std::string_view name;

			bool ToJson(Json &json) const;
		};

		std::string_view checksum;
		std::string_view currentMapName;
		std::unordered_map<u64, PlayerInfo> players;

		Hello(std::string_view checksum, std::string_view currentMapName) : checksum(checksum), currentMapName(currentMapName) {}

		void AddPlayer(u64 id, std::string_view name)
		{
			this->players[id] = {id, name};
		}

		bool ToJson(Json &json) const;
	};

	struct HelloAck
	{
		struct ModeInfo
		{
			Mode mode;
			std::string linuxChecksum {};
			std::string windowsChecksum {};

			bool FromJson(const Json &json);
		};

		struct StyleInfo
		{
			Style style;
			std::string linuxChecksum {};
			std::string windowsChecksum {};

			bool FromJson(const Json &json);
		};

		// seconds
		f64 heartbeatInterval {};
		std::optional<Bhop::API::Map> mapInfo {};
		std::vector<ModeInfo> modes {};
		std::vector<StyleInfo> styles {};

		bool FromJson(const Json &json);
	};
} // namespace Bhop::API::handshake
