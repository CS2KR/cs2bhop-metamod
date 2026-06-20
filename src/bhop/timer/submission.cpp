#include "submission.h"
#include "bhop/db/bhop_db.h"
#include "bhop/global/bhop_global.h"
#include "bhop/global/events.h"
#include "bhop/language/bhop_language.h"
#include "bhop/mode/bhop_mode.h"
#include "bhop/recording/bhop_recording.h"
#include "bhop/style/bhop_style.h"
#include "bhop/replays/bhop_replay.h"
#include "utils/async_file_io.h"
#include "utils/utils.h"

#include "vendor/sql_mm/src/public/sql_mm.h"

CConVar<bool> bhop_debug_announce_global("bhop_debug_announce_global", FCVAR_NONE, "Print debug info for record announcements.", false);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void BuildReplayPath(char *buf, int bufLen, const UUID_t &uuid)
{
	V_snprintf(buf, bufLen, "%s/%s.replay", BHOP_REPLAY_PATH, uuid.ToString().c_str());
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
RunSubmission::RunSubmission(BhopPlayer *player)
	: uid(RunSubmission::idCount++), timestamp(g_pBhopUtils->GetServerGlobals()->realtime), userID(player->GetClient()->GetUserID()),
	  localUUID(player->recordingService->GetCurrentRunUUID()), finalUUID(player->recordingService->GetCurrentRunUUID()),
	  time(player->timerService->GetTime())
{
	this->local = BhopDatabaseService::IsReady() && BhopDatabaseService::IsMapSetUp();
	this->global = player->hasPrime && BhopGlobalService::MayBecomeAvailable();
	if (bhop_debug_announce_global.Get() && !this->global)
	{
		if (!player->hasPrime)
		{
			META_CONPRINTF("[Bhop::Global - %u] Player %s does not have Prime, will not submit globally.\n", uid, player->GetName());
		}
		if (!BhopGlobalService::IsAvailable())
		{
			META_CONPRINTF("[Bhop::Global - %u] Global service is not available, will not submit globally.\n", uid);
		}
	}
	// Setup player snapshot
	this->player.name = player->GetName();
	this->player.steamid64 = player->GetSteamId64();

	// Setup mode
	auto mode = Bhop::mode::GetModeInfo(player->modeService);
	this->mode.name = mode.shortModeName;
	Bhop::API::Mode apiMode;
	this->global = this->global && Bhop::API::DecodeModeString(this->mode.name, apiMode);
	if (!this->global)
	{
		if (bhop_debug_announce_global.Get())
		{
			META_CONPRINTF("[Bhop::Global - %u] Mode '%s' is not a valid global mode, will not submit globally.\n", uid, this->mode.name.c_str());
		}
	}
	this->mode.md5 = mode.md5;
	this->mode.pluginID = mode.id;
	if (mode.databaseID <= 0)
	{
		this->local = false;
	}
	else
	{
		this->mode.localID = mode.databaseID;
	}

	// Setup map
	this->map.name = g_pBhopUtils->GetServerGlobals()->mapname.ToCStr();
	char md5[33];
	g_pBhopUtils->GetCurrentMapMD5(md5, sizeof(md5));
	this->map.md5 = md5;

	// Setup course
	assert(player->timerService->GetCourse());
	this->course.name = player->timerService->GetCourse()->GetName().Get();
	this->course.localID = player->timerService->GetCourse()->localDatabaseID;
	if (this->local && this->course.localID <= 0)
	{
		this->local = false;
	}

	// clang-format off

	BhopGlobalService::WithCurrentMap([&](const Bhop::API::Map *currentMap)
	{
		this->global = this->global && currentMap != nullptr;

		if (!currentMap)
		{
			return;
		}

		const Bhop::API::Map::Course *course = nullptr;

		for (const Bhop::API::Map::Course &c : currentMap->courses)
		{
			if (BHOP_STREQ(c.name.c_str(), this->course.name.c_str()))
			{
				course = &c;
				break;
			}
		}

		if (course == nullptr)
		{
			if (bhop_debug_announce_global.Get())
			{
				META_CONPRINTF("[Bhop::Global - %u] Course '%s' not found on global map '%s', will not submit globally.\n", uid, this->course.name.c_str(),
							   currentMap->name.c_str());
				META_CONPRINTF("[Bhop::Global - %u] Available courses:\n", uid);
				for (const Bhop::API::Map::Course &c : currentMap->courses)
				{
					META_CONPRINTF(" - %s\n", c.name.c_str());
				}
			}
			global = false;
		}
		else
		{
			if (apiMode == Bhop::API::Mode::_128tick)
			{
				this->globalFilterID = course->filters._128tick.id;
			}
			else if (apiMode == Bhop::API::Mode::CSS66 && course->filters.css66)
			{
				this->globalFilterID = course->filters.css66->id;
			}
			else
			{
				if (bhop_debug_announce_global.Get())
				{
					META_CONPRINTF("[Bhop::Global - %u] Course '%s' has no compatible global filter for mode '%s'.\n", uid,
								   this->course.name.c_str(), this->mode.name.c_str());
				}
				global = false;
			}
		}
	});

	// clang-format on

	// Setup styles
	FOR_EACH_VEC(player->styleServices, i)
	{
		auto style = Bhop::style::GetStyleInfo(player->styleServices[i]);
		this->styles.push_back({player->styleServices[i]->GetStyleShortName(), style.md5});
		if (style.databaseID < 0 || style.databaseID >= 64)
		{
			this->local = false;
			continue;
		}
		this->styleIDs |= (1ull << style.databaseID);
	}

	// Metadata
	this->metadata = player->timerService->GetCurrentRunMetadata().Get();
}

void RunSubmission::Start()
{
	// Previous GPBs
	if (global)
	{
		const BhopCourseDescriptor *currentCourse = Bhop::course::GetCourse(this->course.name.c_str(), false);
		BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(userID);
		const PBData *pbData = currentCourse && player ? player->timerService->GetGlobalCachedPB(currentCourse, this->mode.pluginID) : nullptr;
		if (pbData)
		{
			this->oldGPB.overall.time = pbData->overall.pbTime;
			this->oldGPB.overall.points = pbData->overall.points;
		}
	}

	// --- Kick off submissions ---
	// Global submission is always started immediately so the API gets the run data as soon as possible.
	// Local DB insert is deferred to TryFinalize() so the API has a chance to supply the canonical UUID
	// before we write to the DB.
	// Exception: non-global runs insert to DB immediately (no UUID synchronization needed).
	if (global)
	{
		SubmitGlobal();
	}
	else if (local)
	{
		// No global UUID to wait for; insert now with the locally-generated UUID.
		SubmitLocal(localUUID.ToString().c_str());
	}
}

// ---------------------------------------------------------------------------
// API response
// ---------------------------------------------------------------------------

void RunSubmission::SubmitGlobal()
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(this->userID);
	if (!player)
	{
		this->global = false;
		return;
	}

	auto callback = [uid = this->uid](Bhop::API::events::NewRecordAck &ack) { RunSubmission::OnGlobalRecordSubmitted(ack, uid); };
	BhopGlobalService::SubmitRecordResult submissionResult =
		player->globalService->SubmitRecord(this->globalFilterID, this->time, this->mode.md5, &this->styles, this->metadata, std::move(callback));

	if (bhop_debug_announce_global.Get())
	{
		META_CONPRINTF("[Bhop::Global - %u] Global record submission result: %d\n", uid, static_cast<int>(submissionResult));
	}

	switch (submissionResult)
	{
		case BhopGlobalService::SubmitRecordResult::PlayerNotAuthenticated:
		case BhopGlobalService::SubmitRecordResult::MapNotGlobal:
		case BhopGlobalService::SubmitRecordResult::NotConnected:
		{
			this->global = false;
			if (this->local)
			{
				SubmitLocal(localUUID.ToString().c_str());
			}
			break;
		}
		case BhopGlobalService::SubmitRecordResult::Queued:
		{
			this->pendingQueuedSubmission = true;
			break;
		}
		case BhopGlobalService::SubmitRecordResult::Submitted:
		{
			this->global = true;
			break;
		}
	}
}

