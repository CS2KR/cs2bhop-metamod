#include "handshake.h"

bool Bhop::API::handshake::Hello::ToJson(Json &json) const
{
	// clang-format off
	return json.Set("plugin_version", PLUGIN_FULL_VERSION)
		&& json.Set("plugin_version_checksum", this->checksum)
		&& json.Set("map", this->currentMapName)
		&& json.Set("players", this->players);
	// clang-format on
}

bool Bhop::API::handshake::Hello::PlayerInfo::ToJson(Json &json) const
{
	return json.Set("id", this->id) && json.Set("name", this->name);
}

bool Bhop::API::handshake::HelloAck::FromJson(const Json &json)
{
	f64 heartbeatInterval;

	if (!json.Get("heartbeat_interval", heartbeatInterval))
	{
		return false;
	}

	this->heartbeatInterval = heartbeatInterval;

	return json.Get("map", this->mapInfo) && json.Get("modes", this->modes) && json.Get("styles", this->styles);
}

bool Bhop::API::handshake::HelloAck::ModeInfo::FromJson(const Json &json)
{
	std::string mode;

	if (!json.Get("mode", mode))
	{
		return false;
	}

	if (!Bhop::API::DecodeModeString(mode, this->mode))
	{
		return false;
	}

	return json.Get("linux_checksum", this->linuxChecksum) && json.Get("windows_checksum", this->windowsChecksum);
}

bool Bhop::API::handshake::HelloAck::StyleInfo::FromJson(const Json &json)
{
	std::string style;

	if (!json.Get("style", style))
	{
		return false;
	}

	if (!Bhop::API::DecodeStyleString(style, this->style))
	{
		return false;
	}

	return json.Get("linux_checksum", this->linuxChecksum) && json.Get("windows_checksum", this->windowsChecksum);
}
