// required for ws library
#ifdef _WIN32
#pragma comment(lib, "Ws2_32.Lib")
#pragma comment(lib, "Crypt32.Lib")
#endif

#include <string_view>

#include <ixwebsocket/IXNetSystem.h>

#include "common.h"
#include "cs2bhop.h"
#include "bhop/bhop.h"
#include "bhop_global.h"
#include "bhop/global/handshake.h"
#include "bhop/global/events.h"
#include "bhop/mode/bhop_mode.h"
#include "bhop/option/bhop_option.h"
#include "bhop/timer/bhop_timer.h"

#include <vendor/ClientCvarValue/public/iclientcvarvalue.h>

extern IClientCvarValue *g_pClientCvarValue;

bool BhopGlobalService::IsAvailable()
{
	return BhopGlobalService::state.load() == BhopGlobalService::State::HandshakeCompleted;
}

bool BhopGlobalService::MayBecomeAvailable()
{
	return BhopGlobalService::state.load() != BhopGlobalService::State::Disconnected;
}

void BhopGlobalService::UpdateRecordCache()
{
	u16 currentMapID = 0;

	{
		std::unique_lock lock(BhopGlobalService::currentMap.mutex);

		if (!BhopGlobalService::currentMap.data.has_value())
		{
			return;
		}

		currentMapID = BhopGlobalService::currentMap.data->id;
	}

	std::string_view event("want-world-records-for-cache");
	Bhop::API::events::WantWorldRecordsForCache data {currentMapID};
	auto callback = [](Bhop::API::events::WorldRecordsForCache &records)
	{
		for (const Bhop::API::Record &record : records.records)
		{
			const BhopCourseDescriptor *course = Bhop::course::GetCourseByGlobalCourseID(record.course.id);

			if (course == nullptr)
			{
				META_CONPRINTF("[Bhop::Global] Could not find current course?\n");
				continue;
			}

			PluginId modeID = Bhop::mode::GetModeInfo(record.mode).id;

			BhopTimerService::InsertRecordToCache(record.time, course, modeID, true);
		}
	};

	switch (BhopGlobalService::state.load())
	{
		case BhopGlobalService::State::HandshakeCompleted:
			BhopGlobalService::SendMessage(event, data, callback);
			break;

		case BhopGlobalService::State::Disconnected:
			break;

		default:
			BhopGlobalService::AddWhenConnectedCallback([=]() { BhopGlobalService::SendMessage(event, data, callback); });
	}
}

void BhopGlobalService::Init()
{
	if (BhopGlobalService::state.load() != BhopGlobalService::State::Uninitialized)
	{
		return;
	}

	META_CONPRINTF("[Bhop::Global] Initializing GlobalService...\n");

	std::string url = BhopOptionService::GetOptionStr("apiUrl", BhopOptionService::GetOptionStr("apiUrl", "https://api.placeholder.org"));
	std::string_view key = BhopOptionService::GetOptionStr("apiKey");

	if (url.empty())
	{
		META_CONPRINTF("[Bhop::Global] `apiUrl` is empty. GlobalService will be disabled.\n");
		BhopGlobalService::state.store(BhopGlobalService::State::Disconnected);
		return;
	}

	if (url.size() < 4 || url.substr(0, 4) != "http")
	{
		META_CONPRINTF("[Bhop::Global] `apiUrl` is invalid. GlobalService will be disabled.\n");
		BhopGlobalService::state.store(BhopGlobalService::State::Disconnected);
		return;
	}

	if (key.empty())
	{
		META_CONPRINTF("[Bhop::Global] `apiKey` is empty. GlobalService will be disabled.\n");
		BhopGlobalService::state.store(BhopGlobalService::State::Disconnected);
		return;
	}

	url.replace(0, 4, "ws");

	if (url.size() < 9 || url.substr(url.size() - 9) != "/auth/cs2")
	{
		if (url.substr(url.size() - 1) != "/")
		{
			url += "/";
		}

		url += "auth/cs2";
	}

	ix::initNetSystem();

	BhopGlobalService::socket = new ix::WebSocket();
	BhopGlobalService::socket->setUrl(url);

	// ix::WebSocketHttpHeaders headers;
	// headers["Authorization"] = "Bearer ";
	// headers["Authorization"] += key;
	BhopGlobalService::socket->setExtraHeaders({
		{"Authorization", std::string("Bearer ") + key.data()},
	});

	BhopGlobalService::socket->setOnMessageCallback(BhopGlobalService::OnWebSocketMessage);
	BhopGlobalService::socket->start();

	BhopGlobalService::EnforceConVars();

	BhopGlobalService::state.store(BhopGlobalService::State::Initialized);
}