void RunSubmission::OnAPIResponse(const Bhop::API::events::NewRecordAck &ack)
{
	apiResponseReceived = true;
	globalResponse.received = true;
	globalResponse.recordId = ack.recordId;
	globalResponse.overall.rank = ack.overallData.rank;
	globalResponse.overall.points = ack.overallData.points;
	globalResponse.overall.maxRank = ack.overallData.leaderboardSize;

	pendingQueuedSubmission = false;

	if (finalized)
	{
		DoLateAPIResponse(ack.recordId);
	}
	else
	{
		if (replayReady && !ack.recordId.empty())
		{
			UUID_t apiFinalUUID(ack.recordId.c_str());
			if (!(finalUUID == apiFinalUUID))
			{
				if (g_asyncFileIO)
				{
					char oldPath[512], newPath[512];
					BuildReplayPath(oldPath, sizeof(oldPath), finalUUID);
					BuildReplayPath(newPath, sizeof(newPath), apiFinalUUID);
					g_asyncFileIO->QueueRename(oldPath, newPath);
				}
				finalUUID = apiFinalUUID;
			}
		}
		TryFinalize();
	}
}

// ---------------------------------------------------------------------------
// Replay ready
// ---------------------------------------------------------------------------

void RunSubmission::OnReplayReady(std::vector<char> &&buffer)
{
	replayBuffer = std::move(buffer);
	replayReady = true;

	// Eagerly resolve finalUUID if the API already responded, so the disk write uses the
	// right path. If the API responds later, DoLateAPIResponse handles the rename.
	if (apiResponseReceived && !globalResponse.recordId.empty())
	{
		finalUUID = UUID_t(globalResponse.recordId.c_str());
	}

	// Write replay to disk and notify the player regardless of local/global state.
	// QueueWriteBuffer takes the buffer by value, so replayBuffer remains available.
	char replayPath[512];
	BuildReplayPath(replayPath, sizeof(replayPath), finalUUID);
	if (g_asyncFileIO)
	{
		g_asyncFileIO->QueueWriteBuffer(replayPath, replayBuffer);
		// Notify the player now (write is async but fire-and-forget, effectively always succeeds).
		BhopPlayer *callbackPlayer = g_pBhopPlayerManager->ToPlayer(userID);
		if (callbackPlayer)
		{
			callbackPlayer->languageService->PrintChat(true, false, "Replay - Run Replay Saved", finalUUID.ToString().c_str());
			callbackPlayer->languageService->PrintConsole(false, false, "Replay - Run Replay Saved (Console)", finalUUID.ToString().c_str());
		}
	}

	if (finalized)
	{
		return;
	}

	TryFinalize();
}

