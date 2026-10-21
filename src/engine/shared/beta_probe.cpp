/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-ce3aedb8 */

#include "beta_probe.h"

#include <base/logger.h>
#include <base/str.h>

#include <algorithm>
#include <mutex>

namespace BetaProbe
{
int64_t g_aPhaseTicks[NUM_PHASES] = {};
std::atomic<int64_t> g_MainHeartbeat{0};

static const char *const gs_apPhaseNames[NUM_PHASES] = {
	"update",
	"network",
	"snapshot",
	"predict",
	"game_update",
	"server_browser",
	"discord",
	"render",
	"swap",
	"sleep",
};

int64_t g_aPartTicks[NUM_PARTS] = {};

static const char *const gs_apPartNames[NUM_PARTS] = {
	"spotify_connect",
	"listen_along",
	"presence",
	"media_session",
	"music_services",
	"visualizer",
	"artwork",
	"lyrics",
	"music_hud",
	"music_layout",
};

const char *PartName(int Part)
{
	return Part >= 0 && Part < NUM_PARTS ? gs_apPartNames[Part] : "?";
}

const char *PhaseName(int Phase)
{
	return Phase >= 0 && Phase < NUM_PHASES ? gs_apPhaseNames[Phase] : "?";
}

namespace
{
	struct SEvent
	{
		int m_Type;
		int m_Value;
		int64_t m_Time;
	};
	// A small ring: the reporter drains it every frame, a burst beyond this is not worth keeping.
	constexpr int MAX_EVENTS = 64;
	SEvent gs_aEvents[MAX_EVENTS];
	int gs_EventStart = 0;
	int gs_NumEvents = 0;

	std::atomic<FAssertHook> gs_pfnAssertHook{nullptr};
}

void PushEvent(int Type, int Value)
{
	if(gs_NumEvents == MAX_EVENTS)
	{
		gs_EventStart = (gs_EventStart + 1) % MAX_EVENTS;
		gs_NumEvents--;
	}
	gs_aEvents[(gs_EventStart + gs_NumEvents) % MAX_EVENTS] = {Type, Value, time_get()};
	gs_NumEvents++;
}

bool PopEvent(int &Type, int &Value, int64_t &Time)
{
	if(gs_NumEvents == 0)
		return false;
	const SEvent &Event = gs_aEvents[gs_EventStart];
	Type = Event.m_Type;
	Value = Event.m_Value;
	Time = Event.m_Time;
	gs_EventStart = (gs_EventStart + 1) % MAX_EVENTS;
	gs_NumEvents--;
	return true;
}

namespace
{
	constexpr int NUM_BREADCRUMBS = 48;
	constexpr int BREADCRUMB_LENGTH = 200;
	char gs_aaBreadcrumbs[NUM_BREADCRUMBS][BREADCRUMB_LENGTH];
	uint64_t gs_aBreadcrumbSequence[NUM_BREADCRUMBS];
	std::atomic<uint64_t> gs_NextBreadcrumb{1};
	std::mutex gs_BreadcrumbMutex;

	bool IsFeatureSystem(const char *pSystem)
	{
		static const char *const s_apSystems[] = {"spotify", "listen", "discord", "client_indicator", "updater", "beta", "music_player", "social", "cecen"};
		for(const char *pName : s_apSystems)
			if(str_comp(pSystem, pName) == 0)
				return true;
		return false;
	}

	class CBreadcrumbLogger : public ILogger
	{
	public:
		CBreadcrumbLogger() { m_Filter.m_MaxLevel.store(LEVEL_INFO, std::memory_order_relaxed); }

		void Log(const CLogMessage *pMessage) override
		{
			if(pMessage->m_Level > LEVEL_WARN && !(pMessage->m_Level <= LEVEL_INFO && IsFeatureSystem(pMessage->m_aSystem)))
				return;
			// "2026-10-21 05:45:38 I discord: ..." without the date
			const char *pLine = pMessage->m_aLine;
			if(pMessage->m_TimestampLength > 11)
				pLine += 11;
			const std::lock_guard<std::mutex> Lock(gs_BreadcrumbMutex);
			const uint64_t Sequence = gs_NextBreadcrumb.load(std::memory_order_relaxed);
			const int Slot = (int)(Sequence % NUM_BREADCRUMBS);
			str_copy(gs_aaBreadcrumbs[Slot], pLine, BREADCRUMB_LENGTH);
			gs_aBreadcrumbSequence[Slot] = Sequence;
			gs_NextBreadcrumb.store(Sequence + 1, std::memory_order_release);
		}
	};
}

std::shared_ptr<ILogger> CreateBreadcrumbLogger()
{
	return std::make_shared<CBreadcrumbLogger>();
}

void CopyBreadcrumbs(std::vector<std::string> &vOut, uint64_t &Sequence)
{
	const std::lock_guard<std::mutex> Lock(gs_BreadcrumbMutex);
	const uint64_t Next = gs_NextBreadcrumb.load(std::memory_order_relaxed);
	uint64_t First = Next > NUM_BREADCRUMBS ? Next - NUM_BREADCRUMBS : 1;
	First = std::max(First, Sequence + 1);
	for(uint64_t i = First; i < Next; i++)
	{
		const int Slot = (int)(i % NUM_BREADCRUMBS);
		if(gs_aBreadcrumbSequence[Slot] == i)
			vOut.emplace_back(gs_aaBreadcrumbs[Slot]);
	}
	Sequence = Next - 1;
}

void WriteBreadcrumbsUnlocked(FBreadcrumbWriter pfnWrite, void *pUser)
{
	const uint64_t Next = gs_NextBreadcrumb.load(std::memory_order_acquire);
	const uint64_t First = Next > NUM_BREADCRUMBS ? Next - NUM_BREADCRUMBS : 1;
	for(uint64_t i = First; i < Next; i++)
	{
		const int Slot = (int)(i % NUM_BREADCRUMBS);
		if(gs_aBreadcrumbSequence[Slot] == i)
		{
			gs_aaBreadcrumbs[Slot][BREADCRUMB_LENGTH - 1] = '\0';
			pfnWrite(gs_aaBreadcrumbs[Slot], pUser);
		}
	}
}

void SetAssertHook(FAssertHook pfnHook)
{
	gs_pfnAssertHook = pfnHook;
}

void OnAssert(const char *pMessage)
{
	if(FAssertHook pfnHook = gs_pfnAssertHook.load())
		pfnHook(pMessage);
}
} // namespace BetaProbe