void BhopGlobalService::Cleanup()
{
	if (BhopGlobalService::state.load() == BhopGlobalService::State::Uninitialized)
	{
		return;
	}

	META_CONPRINTF("[Bhop::Global] Cleaning up GlobalService...\n");

	BhopGlobalService::RestoreConVars();

	if (BhopGlobalService::socket != nullptr)
	{
		BhopGlobalService::socket->stop();
		delete BhopGlobalService::socket;
		BhopGlobalService::socket = nullptr;
	}

	BhopGlobalService::state.store(BhopGlobalService::State::Uninitialized);

	ix::uninitNetSystem();
}

void BhopGlobalService::OnServerGamePostSimulate()
{
	std::vector<std::function<void()>> callbacks;

	{
		std::unique_lock lock(BhopGlobalService::mainThreadCallbacks.mutex, std::defer_lock);

		if (lock.try_lock())
		{
			BhopGlobalService::mainThreadCallbacks.queue.swap(callbacks);

			if (BhopGlobalService::state.load() == BhopGlobalService::State::HandshakeCompleted)
			{
				callbacks.reserve(callbacks.size() + BhopGlobalService::mainThreadCallbacks.whenConnectedQueue.size());
				callbacks.insert(callbacks.end(), BhopGlobalService::mainThreadCallbacks.whenConnectedQueue.begin(),
								 BhopGlobalService::mainThreadCallbacks.whenConnectedQueue.end());
				BhopGlobalService::mainThreadCallbacks.whenConnectedQueue.clear();
			}
		}
	}

	for (const std::function<void()> &callback : callbacks)
	{
		callback();
	}
}

void BhopGlobalService::OnActivateServer()
{
	switch (BhopGlobalService::state.load())
	{
		case BhopGlobalService::State::Uninitialized:
			BhopGlobalService::Init();
			break;

		case BhopGlobalService::State::HandshakeCompleted:
		{
			bool mapNameOk = false;
			CUtlString currentMapName = g_pBhopUtils->GetCurrentMapName(&mapNameOk);

			if (!mapNameOk)
			{
				META_CONPRINTF("[Bhop::Global] Failed to get current map name. Cannot send `map-change` event.\n");
				return;
			}

			std::string_view event("map-change");
			Bhop::API::events::MapChange data(currentMapName.Get());

			// clang-format off
			BhopGlobalService::SendMessage(event, data, [currentMapName](Bhop::API::events::MapInfo& mapInfo)
			{
				if (mapInfo.data.has_value())
				{
					META_CONPRINTF("[Bhop::Global] %s is approved.\n", mapInfo.data->name.c_str());
				}
				else
				{
					META_CONPRINTF("[Bhop::Global] %s is not approved.\n", currentMapName.Get());
				}

				{
					std::unique_lock lock(BhopGlobalService::currentMap.mutex);
					BhopGlobalService::currentMap.data = std::move(mapInfo.data);
				}
			});
			// clang-format on
		}
		break;
	}
}

