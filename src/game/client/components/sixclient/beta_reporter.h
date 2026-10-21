/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-83054372 */

#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_BETA_REPORTER_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_BETA_REPORTER_H

#include <engine/console.h>

#include <game/client/component.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class IHttpRequest;

// 667client: while the client is in beta, collects the moments players notice and sends them to
// the Cecen service (POST <service>/report), so they can be looked into later:
// - hitches: a frame that took longer than sc_beta_hitch_ms, with the engine phases and the
//   components that took the time,
// - hangs: the game thread stuck for a second or more; a watchdog thread samples its stack,
// - lag: snapshots arriving late, prediction time resets, the server correcting our tee,
// - crashes, failed asserts and sessions that ended without a clean exit (the game was killed,
//   e.g. while hanging), found and sent on the next start.
// Turned off with sc_beta_reports 0.
class CBetaReporter : public CComponent
{
public:
	CBetaReporter();
	~CBetaReporter() override;
	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
	void OnInit() override;
	void OnShutdown() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnUpdate() override;
	void OnRender() override;

	struct SEvent
	{
		std::string m_Type;
		int64_t m_Time = 0; // unix seconds
		std::vector<std::pair<std::string, int64_t>> m_vNumbers;
		std::vector<std::pair<std::string, std::string>> m_vStrings;
		std::vector<std::pair<std::string, int>> m_vTop; // what took the time, in milliseconds
		std::vector<std::string> m_vStack;
		std::vector<std::string> m_vBreadcrumbs; // the log lines before a crash or hang
	};

	// A sample of the stuck game thread, taken by the watchdog.
	struct SHangSample
	{
		int64_t m_Heartbeat = 0;
		int m_StallMs = 0;
		std::vector<std::string> m_vFrames;
	};

private:
	static void ConBetaTest(IConsole::IResult *pResult, void *pUserData);

	bool Enabled() const;
	void ReadPreviousSession();
	void WriteSessionMarker();
	void CollectWindow(int64_t Now);
	void RecordHitch(int ActiveMs, int64_t Now);
	void AddEvent(SEvent &&Event);
	void AddCommon(SEvent &Event);
	std::string BuildPayload(const std::vector<SEvent> &vEvents);
	void Upload(bool Force);
	void SavePending();
	void SendSavedPending();
	std::string SpotifyHelperLogTail();

	void StartWatchdog();
	void StopWatchdog();
	void WatchdogMain();

	std::string m_InstallId;
	std::string m_ReportUrl;
	int64_t m_SessionStart = 0;

	// the current measuring window (between two OnRender calls)
	int64_t m_LastRenderTime = 0;
	int64_t m_ArmedSince = 0;
	bool m_Armed = false;

	// The features added since 1.0.22, timed every frame: even stutters below sc_beta_hitch_ms show
	// up in their worst frame and jitter counts.
	struct SFeature
	{
		std::string m_Name;
		size_t m_Index = 0;
		int m_Part = -1; // a BetaProbe::EPart instead of a component
		int64_t m_TotalTicks = 0;
		int64_t m_MaxTicks = 0;
		int m_JitterFrames = 0;
	};
	std::vector<SFeature> m_vFeatures;

	// input modes and antiping, since the last upload
	int m_NumFastSnapshotJumps = 0;
	int m_NumFastInputJumps = 0;
	int m_MaxFastJump = 0;
	int m_NumOthersCorrections = 0;
	int m_MaxOthersCorrection = 0;
	// a few seconds of corrections, to report bursts as rubberbanding
	int64_t m_BurstStart = 0;
	int m_BurstCorrections = 0;
	int m_BurstFastJumps = 0;
	int m_BurstOthers = 0;
	int m_BurstMaxCorrection = 0;
	uint64_t m_BreadcrumbSequence = 0;
	int m_ReportedSpotifyProblems = 0;

	// collected since the last upload
	std::vector<SEvent> m_vEvents;
	int m_NumHitches = 0;
	int m_NumHangs = 0;
	int m_WorstHitchMs = 0;
	int m_NumSnapshotGaps = 0;
	int m_WorstSnapshotGapMs = 0;
	int m_NumPredictionResets = 0;
	int m_NumCorrections = 0;
	int m_MaxCorrection = 0;
	int64_t m_Frames = 0;
	int64_t m_FrameTicks = 0;
	int64_t m_LastUpload = 0;
	std::shared_ptr<IHttpRequest> m_pUpload;
	std::string m_UploadedFromFile;

	// hang watchdog
	std::thread m_Watchdog;
	std::mutex m_WatchdogMutex;
	std::condition_variable m_WatchdogCv;
	bool m_StopWatchdog = false;
	std::atomic<bool> m_WatchArmed{false};
	std::atomic<bool> m_HangFileWritten{false};
	std::vector<SHangSample> m_vHangSamples; // guarded by m_WatchdogMutex
};

#endif
