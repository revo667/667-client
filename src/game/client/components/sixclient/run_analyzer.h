#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_RUN_ANALYZER_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_RUN_ANALYZER_H

#include <base/vmath.h>

#include <engine/console.h>

#include <game/client/component.h>
#include <game/client/components/ghost.h>

#include <string>
#include <vector>

class CRunAnalyzer : public CComponent
{
public:
	static constexpr int NUM_SEGMENTS = 10;

	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnMapLoad() override;
	void OnRender() override;

	void OnRunStart(int Tick);
	void OnRunTick(const CGhostCharacter &Char, bool Frozen);
	void OnRunEnd(int TimeMs);

private:
	enum EEventType
	{
		EVENT_HOOK = 0,
		EVENT_RELEASE,
		EVENT_JUMP,
		NUM_EVENT_TYPES,
	};

	struct SPbSample
	{
		vec2 m_Pos;
		int m_Tick;
	};

	struct SPbEvent
	{
		int m_Type;
		float m_Index;
	};

	struct SRunRecord
	{
		int64_t m_Timestamp = 0;
		bool m_Finished = false;
		int m_TimeMs = 0;
		int m_RunMs = 0;
		int m_FreezeMs = 0;
	};

	struct SReport
	{
		bool m_Valid = false;
		bool m_Finished = false;
		int m_TimeMs = 0;
		int m_PbMs = 0;
		float m_Progress = 0.0f;
		float m_FreezeSeconds = 0.0f;
		float m_FreezeShare = 0.0f;
		float m_AirShare = 0.0f;
		float m_AvgSpeed = 0.0f;
		float m_MaxSpeed = 0.0f;
		int m_Hooks = 0;
		int m_Jumps = 0;
		int m_aWorstSegment[3] = {-1, -1, -1};
		float m_aWorstLoss[3] = {};
		int m_aTimingCount[NUM_EVENT_TYPES] = {};
		float m_aTimingAvg[NUM_EVENT_TYPES] = {};
		float m_aTimingWithin[NUM_EVENT_TYPES] = {};
	};

	bool m_Running = false;
	int m_StartTick = 0;
	int m_LastTick = 0;
	bool m_HasLast = false;
	CGhostCharacter m_Last = {};

	std::vector<SPbSample> m_vPb;
	std::vector<SPbEvent> m_vPbEvents;
	int m_PbTimeMs = 0;
	float m_PbIndex = 0.0f;
	float m_aSegmentDelta[NUM_SEGMENTS + 1] = {};
	bool m_aSegmentReached[NUM_SEGMENTS + 1] = {};

	int m_Samples = 0;
	int m_FrozenSamples = 0;
	int m_AirSamples = 0;
	float m_SpeedSum = 0.0f;
	float m_MaxSpeed = 0.0f;
	int m_Hooks = 0;
	int m_Jumps = 0;
	std::vector<float> m_avTiming[NUM_EVENT_TYPES];

	SReport m_Report;
	int64_t m_ReportTime = 0;

	char m_aFeedback[64] = "";
	ColorRGBA m_FeedbackColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	int64_t m_FeedbackTime = 0;

	std::string m_MapName;
	std::vector<SRunRecord> m_vAllRuns;
	std::vector<SRunRecord> m_vSessionRuns;

	static void ConRunStats(IConsole::IResult *pResult, void *pUserData);

	void LoadPb();
	void LoadStats();
	void SaveRecord(const SRunRecord &Record);
	void UpdateProgress(vec2 Pos, int RelTick);
	float PbTickAt(float Index) const;
	void DetectEvents(const CGhostCharacter &Prev, const CGhostCharacter &Cur, int *pTypes, int &NumTypes) const;
	void JudgeEvent(int Type);
	void BuildStatsText(const std::vector<SRunRecord> &vRuns, char *pBuf, int BufSize) const;
	void RenderReport();
	void RenderFeedback();
};

#endif
