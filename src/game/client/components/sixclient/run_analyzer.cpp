/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-b4b7395a */

#include "run_analyzer.h"

#include <base/io.h>
#include <base/math.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/map.h>
#include <engine/shared/config.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/gamecore.h>

#include <algorithm>
#include <cmath>

namespace
{
	constexpr const char *STATS_FOLDER = "667Client/run_stats";
	constexpr float SEARCH_MAX_DISTANCE = 200.0f;
	constexpr float EVENT_WINDOW = 8.0f;
	constexpr float TEE_HALF = 14.0f;
	const char *gs_apEventNames[] = {"Hook", "Release", "Jump"};

	float SpeedBps(int VelX, int VelY, int TickSpeed)
	{
		return length(vec2(VelX / 256.0f, VelY / 256.0f)) * TickSpeed / 32.0f;
	}

	void FormatTime(char *pBuf, int BufSize, int Ms)
	{
		const int Minutes = Ms / 60000;
		const int Seconds = (Ms / 1000) % 60;
		const int Hundredths = (Ms / 10) % 100;
		if(Minutes > 0)
			str_format(pBuf, BufSize, "%d:%02d.%02d", Minutes, Seconds, Hundredths);
		else
			str_format(pBuf, BufSize, "%d.%02d", Seconds, Hundredths);
	}
}

void CRunAnalyzer::OnConsoleInit()
{
	Console()->Register("sc_run_stats", "", CFGFLAG_CLIENT, ConRunStats, this, "Show your attempt and finish statistics for the current map");
}

void CRunAnalyzer::ConRunStats(IConsole::IResult *pResult, void *pUserData)
{
	CRunAnalyzer *pSelf = static_cast<CRunAnalyzer *>(pUserData);
	char aSession[256], aAll[256];
	pSelf->BuildStatsText(pSelf->m_vSessionRuns, aSession, sizeof(aSession));
	pSelf->BuildStatsText(pSelf->m_vAllRuns, aAll, sizeof(aAll));
	char aLine[600];
	str_format(aLine, sizeof(aLine), "%s | session: %s", pSelf->m_MapName.c_str(), aSession);
	pSelf->GameClient()->Echo(aLine);
	str_format(aLine, sizeof(aLine), "%s | all time: %s", pSelf->m_MapName.c_str(), aAll);
	pSelf->GameClient()->Echo(aLine);
}

void CRunAnalyzer::OnMapLoad()
{
	m_Running = false;
	m_Report.m_Valid = false;
	m_vSessionRuns.clear();
	m_MapName = GameClient()->Map()->BaseName();
	LoadStats();
}

void CRunAnalyzer::LoadStats()
{
	m_vAllRuns.clear();
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s.csv", STATS_FOLDER, m_MapName.c_str());
	CLineReader Reader;
	if(!Reader.OpenFile(Storage()->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_SAVE)))
		return;
	while(const char *pLine = Reader.Get())
	{
		SRunRecord Record;
		long long Timestamp = 0;
		int Finished = 0;
		if(sscanf(pLine, "%lld,%d,%d,%d,%d", &Timestamp, &Finished, &Record.m_TimeMs, &Record.m_RunMs, &Record.m_FreezeMs) == 5)
		{
			Record.m_Timestamp = Timestamp;
			Record.m_Finished = Finished != 0;
			m_vAllRuns.push_back(Record);
		}
	}
}

void CRunAnalyzer::SaveRecord(const SRunRecord &Record)
{
	Storage()->CreateFolder("667Client", IStorage::TYPE_SAVE);
	Storage()->CreateFolder(STATS_FOLDER, IStorage::TYPE_SAVE);
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s.csv", STATS_FOLDER, m_MapName.c_str());
	IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_APPEND, IStorage::TYPE_SAVE);
	if(!File)
		return;
	char aLine[128];
	str_format(aLine, sizeof(aLine), "%lld,%d,%d,%d,%d", (long long)Record.m_Timestamp, Record.m_Finished ? 1 : 0, Record.m_TimeMs, Record.m_RunMs, Record.m_FreezeMs);
	io_write(File, aLine, str_length(aLine));
	io_write_newline(File);
	io_close(File);
}