void BhopGlobalService::OnPlayerAuthorized()
{
	if (!this->player->IsConnected())
	{
		return;
	}

	u64 steamID = this->player->GetSteamId64();

	std::string_view event("player-join");
	Bhop::API::events::PlayerJoin data;
	data.steamID = steamID;
	data.name = this->player->GetName();
	data.ipAddress = this->player->GetIpAddress();

	auto callback = [steamID](Bhop::API::events::PlayerJoinAck &ack)
	{
		BhopPlayer *player = g_pBhopPlayerManager->SteamIdToPlayer(steamID);

		if (player == nullptr)
		{
			return;
		}

		player->globalService->playerInfo.isBanned = ack.isBanned;
		player->optionService->InitializeGlobalPrefs(ack.preferences.ToString());

		// clang-format off
		u16 currentMapID = BhopGlobalService::WithCurrentMap([](const Bhop::API::Map* currentMap)
		{
			return (currentMap == nullptr) ? 0 : currentMap->id;
		});
		// clang-format on

		if (currentMapID != 0)
		{
			std::string_view event("want-player-records");
			Bhop::API::events::WantPlayerRecords data;
			data.mapID = currentMapID;
			data.playerID = steamID;

			auto callback = [steamID](Bhop::API::events::PlayerRecords &records)
			{
				BhopPlayer *player = g_pBhopPlayerManager->SteamIdToPlayer(steamID);

				if (player == nullptr)
				{
					return;
				}

				for (const Bhop::API::Record &record : records.records)
				{
					const BhopCourseDescriptor *course = Bhop::course::GetCourseByGlobalCourseID(record.course.id);

					if (course == nullptr)
					{
						continue;
					}

					PluginId modeID = Bhop::mode::GetModeInfo(record.mode).id;

					if (record.points != 0)
					{
						player->timerService->InsertPBToCache(record.time, course, modeID, true, "", record.points);
					}
				}
			};

			switch (BhopGlobalService::state.load())
			{
				case BhopGlobalService::State::HandshakeInitiated:
					BhopGlobalService::AddWhenConnectedCallback([=]() { BhopGlobalService::SendMessage(event, data, callback); });
					break;

				case BhopGlobalService::State::HandshakeCompleted:
					BhopGlobalService::SendMessage(event, data, callback);
					break;

				case BhopGlobalService::State::Disconnected:
					META_CONPRINTF("[Bhop::Global] Cannot fetch player PBs if we are disconnected from the API. (state=%i)\n");
					break;

				default:
					// handshake hasn't been initiated yet, so by the time that
					// happens, player will be sent as part of the handshake
					break;
			}
		}
	};

	switch (BhopGlobalService::state.load())
	{
		case BhopGlobalService::State::HandshakeInitiated:
			BhopGlobalService::AddWhenConnectedCallback([=]() { BhopGlobalService::SendMessage(event, data, callback); });
			break;

		case BhopGlobalService::State::HandshakeCompleted:
			BhopGlobalService::SendMessage(event, data, callback);
			break;

		case BhopGlobalService::State::Disconnected:
			break;

		default:
			// handshake hasn't been initiated yet, so by the time that
			// happens, player will be sent as part of the handshake
			break;
	}
}

void BhopGlobalService::OnClientDisconnect()
{
	if (!this->player->IsConnected() || !this->player->IsAuthenticated())
	{
		return;
	}

	if (BhopGlobalService::state.load() != BhopGlobalService::State::HandshakeCompleted)
	{
		return;
	}

	CUtlString getPrefsError;
	CUtlString getPrefsResult;
	this->player->optionService->GetPreferencesAsJSON(&getPrefsError, &getPrefsResult);

	if (!getPrefsError.IsEmpty())
	{
		META_CONPRINTF("[Bhop::Global] Failed to get preferences: %s\n", getPrefsError.Get());
		META_CONPRINTF("[Bhop::Global] Not sending `player-leave` event.\n");
		return;
	}

	std::string_view event("player-leave");
	Bhop::API::events::PlayerLeave data;
	data.steamID = this->player->GetSteamId64();
	data.name = this->player->GetName();
	data.preferences = Json(getPrefsResult.Get());

	switch (BhopGlobalService::state.load())
	{
		case BhopGlobalService::State::HandshakeCompleted:
			BhopGlobalService::SendMessage(event, data);
			break;

		default:
			break;
	}
}

