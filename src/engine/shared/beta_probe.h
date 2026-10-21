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

// 667client: cheap measurements the engine takes for the beta reporter
// (game/client/components/sixclient/beta_reporter). Everything except the heartbeat is touched
// by the main thread only, so nothing here locks. Times come from time_get_impl(): time_get()
// returns the same value for a whole main loop iteration.
namespace BetaProbe
{
enum EPhase
{
	PHASE_UPDATE = 0, // CClient::Update as a whole
	PHASE_NETWORK, // reading and handling packets
	PHASE_SNAPSHOT, // GameClient()->OnNewSnapshot
	PHASE_PREDICT, // GameClient()->OnPredict
	PHASE_GAME_UPDATE, // GameClient()->OnUpdate
	PHASE_SERVER_BROWSER,
	PHASE_DISCORD,
	PHASE_RENDER,
	PHASE_SWAP,
	PHASE_SLEEP, // the intended wait for the next frame, not part of a stall
	NUM_PHASES,
};

const char *PhaseName(int Phase);

// Time spent per phase since the reporter last collected it, in time_get() ticks.
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

// Parts of the heavier features, timed on their own so a report says which part of a feature
// takes the time (is it Spotify Connect or the music player's HUD?).
enum EPart
{
	PART_SPOTIFY_CONNECT = 0, // the Spotify Connect remote, on the game thread
	PART_LISTEN_ALONG, // following another player's song
	PART_PRESENCE, // announcing ourselves and reading the Cecen player list
	PART_MEDIA_SESSION, // reading what the system media player plays
	PART_MUSIC_SERVICES, // starting and stopping the music player's background services
	PART_VISUALIZER,
	PART_ARTWORK,
	PART_LYRICS,
	PART_MUSIC_HUD, // drawing the music player
	PART_MUSIC_LAYOUT, // placing the music player on the HUD, every frame
	NUM_PARTS,
};

const char *PartName(int Part);
extern int64_t g_aPartTicks[NUM_PARTS];

class CPartScope
{
	int m_Part;
	int64_t m_Start;

public:
	explicit CPartScope(int Part) :
		m_Part(Part), m_Start(time_get_impl()) {}
	~CPartScope() { g_aPartTicks[m_Part] += time_get_impl() - m_Start; }
	CPartScope(const CPartScope &) = delete;
	CPartScope &operator=(const CPartScope &) = delete;
};

enum EEvent
{
	EVENT_PREDICTION_RESET = 0, // the client threw its predicted time away ("prediction time reset!")
	EVENT_SNAPSHOT_GAP, // no snapshot arrived for Value milliseconds
};

void PushEvent(int Type, int Value);
bool PopEvent(int &Type, int &Value, int64_t &Time);

// Set at the start of every main loop iteration. The hang watchdog reads it from its own thread.
extern std::atomic<int64_t> g_MainHeartbeat;
inline void Heartbeat() { g_MainHeartbeat.store(time_get_impl(), std::memory_order_relaxed); }

// Breadcrumbs: the last log lines of the features added since 1.0.22 (spotify, listen, discord,
// client_indicator, updater, beta, ...) and every warning and error, from any thread. The client
// adds this logger to its global loggers.
std::shared_ptr<ILogger> CreateBreadcrumbLogger();
// The lines logged after Sequence, oldest first; Sequence is moved past them.
void CopyBreadcrumbs(std::vector<std::string> &vOut, uint64_t &Sequence);
// For crash handlers: takes no lock and allocates nothing, a line being written may come out torn.
typedef void (*FBreadcrumbWriter)(const char *pLine, void *pUser);
void WriteBreadcrumbsUnlocked(FBreadcrumbWriter pfnWrite, void *pUser);

// A failing dbg_assert passes its message here before the process ends.
typedef void (*FAssertHook)(const char *pMessage);
void SetAssertHook(FAssertHook pfnHook);
void OnAssert(const char *pMessage);
} // namespace BetaProbe

#endif
