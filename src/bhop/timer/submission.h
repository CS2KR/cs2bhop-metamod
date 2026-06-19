/*
 * Run submission stuff.
 */

#pragma once

#include "bhop/timer/bhop_timer.h"
#include "bhop/global/api.h"
#include "utils/uuid.h"

struct RunSubmission
{
	RunSubmission(BhopPlayer *player);

	const u32 uid;
	const f64 timestamp;

	const CPlayerUserId userID;

	const UUID_t localUUID;
	UUID_t finalUUID;

	bool global {};
	bool local {};

	bool pendingQueuedSubmission {};

	bool replayReady {};
	bool apiResponseReceived {};
	bool finalized {};
	bool runAnnounced {};
	bool localSubmitted {};

	std::vector<char> replayBuffer;

	struct
	{
		std::string name {};
		u64 steamid64 {};
	} player;

	const f64 time;

	// We need to store the previous global PBs because the player might be gone before the announcement is made.
	struct
	{
		struct
		{
			f64 time {};
			f64 points {};
		} overall;
	} oldGPB;

	struct
	{
		std::string name {};
		std::string md5 {};
		u32 localID {};
	} mode;

	struct
	{
		std::string name {};
		std::string md5 {};
	} map;

	struct
	{
		std::string name {};
		u32 localID {};
	} course;

	u16 globalFilterID {};

	struct StyleInfo
	{
		std::string name {};
		std::string md5 {};
	};

	std::vector<StyleInfo> styles;
	u64 styleIDs {};

	std::string metadata;

	struct
	{
		bool received {};
		std::string recordId {};

		struct RunData
		{
			u32 rank {};
			f64 points = -1;
			u64 maxRank {};
		};

		RunData overall {};
	} globalResponse;

	struct
	{
		bool received {};

		struct
		{
			bool firstTime {};
			f32 pbDiff {};
			u32 rank {};
			u32 maxRank {};
		} overall;
	} localResponse;

	void OnReplayReady(std::vector<char> &&buffer);

	static RunSubmission *Create(BhopPlayer *player)
	{
		RunSubmission *sub = new RunSubmission(player);
		submissions.push_back(sub);
		return sub;
	}

	static RunSubmission *Get(u32 uid)
	{
		auto it = std::find_if(submissions.begin(), submissions.end(), [uid](RunSubmission *s) { return s->uid == uid; });
		return (it != submissions.end()) ? *it : nullptr;
	}

	static RunSubmission *GetByUUID(const UUID_t &uuid)
	{
		auto it = std::find_if(submissions.begin(), submissions.end(), [&uuid](RunSubmission *s) { return s->localUUID == uuid; });
		return (it != submissions.end()) ? *it : nullptr;
	}

	static void CheckAll();

	static void Clear()
	{
		for (auto *s : submissions)
		{
			delete s;
		}
		submissions.clear();
	}

private:
	void TryFinalize();

	void SubmitLocal(const char *uuid);

	void UpdateLocalCache();

	void AnnounceRun();
	void AnnounceLocal();

	static inline std::vector<RunSubmission *> submissions;
	static inline u32 idCount = 0;

	static inline constexpr f64 timeout = 5.0f;
	static inline constexpr f64 announcementTimeout = 10.0f;
};