void CRunAnalyzer::LoadPb()
{
	m_vPb.clear();
	m_vPbEvents.clear();
	m_PbTimeMs = 0;
	std::vector<CGhostCharacter> vPath;
	int StartTick = 0;
	if(!GameClient()->m_Ghost.CopyOwnGhostPath(vPath, StartTick, m_PbTimeMs) || vPath.size() < 4)
		return;
	m_vPb.reserve(vPath.size());
	for(const CGhostCharacter &Char : vPath)
		m_vPb.push_back({vec2((float)Char.m_X, (float)Char.m_Y), Char.m_Tick - StartTick});
	for(size_t i = 1; i < vPath.size(); i++)
	{
		int aTypes[NUM_EVENT_TYPES];
		int NumTypes = 0;
		DetectEvents(vPath[i - 1], vPath[i], aTypes, NumTypes);
		for(int t = 0; t < NumTypes; t++)
			m_vPbEvents.push_back({aTypes[t], (float)i});
	}
}

void CRunAnalyzer::OnRunStart(int Tick)
{
	if(m_Running)
		OnRunEnd(-1);
	m_Running = true;
	m_StartTick = Tick;
	m_LastTick = Tick;
	m_HasLast = false;
	m_PbIndex = 0.0f;
	std::fill(std::begin(m_aSegmentDelta), std::end(m_aSegmentDelta), 0.0f);
	std::fill(std::begin(m_aSegmentReached), std::end(m_aSegmentReached), false);
	m_Samples = m_FrozenSamples = m_AirSamples = 0;
	m_SpeedSum = m_MaxSpeed = 0.0f;
	m_Hooks = m_Jumps = 0;
	for(auto &vTiming : m_avTiming)
		vTiming.clear();
	LoadPb();
	if(!m_vPb.empty())
		m_aSegmentReached[0] = true;
}

float CRunAnalyzer::PbTickAt(float Index) const
{
	if(m_vPb.empty())
		return 0.0f;
	const int Max = (int)m_vPb.size() - 1;
	const float Clamped = std::clamp(Index, 0.0f, (float)Max);
	const int Base = (int)Clamped;
	const int Next = std::min(Base + 1, Max);
	return mix((float)m_vPb[Base].m_Tick, (float)m_vPb[Next].m_Tick, Clamped - Base);
}

void CRunAnalyzer::UpdateProgress(vec2 Pos, int RelTick)
{
	if(m_vPb.size() < 2)
		return;
	const int Last = (int)m_vPb.size() - 1;
	const int From = std::max(0, (int)m_PbIndex - 5);
	const int To = std::min(Last - 1, (int)m_PbIndex + 60);
	float BestDistance = SEARCH_MAX_DISTANCE;
	float BestIndex = -1.0f;
	for(int i = From; i <= To; i++)
	{
		const vec2 A = m_vPb[i].m_Pos;
		const vec2 B = m_vPb[i + 1].m_Pos;
		const vec2 AB = B - A;
		const float LenSq = dot(AB, AB);
		const float T = LenSq > 0.0f ? std::clamp(dot(Pos - A, AB) / LenSq, 0.0f, 1.0f) : 0.0f;
		const float Distance = distance(Pos, A + AB * T);
		if(Distance < BestDistance)
		{
			BestDistance = Distance;
			BestIndex = i + T;
		}
	}
	if(BestIndex < 0.0f)
		return;
	m_PbIndex = std::max(m_PbIndex, BestIndex);

	const float Delta = (RelTick - PbTickAt(m_PbIndex)) / (float)Client()->GameTickSpeed();
	const int Segment = std::min(NUM_SEGMENTS, (int)(m_PbIndex / Last * NUM_SEGMENTS));
	for(int s = 1; s <= Segment; s++)
	{
		if(!m_aSegmentReached[s])
		{
			m_aSegmentReached[s] = true;
			m_aSegmentDelta[s] = Delta;
		}
	}
}

