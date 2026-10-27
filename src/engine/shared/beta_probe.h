/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-e3072155 */

#ifndef ENGINE_SHARED_BETA_PROBE_H
#define ENGINE_SHARED_BETA_PROBE_H

#include <base/time.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class ILogger;

namespace BetaProbe
{
enum EPhase
{
	PHASE_UPDATE = 0,
	PHASE_NETWORK,
	PHASE_SNAPSHOT,
	PHASE_PREDICT,
	PHASE_GAME_UPDATE,
	PHASE_SERVER_BROWSER,
	PHASE_DISCORD,
	PHASE_RENDER,
	PHASE_SWAP,
	PHASE_SLEEP,
	NUM_PHASES,
};

const char *PhaseName(int Phase);

extern int64_t g_aPhaseTicks[NUM_PHASES];

class CScope
{
	int m_Phase;
	int64_t m_Start;

public:
	explicit CScope(int Phase) :
		m_Phase(Phase), m_Start(time_get_impl()) {}
	~CScope() { g_aPhaseTicks[m_Phase] += time_get_impl() - m_Start; }
	CScope(const CScope &) = delete;
	CScope &operator=(const CScope &) = delete;
};

enum EPart
{
	PART_SPOTIFY_CONNECT = 0,
	PART_LISTEN_ALONG,
	PART_PRESENCE,
	PART_MEDIA_SESSION,
	PART_MUSIC_SERVICES,
	PART_VISUALIZER,
	PART_ARTWORK,
	PART_LYRICS,
	PART_MUSIC_HUD,
	PART_MUSIC_LAYOUT,
	NUM_PARTS,
};

const char *PartName(int Part);
extern int64_t g_aPartTicks[NUM_PARTS];
extern int64_t g_aPartTicksTotal[NUM_PARTS];

inline void AddPartTicks(int Part, int64_t Ticks)
{
	g_aPartTicks[Part] += Ticks;
	g_aPartTicksTotal[Part] += Ticks;
}

class CPartScope
{
	int m_Part;
	int64_t m_Start;

public:
	explicit CPartScope(int Part) :
		m_Part(Part), m_Start(time_get_impl()) {}
	~CPartScope() { AddPartTicks(m_Part, time_get_impl() - m_Start); }
	CPartScope(const CPartScope &) = delete;
	CPartScope &operator=(const CPartScope &) = delete;
};

enum EEvent
{
	EVENT_PREDICTION_RESET = 0,
	EVENT_SNAPSHOT_GAP,
};

void PushEvent(int Type, int Value);
bool PopEvent(int &Type, int &Value, int64_t &Time);

extern std::atomic<int64_t> g_MainHeartbeat;
inline void Heartbeat() { g_MainHeartbeat.store(time_get_impl(), std::memory_order_relaxed); }

std::shared_ptr<ILogger> CreateBreadcrumbLogger();
void CopyBreadcrumbs(std::vector<std::string> &vOut, uint64_t &Sequence);
typedef void (*FBreadcrumbWriter)(const char *pLine, void *pUser);
void WriteBreadcrumbsUnlocked(FBreadcrumbWriter pfnWrite, void *pUser);

typedef void (*FAssertHook)(const char *pMessage);
void SetAssertHook(FAssertHook pfnHook);
void OnAssert(const char *pMessage);
} // namespace BetaProbe

#endif