// ---------------------------------------------------------------------------
// TryFinalize
// ---------------------------------------------------------------------------

void RunSubmission::TryFinalize()
{
	if (finalized)
	{
		return;
	}

	// No submissions — nothing to finalize; just mark done so CheckAll() can announce and GC.
	if (!local && !global)
	{
		finalized = true;
		return;
	}

	const f64 now = g_pBhopUtils->GetServerGlobals()->realtime;
	const bool timeoutReached = now >= (timestamp + RunSubmission::timeout);

	// For global runs that haven't heard back from the API yet:
	// keep waiting unless we've timed out or the run was only queued (offline).
	if (global && !apiResponseReceived && !timeoutReached && !pendingQueuedSubmission)
	{
		return;
	}

	finalized = true;

	// Determine the authoritative UUID for this run (if not already set in OnReplayReady).
	if (apiResponseReceived && !globalResponse.recordId.empty())
	{
		finalUUID = UUID_t(globalResponse.recordId.c_str());
	}
	// else: finalUUID stays equal to localUUID (set in constructor)

	// 1. Local DB insert with the final UUID (skip if already inserted in the constructor
	// for non-global runs, where SubmitLocal was called immediately with localUUID).
	if (local && !localSubmitted)
	{
		SubmitLocal(finalUUID.ToString().c_str());
	}

	// 2. Upload replay to the global API if the run was accepted.
	/*
	if (global && apiResponseReceived && !globalResponse.recordId.empty() && !replayBuffer.empty())
	{
		BhopGlobalService::QueueReplayUpload(finalUUID, std::vector<char>(replayBuffer));
	}
	*/
}