void CRunAnalyzer::DetectEvents(const CGhostCharacter &Prev, const CGhostCharacter &Cur, int *pTypes, int &NumTypes) const
{
	NumTypes = 0;
	const bool WasHooking = Prev.m_HookState == HOOK_FLYING || Prev.m_HookState == HOOK_GRABBED;
	const bool IsHooking = Cur.m_HookState == HOOK_FLYING || Cur.m_HookState == HOOK_GRABBED;
	if(!WasHooking && IsHooking)
		pTypes[NumTypes++] = EVENT_HOOK;
	else if(WasHooking && !IsHooking)
		pTypes[NumTypes++] = EVENT_RELEASE;
	if(Cur.m_VelY < -8 * 256 && Cur.m_VelY < Prev.m_VelY - 4 * 256)
		pTypes[NumTypes++] = EVENT_JUMP;
}

void CRunAnalyzer::JudgeEvent(int Type)
{
	if(m_vPbEvents.empty())
		return;
	float BestOffset = 0.0f;
	bool Found = false;
	for(const SPbEvent &Event : m_vPbEvents)
	{
		if(Event.m_Type != Type || std::fabs(Event.m_Index - m_PbIndex) > EVENT_WINDOW)
			continue;
		const float Offset = PbTickAt(m_PbIndex) - PbTickAt(Event.m_Index);
		if(!Found || std::fabs(Offset) < std::fabs(BestOffset))
		{
			BestOffset = Offset;
			Found = true;
		}
	}
	if(!Found)
		return;
	m_avTiming[Type].push_back(BestOffset);

	if(!g_Config.m_ScRunFeedback)
		return;
	const int Ticks = round_to_int(BestOffset);
	if(Ticks == 0)
	{
		str_format(m_aFeedback, sizeof(m_aFeedback), "%s  on time", gs_apEventNames[Type]);
		m_FeedbackColor = ColorRGBA(0.45f, 0.9f, 0.5f, 1.0f);
	}
	else
	{
		str_format(m_aFeedback, sizeof(m_aFeedback), "%s  %+d tick %s", gs_apEventNames[Type], Ticks, Ticks > 0 ? "late" : "early");
		m_FeedbackColor = std::abs(Ticks) <= 1 ? ColorRGBA(1.0f, 0.85f, 0.4f, 1.0f) : ColorRGBA(1.0f, 0.45f, 0.45f, 1.0f);
	}
	m_FeedbackTime = time_get();
}

void CRunAnalyzer::OnRunTick(const CGhostCharacter &Char, bool Frozen)
{
	if(!m_Running)
		return;
	const int RelTick = Char.m_Tick - m_StartTick;
	const vec2 Pos((float)Char.m_X, (float)Char.m_Y);
	UpdateProgress(Pos, RelTick);

	m_Samples++;
	if(Frozen)
		m_FrozenSamples++;
	const bool Grounded = Collision()->CheckPoint(Pos.x + TEE_HALF, Pos.y + TEE_HALF + 5) || Collision()->CheckPoint(Pos.x - TEE_HALF, Pos.y + TEE_HALF + 5);
	if(!Grounded)
		m_AirSamples++;
	const float Speed = SpeedBps(Char.m_VelX, Char.m_VelY, Client()->GameTickSpeed());
	m_SpeedSum += Speed;
	m_MaxSpeed = std::max(m_MaxSpeed, Speed);

	if(m_HasLast)
	{
		int aTypes[NUM_EVENT_TYPES];
		int NumTypes = 0;
		DetectEvents(m_Last, Char, aTypes, NumTypes);
		for(int t = 0; t < NumTypes; t++)
		{
			if(aTypes[t] == EVENT_HOOK)
				m_Hooks++;
			else if(aTypes[t] == EVENT_JUMP)
				m_Jumps++;
			if(!Frozen)
				JudgeEvent(aTypes[t]);
		}
	}
	m_Last = Char;
	m_HasLast = true;
	m_LastTick = Char.m_Tick;
}