void BhopGlobalService::OnWebSocketMessage(const ix::WebSocketMessagePtr &message)
{
	switch (message->type)
	{
		case ix::WebSocketMessageType::Open:
		{
			META_CONPRINTF("[Bhop::Global] Connection established!\n");
			BhopGlobalService::state.store(BhopGlobalService::State::Connected);
			BhopGlobalService::AddMainThreadCallback(BhopGlobalService::InitiateHandshake);
		}
		break;

		case ix::WebSocketMessageType::Close:
		{
			META_CONPRINTF("[Bhop::Global] Connection closed (code %i): %s\n", message->closeInfo.code, message->closeInfo.reason.c_str());

			switch (message->closeInfo.code)
			{
				case 1000 /* NORMAL */:     /* fall-through */
				case 1001 /* GOING AWAY */: /* fall-through */
				case 1006 /* ABNORMAL */:
				{
					BhopGlobalService::socket->enableAutomaticReconnection();
					BhopGlobalService::state.store(BhopGlobalService::State::DisconnectedButWorthRetrying);
				}
				break;

				case 1008 /* POLICY VIOLATION */:
				{
					if (BhopGlobalService::state.load() == BhopGlobalService::State::HandshakeCompleted
						&& message->closeInfo.reason.find("heartbeat") != message->closeInfo.reason.size())
					{
						BhopGlobalService::socket->enableAutomaticReconnection();
						BhopGlobalService::state.store(BhopGlobalService::State::DisconnectedButWorthRetrying);
					}
					else
					{
						BhopGlobalService::socket->disableAutomaticReconnection();
						BhopGlobalService::state.store(BhopGlobalService::State::Disconnected);
					}
				}
				break;

				default:
				{
					BhopGlobalService::socket->disableAutomaticReconnection();
					BhopGlobalService::state.store(BhopGlobalService::State::Disconnected);
				}
			}
		}
		break;

		case ix::WebSocketMessageType::Error:
		{
			META_CONPRINTF("[Bhop::Global] WebSocket error (code %i): %s\n", message->errorInfo.http_status, message->errorInfo.reason.c_str());

			switch (message->errorInfo.http_status)
			{
				case 401:
				{
					BhopGlobalService::socket->disableAutomaticReconnection();
					BhopGlobalService::state.store(BhopGlobalService::State::Disconnected);
				}
				break;

				case 429:
				{
					META_CONPRINTF("[Bhop::Global] API rate limit reached; increasing down reconnection delay...\n");
					BhopGlobalService::socket->enableAutomaticReconnection();
					BhopGlobalService::socket->setMinWaitBetweenReconnectionRetries(10'000 /* ms */);
					BhopGlobalService::socket->setMaxWaitBetweenReconnectionRetries(30'000 /* ms */);
					BhopGlobalService::state.store(BhopGlobalService::State::DisconnectedButWorthRetrying);
				}
				break;

				case 500: /* fall-through */
				case 502:
				{
					META_CONPRINTF("[Bhop::Global] API encountered an internal error\n");
					BhopGlobalService::socket->enableAutomaticReconnection();
					BhopGlobalService::socket->setMinWaitBetweenReconnectionRetries(10 * 60'000 /* ms */);
					BhopGlobalService::socket->setMaxWaitBetweenReconnectionRetries(30 * 60'000 /* ms */);
					BhopGlobalService::state.store(BhopGlobalService::State::DisconnectedButWorthRetrying);
				}
				break;

				default:
				{
					BhopGlobalService::socket->disableAutomaticReconnection();
					BhopGlobalService::state.store(BhopGlobalService::State::Disconnected);
				}
			}
		}
		break;

		case ix::WebSocketMessageType::Message:
		{
			META_CONPRINTF("[Bhop::Global] Received WebSocket message:\n-----\n%s\n------\n", message->str.c_str());

			Json payload(message->str);

			switch (BhopGlobalService::state.load())
			{
				case BhopGlobalService::State::HandshakeInitiated:
				{
					Bhop::API::handshake::HelloAck helloAck;

					if (!helloAck.FromJson(payload))
					{
						META_CONPRINTF("[Bhop::Global] Failed to decode 'HelloAck'\n");
						break;
					}

					BhopGlobalService::AddMainThreadCallback([ack = std::move(helloAck)]() mutable { BhopGlobalService::CompleteHandshake(ack); });
				}
				break;

				case BhopGlobalService::State::HandshakeCompleted:
				{
					if (!payload.IsValid())
					{
						META_CONPRINTF("[Bhop::Global] WebSocket message is not valid JSON.\n");
						break;
					}

					u32 messageID = 0;

					if (!payload.Get("id", messageID))
					{
						META_CONPRINTF("[Bhop::Global] Ignoring message without valid ID\n");
						break;
					}

					BhopGlobalService::AddMainThreadCallback([=]() { BhopGlobalService::ExecuteMessageCallback(messageID, payload); });
				}
				break;
			}
		}
		break;

		case ix::WebSocketMessageType::Ping:
		{
			META_CONPRINTF("[Bhop::Global] Ping!\n");
		}
		break;

		case ix::WebSocketMessageType::Pong:
		{
			META_CONPRINTF("[Bhop::Global] Pong!\n");
		}
		break;
	}
}

void BhopGlobalService::InitiateHandshake()
{
	bool mapNameOk = false;
	CUtlString currentMapName = g_pBhopUtils->GetCurrentMapName(&mapNameOk);

	if (!mapNameOk)
	{
		META_CONPRINTF("[Bhop::Global] Failed to get current map name. Cannot initiate handshake.\n");
		return;
	}

	std::string_view event("hello");
	Bhop::API::handshake::Hello data(g_BhopPlugin.GetMD5(), currentMapName.Get());

	for (Player *player : g_pPlayerManager->players)
	{
		if (player && player->IsAuthenticated())
		{
			data.AddPlayer(player->GetSteamId64(), player->GetName());
		}
	}

	BhopGlobalService::SendMessage(event, data);
	BhopGlobalService::state.store(State::HandshakeInitiated);
}

void BhopGlobalService::CompleteHandshake(Bhop::API::handshake::HelloAck &ack)
{
	BhopGlobalService::state.store(State::HandshakeCompleted);

	// clang-format off
	std::thread([heartbeatInterval = std::chrono::milliseconds(static_cast<i64>(ack.heartbeatInterval * 800))]()
	{
		while (BhopGlobalService::state.load() == State::HandshakeCompleted) {
			BhopGlobalService::socket->ping("");
			META_CONPRINTF("[Bhop::Global] Sent heartbeat. (interval=%is)\n", std::chrono::duration_cast<std::chrono::seconds>(heartbeatInterval).count());
			std::this_thread::sleep_for(heartbeatInterval);
		}
	}).detach();
	// clang-format on

	{
		std::unique_lock lock(BhopGlobalService::currentMap.mutex);
		BhopGlobalService::currentMap.data = std::move(ack.mapInfo);
	}

	{
		std::unique_lock lock(BhopGlobalService::globalModes.mutex);
		BhopGlobalService::globalModes.data = std::move(ack.modes);
	}

	{
		std::unique_lock lock(BhopGlobalService::globalStyles.mutex);
		BhopGlobalService::globalStyles.data = std::move(ack.styles);
	}

	META_CONPRINTF("[Bhop::Global] Completed handshake!\n");
}

void BhopGlobalService::ExecuteMessageCallback(u32 messageID, const Json &payload)
{
	std::function<void(u32, const Json &)> callback;

	{
		std::unique_lock lock(BhopGlobalService::messageCallbacks.mutex);
		std::unordered_map<u32, std::function<void(u32, const Json &)>> &callbacks = BhopGlobalService::messageCallbacks.queue;

		if (auto found = callbacks.extract(messageID); !found.empty())
		{
			callback = found.mapped();
		}
	}

	if (callback)
	{
		META_CONPRINTF("[Bhop::Global] Executing callback #%i\n", messageID);
		callback(messageID, payload);
	}
}