// ---------------------------------------------------------------------------
// Late API response (API replied after we already finalized with localUUID)
// ---------------------------------------------------------------------------

void RunSubmission::DoLateAPIResponse(const std::string &apiUUID)
{
	if (apiUUID.empty())
	{
		return;
	}

	UUID_t apiFinalUUID(apiUUID.c_str());

	if (g_asyncFileIO)
	{
		char oldPath[512], newPath[512];
		BuildReplayPath(oldPath, sizeof(oldPath), localUUID);
		BuildReplayPath(newPath, sizeof(newPath), apiFinalUUID);
		g_asyncFileIO->QueueRename(oldPath, newPath);
	}

	if (localSubmitted)
	{
		BhopDatabaseService::UpdateRunUUID(localUUID.ToString().c_str(), apiUUID.c_str(), nullptr, nullptr);
	}

	finalUUID = apiFinalUUID;
}

// ---------------------------------------------------------------------------
// Local DB submission
// ---------------------------------------------------------------------------

void RunSubmission::SubmitLocal(const char *uuid)
{
	this->localSubmitted = true;

	// Styled runs use a fire-and-forget insert (save_time.cpp skips rank queries);
	// mark local false now so CheckAll() doesn't wait for a response that will never arrive.
	if (this->styleIDs != 0)
	{
		this->local = false;
	}

	auto onFailure = [uid = this->uid](std::string, int)
	{
		RunSubmission *sub = RunSubmission::Get(uid);
		if (!sub)
		{
			return;
		}
		sub->local = false;
	};

	auto onSuccess = [uid = this->uid](std::vector<ISQLQuery *> queries)
	{
		RunSubmission *sub = RunSubmission::Get(uid);
		if (!sub)
		{
			return;
		}
		sub->localResponse.received = true;

		ISQLResult *result = queries[1]->GetResultSet();
		sub->localResponse.overall.firstTime = result->GetRowCount() == 1;
		if (!sub->localResponse.overall.firstTime)
		{
			result->FetchRow();
			f32 pb = result->GetFloat(0);
			if (fabs(pb - sub->time) < EPSILON)
			{
				result->FetchRow();
				f32 oldPB = result->GetFloat(0);
				sub->localResponse.overall.pbDiff = sub->time - oldPB;
			}
			else
			{
				sub->localResponse.overall.pbDiff = sub->time - pb;
			}
		}

		result = queries[2]->GetResultSet();
		result->FetchRow();
		sub->localResponse.overall.rank = result->GetInt(0);

		result = queries[3]->GetResultSet();
		result->FetchRow();
		sub->localResponse.overall.maxRank = result->GetInt(0);

		sub->UpdateLocalCache();
	};

	BhopDatabaseService::SaveTime(uuid, this->player.steamid64, this->course.localID, this->mode.localID, this->time, this->styleIDs, this->metadata,
								  onSuccess, onFailure);
}

// ---------------------------------------------------------------------------
// Cache updates
// ---------------------------------------------------------------------------

void RunSubmission::UpdateLocalCache()
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(this->userID);
	if (player)
	{
		player->timerService->UpdateLocalPBCache();
	}
	BhopTimerService::UpdateLocalRecordCache();
}

// ---------------------------------------------------------------------------
// CheckAll — per-frame tick: drive announcements and garbage-collect
// ---------------------------------------------------------------------------