void CRunAnalyzer::OnRunEnd(int TimeMs)
{
	if(!m_Running)
		return;
	m_Running = false;
	const int TickSpeed = std::max(1, Client()->GameTickSpeed());
	const int RunMs = (m_LastTick - m_StartTick) * 1000 / TickSpeed;
	if(RunMs < 1000 && TimeMs <= 0)
		return;

	SReport Report;
	Report.m_Valid = true;
	Report.m_Finished = TimeMs > 0;
	Report.m_TimeMs = TimeMs > 0 ? TimeMs : RunMs;
	Report.m_PbMs = m_PbTimeMs;
	Report.m_Progress = m_vPb.size() > 1 ? (Report.m_Finished ? 1.0f : m_PbIndex / (m_vPb.size() - 1)) : 0.0f;
	const float SampleSeconds = m_Samples > 0 ? RunMs / 1000.0f / m_Samples : 0.0f;
	Report.m_FreezeSeconds = m_FrozenSamples * SampleSeconds;
	Report.m_FreezeShare = m_Samples ? 100.0f * m_FrozenSamples / m_Samples : 0.0f;
	Report.m_AirShare = m_Samples ? 100.0f * m_AirSamples / m_Samples : 0.0f;
	Report.m_AvgSpeed = m_Samples ? m_SpeedSum / m_Samples : 0.0f;
	Report.m_MaxSpeed = m_MaxSpeed;
	Report.m_Hooks = m_Hooks;
	Report.m_Jumps = m_Jumps;

	if(Report.m_Finished)
	{
		m_aSegmentReached[NUM_SEGMENTS] = true;
		m_aSegmentDelta[NUM_SEGMENTS] = m_PbTimeMs > 0 ? (TimeMs - m_PbTimeMs) / 1000.0f : m_aSegmentDelta[NUM_SEGMENTS];
	}
	std::vector<std::pair<float, int>> vLosses;
	for(int s = 1; s <= NUM_SEGMENTS; s++)
		if(m_aSegmentReached[s] && m_aSegmentReached[s - 1])
			vLosses.emplace_back(m_aSegmentDelta[s] - m_aSegmentDelta[s - 1], s - 1);
	std::sort(vLosses.begin(), vLosses.end(), [](const auto &A, const auto &B) { return A.first > B.first; });
	for(int i = 0; i < 3 && i < (int)vLosses.size(); i++)
	{
		if(vLosses[i].first <= 0.01f)
			break;
		Report.m_aWorstLoss[i] = vLosses[i].first;
		Report.m_aWorstSegment[i] = vLosses[i].second;
	}

	for(int t = 0; t < NUM_EVENT_TYPES; t++)
	{
		const std::vector<float> &vTiming = m_avTiming[t];
		Report.m_aTimingCount[t] = (int)vTiming.size();
		if(vTiming.empty())
			continue;
		float AbsSum = 0.0f;
		int Within = 0;
		for(float Offset : vTiming)
		{
			AbsSum += std::fabs(Offset);
			if(std::fabs(Offset) <= 1.0f)
				Within++;
		}
		Report.m_aTimingAvg[t] = AbsSum / vTiming.size();
		Report.m_aTimingWithin[t] = 100.0f * Within / vTiming.size();
	}

	SRunRecord Record;
	Record.m_Timestamp = time_timestamp();
	Record.m_Finished = Report.m_Finished;
	Record.m_TimeMs = Report.m_Finished ? TimeMs : 0;
	Record.m_RunMs = RunMs;
	Record.m_FreezeMs = (int)(Report.m_FreezeSeconds * 1000.0f);
	m_vSessionRuns.push_back(Record);
	m_vAllRuns.push_back(Record);
	if(g_Config.m_ScRunStatsSave)
		SaveRecord(Record);

	m_Report = Report;
	m_Report.m_Valid = Report.m_TimeMs >= 60000;
	m_ReportTime = time_get();
}

void CRunAnalyzer::BuildStatsText(const std::vector<SRunRecord> &vRuns, char *pBuf, int BufSize) const
{
	int Finishes = 0;
	int Best = 0;
	int64_t FinishSum = 0;
	for(const SRunRecord &Run : vRuns)
	{
		if(!Run.m_Finished)
			continue;
		Finishes++;
		FinishSum += Run.m_TimeMs;
		if(Best == 0 || Run.m_TimeMs < Best)
			Best = Run.m_TimeMs;
	}
	if(Finishes == 0)
	{
		str_format(pBuf, BufSize, "%d attempts, no finish yet", (int)vRuns.size());
		return;
	}
	char aBest[32], aAvg[32];
	FormatTime(aBest, sizeof(aBest), Best);
	FormatTime(aAvg, sizeof(aAvg), (int)(FinishSum / Finishes));

	std::vector<int> vFinishTimes;
	for(const SRunRecord &Run : vRuns)
		if(Run.m_Finished)
			vFinishTimes.push_back(Run.m_TimeMs);
	char aTrend[64] = "";
	if(vFinishTimes.size() >= 6)
	{
		const size_t Half = std::min<size_t>(5, vFinishTimes.size() / 2);
		int64_t First = 0, Recent = 0;
		for(size_t i = 0; i < Half; i++)
		{
			First += vFinishTimes[i];
			Recent += vFinishTimes[vFinishTimes.size() - 1 - i];
		}
		const float Change = (Recent - First) / (float)Half / 1000.0f;
		str_format(aTrend, sizeof(aTrend), ", trend %+.2fs", Change);
	}
	str_format(pBuf, BufSize, "%d attempts, %d finishes (%d%%), best %s, avg %s%s", (int)vRuns.size(), Finishes, (int)(100.0f * Finishes / vRuns.size()), aBest, aAvg, aTrend);
}

void CRunAnalyzer::RenderFeedback()
{
	if(!g_Config.m_ScRunFeedback || m_aFeedback[0] == '\0')
		return;
	const float Age = (time_get() - m_FeedbackTime) / (float)time_freq();
	if(Age > 1.5f)
		return;
	const float Alpha = Age < 1.0f ? 1.0f : 1.0f - (Age - 1.0f) / 0.5f;
	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(Width, Height);
	CUIRect Label = {Width / 2.0f - 60.0f, Height * 0.62f, 120.0f, 10.0f};
	TextRender()->TextColor(m_FeedbackColor.WithAlpha(Alpha));
	TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f * Alpha));
	Ui()->DoLabel(&Label, m_aFeedback, 7.0f, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
}