void RunSubmission::CheckAll()
{
	// clang-format off
	submissions.erase(std::remove_if(submissions.begin(), submissions.end(),
		[](RunSubmission *sub)
		{
			const f64 now = g_pBhopUtils->GetServerGlobals()->realtime;

			// Try to finalize on each frame until it commits
			sub->TryFinalize();

			// Wait for all pending responses before announcing rank info, so local and
			// global messages arrive together in a single burst.
			bool localReady = !sub->local || sub->localResponse.received;
			bool globalReady = !sub->global || sub->globalResponse.received;

			if (localReady && globalReady && !sub->runAnnounced)
			{
				sub->runAnnounced = true;
				sub->AnnounceRun();
				if (sub->local) sub->AnnounceLocal();
				//if (sub->global) sub->AnnounceGlobal();
			}

			// Keep alive until the queued submission eventually gets an API response.
			// These entries are cleaned up by Clear() on map change.
			if (sub->pendingQueuedSubmission)
			{
				return false;
			}

			if (!sub->finalized)
			{
				return false;
			}

			// Wait for local and global responses (with regular timeout)
			bool waitingForLocal = sub->local && !sub->localResponse.received;
			bool waitingForGlobal = sub->global && !sub->globalResponse.received;
			const bool announcementTimeoutReached = now >= (sub->timestamp + RunSubmission::announcementTimeout);

			if (waitingForLocal || waitingForGlobal)
			{
				if (!announcementTimeoutReached)
				{
					return false;
				}
				// Announcement timeout — announce what we have and GC
				if (!sub->runAnnounced)
				{
					sub->runAnnounced = true;
					sub->AnnounceRun();
					if (sub->local && sub->localResponse.received)
					{
						sub->AnnounceLocal();
					}
				}
				if (sub->global && sub->globalResponse.received)
				{
					//sub->AnnounceGlobal();
				}
			}

			// Keep alive until the replay breather ends and OnReplayReady fires,
			// so the disk write and player notification are not lost.
			if (!sub->replayReady)
			{
				return false;
			}

			delete sub;
			return true;
		}),
		submissions.end());
	// clang-format on
}

void RunSubmission::OnGlobalRecordSubmitted(const Bhop::API::events::NewRecordAck &ack, u32 uid)
{
	RunSubmission *sub = RunSubmission::Get(uid);
	if (!sub)
	{
		return;
	}

	sub->OnAPIResponse(ack);
}

// ---------------------------------------------------------------------------
// Announce methods
// ---------------------------------------------------------------------------

void RunSubmission::AnnounceRun()
{
	char formattedTime[32];
	utils::FormatTime(time, formattedTime, sizeof(formattedTime));

	CUtlString combinedModeStyleText;
	combinedModeStyleText.Format("{purple}%s{grey}", this->mode.name.c_str());
	for (auto &style : this->styles)
	{
		combinedModeStyleText += " +{grey2}";
		combinedModeStyleText.Append(style.name.c_str());
		combinedModeStyleText += "{grey}";
	}

	for (u32 i = 0; i < MAXPLAYERS + 1; i++)
	{
		BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(i);
		if (!player->IsInGame())
		{
			continue;
		}
		player->languageService->PrintChat(true, false, "Beat Course Info - Basic", this->player.name.c_str(), this->course.name.c_str(),
										   formattedTime, combinedModeStyleText.Get());
	}
}

void RunSubmission::AnnounceLocal()
{
	for (u32 i = 0; i < MAXPLAYERS + 1; i++)
	{
		BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(i);
		if (!player->IsInGame())
		{
			continue;
		}

		char formattedDiffTime[32];
		BhopTimerService::FormatDiffTime(this->localResponse.overall.pbDiff, formattedDiffTime, sizeof(formattedDiffTime));

		// clang-format off
		std::string diffText = this->localResponse.overall.firstTime
			? ""
			: player->languageService->PrepareMessage("Personal Best Difference",
				this->localResponse.overall.pbDiff < 0 ? "{green}" : "{red}", formattedDiffTime);
		// clang-format on

		player->languageService->PrintChat(true, false, "Beat Course Info - Local", this->localResponse.overall.rank,
										   this->localResponse.overall.maxRank, diffText.c_str());
	}
}