void CRunAnalyzer::RenderReport()
{
	if(!g_Config.m_ScRunReport || !m_Report.m_Valid)
		return;
	const float Age = (time_get() - m_ReportTime) / (float)time_freq();
	const float Duration = (float)g_Config.m_ScRunReportTime;
	if(Age > Duration)
		return;
	const float Alpha = Age < Duration - 0.5f ? 1.0f : (Duration - Age) / 0.5f;

	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(Width, Height);

	const SReport &R = m_Report;
	char aaLines[7][160];
	int NumLines = 0;
	char aTime[32], aPb[32];
	FormatTime(aTime, sizeof(aTime), R.m_TimeMs);
	if(R.m_Finished)
	{
		if(R.m_PbMs > 0)
		{
			FormatTime(aPb, sizeof(aPb), R.m_PbMs);
			str_format(aaLines[NumLines++], sizeof(aaLines[0]), "Finished %s  (PB %s, %+.2fs)", aTime, aPb, (R.m_TimeMs - R.m_PbMs) / 1000.0f);
		}
		else
			str_format(aaLines[NumLines++], sizeof(aaLines[0]), "Finished %s", aTime);
	}
	else if(R.m_Progress > 0.0f)
		str_format(aaLines[NumLines++], sizeof(aaLines[0]), "Run ended at %d%% of the map after %s", (int)(R.m_Progress * 100.0f), aTime);
	else
		str_format(aaLines[NumLines++], sizeof(aaLines[0]), "Run ended after %s", aTime);

	str_format(aaLines[NumLines++], sizeof(aaLines[0]), "Freeze %.1fs (%.0f%%)  ·  Air %.0f%%  ·  Speed avg %.1f / max %.1f bps", R.m_FreezeSeconds, R.m_FreezeShare, R.m_AirShare, R.m_AvgSpeed, R.m_MaxSpeed);
	str_format(aaLines[NumLines++], sizeof(aaLines[0]), "Hooks %d  ·  Jumps %d", R.m_Hooks, R.m_Jumps);

	if(R.m_aWorstSegment[0] >= 0)
	{
		char aLoss[160] = "Lost most time at";
		for(int i = 0; i < 3 && R.m_aWorstSegment[i] >= 0; i++)
		{
			char aPart[48];
			str_format(aPart, sizeof(aPart), "%s %d-%d%% (+%.2fs)", i ? "," : "", R.m_aWorstSegment[i] * 10, R.m_aWorstSegment[i] * 10 + 10, R.m_aWorstLoss[i]);
			str_append(aLoss, aPart, sizeof(aLoss));
		}
		str_copy(aaLines[NumLines++], aLoss, sizeof(aaLines[0]));
	}

	if(R.m_aTimingCount[EVENT_HOOK] + R.m_aTimingCount[EVENT_JUMP] + R.m_aTimingCount[EVENT_RELEASE] > 0)
	{
		char aTiming[160] = "Timing vs PB:";
		for(int t = 0; t < NUM_EVENT_TYPES; t++)
		{
			if(R.m_aTimingCount[t] == 0)
				continue;
			char aPart[64];
			str_format(aPart, sizeof(aPart), " %s %.1f ticks off (%.0f%% within 1)", gs_apEventNames[t], R.m_aTimingAvg[t], R.m_aTimingWithin[t]);
			str_append(aTiming, aPart, sizeof(aTiming));
		}
		str_copy(aaLines[NumLines++], aTiming, sizeof(aaLines[0]));
	}
	else if(R.m_PbMs <= 0)
		str_copy(aaLines[NumLines++], "Finish once to get a PB ghost for timing and split analysis", sizeof(aaLines[0]));

	char aSession[200];
	BuildStatsText(m_vSessionRuns, aSession, sizeof(aSession));
	str_format(aaLines[NumLines++], sizeof(aaLines[0]), "Session: %s", aSession);

	const float FontSize = 6.0f;
	const float LineHeight = 8.5f;
	float MaxWidth = 0.0f;
	for(int i = 0; i < NumLines; i++)
		MaxWidth = std::max(MaxWidth, TextRender()->TextWidth(i == 0 ? FontSize * 1.2f : FontSize, aaLines[i]));
	CUIRect Box = {Width - MaxWidth - 18.0f, Height * 0.2f, MaxWidth + 12.0f, NumLines * LineHeight + 2.0f * LineHeight * 0.2f + 8.0f};
	Box.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.55f * Alpha), IGraphics::CORNER_ALL, 4.0f);
	Box.Margin(5.0f, &Box);
	for(int i = 0; i < NumLines; i++)
	{
		CUIRect Line;
		Box.HSplitTop(i == 0 ? LineHeight * 1.2f : LineHeight, &Line, &Box);
		if(i == 0)
			TextRender()->TextColor(R.m_Finished ? ColorRGBA(0.45f, 0.9f, 0.5f, Alpha) : ColorRGBA(1.0f, 0.75f, 0.45f, Alpha));
		else
			TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, (i == NumLines - 1 ? 0.6f : 0.9f) * Alpha));
		Ui()->DoLabel(&Line, aaLines[i], i == 0 ? FontSize * 1.2f : FontSize, TEXTALIGN_ML);
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CRunAnalyzer::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	RenderFeedback();
	RenderReport();
}
