/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-d47c8b1a */

#include "fast_input_log.h"

#include <base/io.h>
#include <base/math.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace
{
	constexpr const char *LOG_FOLDER = "667Client/input_logs";
	constexpr float BIG_ERROR = 2.0f;
	constexpr float TELEPORT_DISTANCE = 160.0f;

	float Percentile(std::vector<float> vValues, float Fraction)
	{
		if(vValues.empty())
			return 0.0f;
		std::sort(vValues.begin(), vValues.end());
		return vValues[std::min(vValues.size() - 1, (size_t)(Fraction * vValues.size()))];
	}

	const char *gs_apKeyNames[] = {"left", "right", "jump", "hook", "fire"};

	const char *ModeName(int Mode)
	{
		switch(Mode)
		{
		case SC_INPUTS_FAST: return "fast";
		case SC_INPUTS_BEST: return "best";
		case SC_INPUTS_SAIKO: return "saiko";
		case SC_INPUTS_CLOUD: return "cloud";
		case SC_INPUTS_SIX: return "667";
		case SC_INPUTS_MEOW: return "meow";
		default: return "off";
		}
	}
}

void CFastInputLog::OnConsoleInit()
{
	Console()->Register("sc_input_log", "", CFGFLAG_CLIENT, ConToggle, this, "Start or stop recording a fast input log");
	Console()->Register("sc_input_log_mark", "", CFGFLAG_CLIENT, ConMark, this, "Mark the current moment in the fast input log");
	// 667client: the delta and f modes were removed; saved configs fall back to fast input.
	Console()->Chain("sc_inputs", ConchainInputs, this);
	Console()->Register("sc_input_calibrate", "", CFGFLAG_CLIENT, ConCalibrate, this, "Play for two minutes to find the best 667 input settings for you");
}

void CFastInputLog::ConchainInputs(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(g_Config.m_ScInputs == SC_INPUTS_REMOVED_DELTA || g_Config.m_ScInputs == SC_INPUTS_REMOVED_F)
		g_Config.m_ScInputs = SC_INPUTS_FAST;
}

void CFastInputLog::OnStateChange(int NewState, int OldState)
{
	if(m_Active && NewState != IClient::STATE_ONLINE && OldState == IClient::STATE_ONLINE)
		Stop();
}

void CFastInputLog::OnShutdown()
{
	if(m_Active)
		Stop();
}

void CFastInputLog::ConToggle(IConsole::IResult *pResult, void *pUserData)
{
	CFastInputLog *pSelf = static_cast<CFastInputLog *>(pUserData);
	if(pSelf->m_Active)
		pSelf->Stop();
	else
		pSelf->Start();
}

void CFastInputLog::ConCalibrate(IConsole::IResult *pResult, void *pUserData)
{
	CFastInputLog *pSelf = static_cast<CFastInputLog *>(pUserData);
	if(pSelf->m_Calibrating)
		pSelf->CancelCalibration();
	else
		pSelf->StartCalibration();
}

void CFastInputLog::ConMark(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CFastInputLog *>(pUserData)->Mark();
}

double CFastInputLog::Now() const
{
	return (time_get() - m_StartTime) * 1000.0 / (double)time_freq();
}

void CFastInputLog::WriteLine(IOHANDLE File, const char *pLine)
{
	if(!File)
		return;
	io_write(File, pLine, str_length(pLine));
	io_write_newline(File);
}

void CFastInputLog::Message(const char *pText)
{
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "input_log", pText);
	if(Client()->State() == IClient::STATE_ONLINE)
		GameClient()->Echo(pText);
}

void CFastInputLog::Start()
{
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		Message("Join a server first, the log records live gameplay.");
		return;
	}

	Storage()->CreateFolder("667Client", IStorage::TYPE_SAVE);
	Storage()->CreateFolder(LOG_FOLDER, IStorage::TYPE_SAVE);
	char aTimestamp[64];
	str_timestamp(aTimestamp, sizeof(aTimestamp));
	str_format(m_aBaseName, sizeof(m_aBaseName), "%s/input_%s", LOG_FOLDER, aTimestamp);

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s_ticks.csv", m_aBaseName);
	m_TicksFile = Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	str_format(aPath, sizeof(aPath), "%s_events.csv", m_aBaseName);
	m_EventsFile = Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	str_format(aPath, sizeof(aPath), "%s_frames.csv", m_aBaseName);
	m_FramesFile = Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!m_TicksFile || !m_EventsFile || !m_FramesFile)
	{
		if(m_TicksFile)
			io_close(m_TicksFile);
		if(m_EventsFile)
			io_close(m_EventsFile);
		if(m_FramesFile)
			io_close(m_FramesFile);
		m_TicksFile = m_EventsFile = m_FramesFile = nullptr;
		Message("Could not create the input log files.");
		return;
	}

	WriteLine(m_TicksFile, "time_ms,tick,mode,offset_ticks,prediction_ms,pos_x,pos_y,vel_x,vel_y,frozen,direction,jump,hook,fire,weapon,target_x,target_y,changed,err_h1,err_h2,err_h3");
	WriteLine(m_EventsFile, "time_ms,type,value_a,value_b,value_c");
	WriteLine(m_FramesFile, "time_ms,frame_dt_ms,raw_x,raw_y,final_x,final_y,lead,smoothing");

	char aInfo[256];
	str_format(aInfo, sizeof(aInfo), "0,info,%s,%d,%d", ModeName(g_Config.m_ScInputs), g_Config.m_ClSubTickAiming, Client()->GameTickSpeed());
	WriteLine(m_EventsFile, aInfo);

	for(auto &aGuessTick : m_aaGuessTick)
		std::fill(std::begin(aGuessTick), std::end(aGuessTick), -1);
	m_LastTick = -1;
	m_HasLastInput = false;
	m_DirectionRun = 0;
	m_LastDirection = 0;
	m_Ticks = m_MovingTicks = m_Frames = m_Marks = 0;
	std::fill(std::begin(m_aChanges), std::end(m_aChanges), 0);
	m_vDirectionHolds.clear();
	m_PredMsSum = m_PredMsSqSum = 0.0;
	m_PredMsMin = 1 << 30;
	m_PredMsMax = 0;
	std::fill(std::begin(m_aErrorSum), std::end(m_aErrorSum), 0.0);
	std::fill(std::begin(m_aErrorCount), std::end(m_aErrorCount), 0);
	std::fill(std::begin(m_aErrorBig), std::end(m_aErrorBig), 0);
	m_InputEvents = 0;
	m_InputIntraSum = 0.0;
	m_Corrections = 0;
	m_vShifts.clear();
	m_vJumpShifts.clear();
	m_vJumpScreenSteps.clear();
	m_vPresses.clear();
	std::fill(std::begin(m_aOpenPress), std::end(m_aOpenPress), -1);
	std::fill(std::begin(m_aKeyDown), std::end(m_aKeyDown), false);
	std::fill(std::begin(m_aLastUp), std::end(m_aLastUp), -1000.0);
	m_PendingKeyMask = 0;
	m_HasLastSentFire = false;
	m_OverlapStart = -1.0;
	m_OverlapTotal = 0.0;
	m_Overlaps = 0;
	m_Reversals = 0;
	for(auto &vShifts : m_avKeyShifts)
		vShifts.clear();
	for(SPredState &State : m_aPredStates)
		State.m_Tick = -1;
	m_ServerChecks = m_ServerPosMismatches = m_ServerHookMismatches = m_ServerHookedMismatches = 0;
	m_ServerPosMax = 0.0f;
	m_JumpWatchUntil = -1.0;
	m_JumpWatchMax = 0.0f;
	m_HasLastFinalPos = false;
	m_CorrectionSum = m_CorrectionMax = 0.0;
	m_LeadSum = m_SmoothSum = 0.0;
	m_FrameDtSum = m_FrameDtMax = 0.0;
	m_OffsetSum = 0.0f;
	m_StartTime = time_get();
	m_LastFrameTime = 0;
	m_Active = true;

	if(!m_Quiet)
	{
		char aMsg[256];
		str_format(aMsg, sizeof(aMsg), "Input log started (%s mode). Play normally, run sc_input_log again to stop.", ModeName(g_Config.m_ScInputs));
		Message(aMsg);
	}
}

void CFastInputLog::Mark()
{
	if(!m_Active)
	{
		Message("The input log is not running.");
		return;
	}
	m_Marks++;
	char aLine[128];
	str_format(aLine, sizeof(aLine), "%.1f,mark,%d,0,0", Now(), m_Marks);
	WriteLine(m_EventsFile, aLine);
	char aMsg[64];
	str_format(aMsg, sizeof(aMsg), "Marked moment #%d", m_Marks);
	Message(aMsg);
}

void CFastInputLog::OnPredicted(int RegularTick, int FinalTick, const vec2 *pPredPos, const int *pPredTick, const CCharacterCore &RegularCore, const CNetObj_PlayerInput *pInput, float OffsetTicks, int PredictionMs)
{
	if(!m_Active || RegularTick <= 0)
		return;
	const vec2 Vel = RegularCore.m_Vel;
	const bool Frozen = RegularCore.m_FreezeEnd != 0 || RegularCore.m_DeepFrozen;
	SPredState &PredState = m_aPredStates[RegularTick % HISTORY];
	PredState.m_Tick = RegularTick;
	PredState.m_Pos = RegularCore.m_Pos;
	PredState.m_HookState = RegularCore.m_HookState;
	PredState.m_HookedPlayer = RegularCore.HookedPlayer();

	if(RegularTick > m_LastTick && pPredTick[RegularTick % HISTORY] == RegularTick)
	{
		const vec2 Pos = pPredPos[RegularTick % HISTORY];
		CNetObj_PlayerInput Input = pInput ? *pInput : CNetObj_PlayerInput{};

		float aError[MAX_HORIZON + 1];
		const bool Moving = length(Vel) > 0.1f;
		for(int h = 1; h <= MAX_HORIZON; h++)
		{
			aError[h] = -1.0f;
			const int Slot = RegularTick % HISTORY;
			if(m_aaGuessTick[h][Slot] != RegularTick)
				continue;
			const float Error = distance(m_aaGuessPos[h][Slot], Pos);
			if(Error >= TELEPORT_DISTANCE)
				continue;
			aError[h] = Error;
			if(Moving)
			{
				m_aErrorSum[h] += Error;
				m_aErrorCount[h]++;
				if(Error > BIG_ERROR)
					m_aErrorBig[h]++;
			}
		}

		int Changed = 0;
		if(m_HasLastInput)
		{
			if(Input.m_Direction != m_LastInput.m_Direction)
				Changed |= 1;
			if((Input.m_Jump != 0) != (m_LastInput.m_Jump != 0))
				Changed |= 2;
			if((Input.m_Hook != 0) != (m_LastInput.m_Hook != 0))
				Changed |= 4;
			if(Input.m_Fire != m_LastInput.m_Fire)
				Changed |= 8;
			if(Input.m_WantedWeapon != m_LastInput.m_WantedWeapon)
				Changed |= 16;
			for(int i = 0; i < 5; i++)
				if(Changed & (1 << i))
					m_aChanges[i]++;
		}
		if(Input.m_Direction == m_LastDirection && Input.m_Direction != 0)
			m_DirectionRun++;
		else
		{
			if(m_LastDirection != 0 && m_DirectionRun > 0)
				m_vDirectionHolds.push_back(m_DirectionRun);
			m_DirectionRun = Input.m_Direction != 0 ? 1 : 0;
		}
		m_LastDirection = Input.m_Direction;
		m_LastInput = Input;
		m_HasLastInput = true;

		m_Ticks++;
		if(Moving)
			m_MovingTicks++;
		m_PredMsSum += PredictionMs;
		m_PredMsSqSum += (double)PredictionMs * PredictionMs;
		m_PredMsMin = std::min(m_PredMsMin, PredictionMs);
		m_PredMsMax = std::max(m_PredMsMax, PredictionMs);
		m_OffsetSum += OffsetTicks;

		char aLine[512];
		str_format(aLine, sizeof(aLine), "%.1f,%d,%s,%.3f,%d,%.2f,%.2f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.3f,%.3f,%.3f",
			Now(), RegularTick, ModeName(g_Config.m_ScInputs), OffsetTicks, PredictionMs, Pos.x, Pos.y, Vel.x, Vel.y, Frozen ? 1 : 0,
			Input.m_Direction, Input.m_Jump, Input.m_Hook, Input.m_Fire, Input.m_WantedWeapon, Input.m_TargetX, Input.m_TargetY, Changed,
			aError[1], aError[2], aError[3]);
		WriteLine(m_TicksFile, aLine);
		m_LastTick = RegularTick;
	}

	for(int Tick = RegularTick + 1; Tick <= FinalTick && Tick - RegularTick <= MAX_HORIZON; Tick++)
	{
		const int Slot = Tick % HISTORY;
		if(pPredTick[Slot] != Tick)
			continue;
		const int Horizon = Tick - RegularTick;
		m_aaGuessPos[Horizon][Slot] = pPredPos[Slot];
		m_aaGuessTick[Horizon][Slot] = Tick;
	}
}

void CFastInputLog::OnInputEvent(float IntraTick)
{
	if(!m_Active)
		return;
	m_InputEvents++;
	m_InputIntraSum += IntraTick;
	char aLine[128];
	str_format(aLine, sizeof(aLine), "%.1f,input,%.3f,0,0", Now(), IntraTick);
	WriteLine(m_EventsFile, aLine);
}

void CFastInputLog::OnCorrection(vec2 Before, vec2 After, bool InputChanged, bool JumpPressed)
{
	if(!m_Active)
		return;
	const float Length = distance(Before, After);
	if(Length < 0.01f || Length >= TELEPORT_DISTANCE)
		return;
	if(InputChanged)
	{
		(JumpPressed ? m_vJumpShifts : m_vShifts).push_back(Length);
		for(int Key = 0; Key < NUM_KEYS; Key++)
			if(m_PendingKeyMask & (1 << Key))
				m_avKeyShifts[Key].push_back(Length);
		m_PendingKeyMask = 0;
		if(JumpPressed)
		{
			if(m_JumpWatchUntil >= 0.0)
				m_vJumpScreenSteps.push_back(m_JumpWatchMax);
			m_JumpWatchUntil = Now() + 60.0;
			m_JumpWatchMax = 0.0f;
		}
	}
	else
	{
		m_Corrections++;
		m_CorrectionSum += Length;
		m_CorrectionMax = std::max(m_CorrectionMax, (double)Length);
	}
	char aLine[128];
	str_format(aLine, sizeof(aLine), "%.1f,%s,%.3f,%.3f,%.3f", Now(), InputChanged ? (JumpPressed ? "jump_shift" : "input_shift") : "correction", Length, After.x - Before.x, After.y - Before.y);
	WriteLine(m_EventsFile, aLine);
}

void CFastInputLog::OnFrame(vec2 RawPos, vec2 FinalPos, vec2 RegularPos, bool HasRegular)
{
	if(!m_Active)
		return;
	const int64_t Time = time_get();
	const double FrameDt = m_LastFrameTime ? (Time - m_LastFrameTime) * 1000.0 / (double)time_freq() : 0.0;
	m_LastFrameTime = Time;
	const float Lead = HasRegular ? distance(RawPos, RegularPos) : 0.0f;
	const float Smoothing = distance(RawPos, FinalPos);
	const double FrameTime = Now();
	if(m_JumpWatchUntil >= 0.0)
	{
		if(m_HasLastFinalPos)
			m_JumpWatchMax = std::max(m_JumpWatchMax, distance(m_LastFinalPos, FinalPos));
		if(FrameTime > m_JumpWatchUntil)
		{
			m_vJumpScreenSteps.push_back(m_JumpWatchMax);
			m_JumpWatchUntil = -1.0;
		}
	}
	m_LastFinalPos = FinalPos;
	m_HasLastFinalPos = true;
	m_Frames++;
	m_LeadSum += Lead;
	m_SmoothSum += Smoothing;
	m_FrameDtSum += FrameDt;
	m_FrameDtMax = std::max(m_FrameDtMax, FrameDt);

	char aLine[256];
	str_format(aLine, sizeof(aLine), "%.2f,%.3f,%.2f,%.2f,%.2f,%.2f,%.3f,%.3f", Now(), FrameDt, RawPos.x, RawPos.y, FinalPos.x, FinalPos.y, Lead, Smoothing);
	WriteLine(m_FramesFile, aLine);
}

void CFastInputLog::OnKey(int Key, bool Pressed, vec2 MousePos, float IntraTick)
{
	if(!m_Active || Key < 0 || Key >= NUM_KEYS)
		return;
	const double Time = Now();
	if(Pressed == m_aKeyDown[Key] && Key != KEY_FIRE)
		return;
	m_aKeyDown[Key] = Pressed;

	char aLine[160];
	str_format(aLine, sizeof(aLine), "%.1f,%s_%s,%.3f,%.1f,%.1f", Time, gs_apKeyNames[Key], Pressed ? "down" : "up", IntraTick, MousePos.x, MousePos.y);
	WriteLine(m_EventsFile, aLine);

	if(Key == KEY_LEFT || Key == KEY_RIGHT)
	{
		const int Other = Key == KEY_LEFT ? KEY_RIGHT : KEY_LEFT;
		if(Pressed && m_aKeyDown[Other])
		{
			m_OverlapStart = Time;
			m_Overlaps++;
		}
		else if(!Pressed && m_OverlapStart >= 0.0)
		{
			m_OverlapTotal += Time - m_OverlapStart;
			m_OverlapStart = -1.0;
		}
		if(Pressed && (m_aKeyDown[Other] || Time - m_aLastUp[Other] < 50.0))
			m_Reversals++;
	}

	if(Pressed)
	{
		m_PendingKeyMask |= 1 << Key;
		SPress Press;
		Press.m_Key = Key;
		Press.m_Down = Time;
		Press.m_Intra = IntraTick;
		Press.m_Mouse = MousePos;
		m_vPresses.push_back(Press);
		m_aOpenPress[Key] = (int)m_vPresses.size() - 1;
	}
	else
	{
		m_aLastUp[Key] = Time;
		if(m_aOpenPress[Key] >= 0)
			m_vPresses[m_aOpenPress[Key]].m_Up = Time;
	}
}

void CFastInputLog::OnSend(const CNetObj_PlayerInput &Input)
{
	if(!m_Active)
		return;
	const double Time = Now();
	const bool FireChanged = m_HasLastSentFire && Input.m_Fire != m_LastSentFire;
	m_LastSentFire = Input.m_Fire;
	m_HasLastSentFire = true;

	for(int Key = 0; Key < NUM_KEYS; Key++)
	{
		const int Index = m_aOpenPress[Key];
		if(Index < 0)
			continue;
		SPress &Press = m_vPresses[Index];
		if(Press.m_Sent >= 0.0 || Press.m_Lost)
			continue;
		bool Contains = false;
		switch(Key)
		{
		case KEY_LEFT: Contains = Input.m_Direction == -1; break;
		case KEY_RIGHT: Contains = Input.m_Direction == 1; break;
		case KEY_JUMP: Contains = Input.m_Jump != 0; break;
		case KEY_HOOK: Contains = Input.m_Hook != 0; break;
		case KEY_FIRE: Contains = FireChanged; break;
		}
		if(Contains)
		{
			Press.m_Sent = Time;
			if((Key == KEY_HOOK || Key == KEY_FIRE) && length(Press.m_Mouse) > 0.0f)
			{
				const vec2 Target((float)Input.m_TargetX, (float)Input.m_TargetY);
				if(length(Target) > 0.0f)
				{
					const float Dot = std::clamp(dot(normalize(Press.m_Mouse), normalize(Target)), -1.0f, 1.0f);
					Press.m_AimError = std::acos(Dot) * 180.0f / pi;
				}
			}
			char aLine[160];
			str_format(aLine, sizeof(aLine), "%.1f,%s_sent,%.2f,%.2f,0", Time, gs_apKeyNames[Key], Time - Press.m_Down, Press.m_AimError);
			WriteLine(m_EventsFile, aLine);
		}
		else if(Press.m_Up >= 0.0)
		{
			Press.m_Lost = true;
			char aLine[160];
			str_format(aLine, sizeof(aLine), "%.1f,%s_lost,%.2f,0,0", Time, gs_apKeyNames[Key], Press.m_Up - Press.m_Down);
			WriteLine(m_EventsFile, aLine);
		}
	}
}

void CFastInputLog::OnServerState(int Tick, const CNetObj_Character &Char)
{
	if(!m_Active || Tick <= 0)
		return;
	const SPredState &State = m_aPredStates[Tick % HISTORY];
	if(State.m_Tick != Tick)
		return;
	m_ServerChecks++;
	const vec2 ServerPos((float)Char.m_X, (float)Char.m_Y);
	const float PosError = distance(State.m_Pos, ServerPos);
	const bool PredGrabbed = State.m_HookState == HOOK_GRABBED;
	const bool ServerGrabbed = Char.m_HookState == HOOK_GRABBED;
	const bool HookMismatch = PredGrabbed != ServerGrabbed;
	const bool HookedMismatch = State.m_HookedPlayer != Char.m_HookedPlayer;
	if(PosError < TELEPORT_DISTANCE && PosError > BIG_ERROR)
	{
		m_ServerPosMismatches++;
		m_ServerPosMax = std::max(m_ServerPosMax, PosError);
	}
	if(HookMismatch)
		m_ServerHookMismatches++;
	if(HookedMismatch)
		m_ServerHookedMismatches++;
	if((PosError > BIG_ERROR && PosError < TELEPORT_DISTANCE) || HookMismatch || HookedMismatch)
	{
		char aLine[200];
		str_format(aLine, sizeof(aLine), "%.1f,server_mismatch,%.2f,%d/%d,%d/%d", Now(), PosError, State.m_HookState, Char.m_HookState, State.m_HookedPlayer, Char.m_HookedPlayer);
		WriteLine(m_EventsFile, aLine);
	}
}

void CFastInputLog::Stop()
{
	if(!m_Active)
		return;
	m_Active = false;
	if(m_LastDirection != 0 && m_DirectionRun > 0)
		m_vDirectionHolds.push_back(m_DirectionRun);
	io_close(m_TicksFile);
	io_close(m_EventsFile);
	io_close(m_FramesFile);
	m_TicksFile = m_EventsFile = m_FramesFile = nullptr;

	const double Seconds = std::max(0.001, Now() / 1000.0);
	const double TickMs = 1000.0 / std::max(1, Client()->GameTickSpeed());
	const int Ticks = std::max(1, m_Ticks);
	const double PredAvg = m_PredMsSum / Ticks;
	const double PredJitter = std::sqrt(std::max(0.0, m_PredMsSqSum / Ticks - PredAvg * PredAvg));
	double aErrAvg[MAX_HORIZON + 1] = {};
	for(int h = 1; h <= MAX_HORIZON; h++)
	{
		aErrAvg[h] = m_aErrorCount[h] ? m_aErrorSum[h] / m_aErrorCount[h] : -1.0;
	}

	std::sort(m_vDirectionHolds.begin(), m_vDirectionHolds.end());
	const int HoldMedian = m_vDirectionHolds.empty() ? 0 : m_vDirectionHolds[m_vDirectionHolds.size() / 2];
	const int ShortHolds = (int)std::count_if(m_vDirectionHolds.begin(), m_vDirectionHolds.end(), [](int Hold) { return Hold <= 3; });
	const double ShortShare = m_vDirectionHolds.empty() ? 0.0 : 100.0 * ShortHolds / m_vDirectionHolds.size();
	const double CorrectionsPerSec = m_Corrections / Seconds;
	const double CorrectionAvg = m_Corrections ? m_CorrectionSum / m_Corrections : 0.0;

	const double CurrentOffset = m_OffsetSum / Ticks;
	double RecommendedOffset = 1.0;
	const double ShiftWorst = Percentile(m_vShifts, 0.9f);
	if(ShiftWorst > 0.0 && CurrentOffset > 0.0)
		RecommendedOffset = 4.0 / ShiftWorst * (0.5 + CurrentOffset) - 0.5;
	RecommendedOffset = std::clamp(std::round(RecommendedOffset * 20.0) / 20.0, 0.6, 1.5);
	const int RecommendedSmoothing = CorrectionsPerSec < 0.5 ? 0 : (CorrectionAvg > 6.0 ? 30 : 15);
	const bool RecommendAdaptive = PredJitter > 8.0 || CorrectionsPerSec > 3.0;

	char aSummary[4096];
	str_format(aSummary, sizeof(aSummary),
		"Cecen Client fast input log\n"
		"mode: %s, average offset: %.2f ticks, sub-tick aiming: %d\n"
		"duration: %.1f s, ticks: %d (%d moving), frames: %d, average fps: %.0f, worst frame: %.1f ms\n"
		"\n"
		"network\n"
		"  prediction time: avg %.1f ms, min %d, max %d, jitter %.1f ms\n"
		"\n"
		"your inputs (per second)\n"
		"  direction: %.2f, jump: %.2f, hook: %.2f, fire: %.2f, weapon: %.2f\n"
		"  direction hold median: %d ticks (%.0f ms), taps of 3 ticks or less: %.0f%%\n"
		"  input events seen: %d, average press point inside the tick: %.2f\n"
		"\n"
		"position jump when your input changes (game units, 32 = one tile)\n"
		"  direction/hook/fire: %d times, median %.2f, worst 10%% %.2f, max %.2f\n"
		"  jump: %d times, median %.2f, worst 10%% %.2f, max %.2f\n"
		"  jump on screen (biggest single frame step after easing): median %.2f, max %.2f\n"
		"  2 ticks ahead guess error: avg %.2f\n"
		"\n"
		"server corrections (not caused by your input)\n"
		"  %.2f per second, avg %.2f units, max %.2f units\n"
		"\n"
		"visuals\n"
		"  average lead over regular prediction: %.2f units, average smoothing offset: %.2f units\n"
		"\n"
		"recommendation\n"
		"  sc_inputs 7\n"
		"  sc_667_input_amount %d\n"
		"  sc_667_input_smoothing %d\n"
		"  sc_667_input_adaptive %d\n"
		"  marks: %d\n",
		ModeName(g_Config.m_ScInputs), CurrentOffset, g_Config.m_ClSubTickAiming,
		Seconds, m_Ticks, m_MovingTicks, m_Frames, m_Frames / Seconds, m_FrameDtMax,
		PredAvg, m_PredMsMin == (1 << 30) ? 0 : m_PredMsMin, m_PredMsMax, PredJitter,
		m_aChanges[0] / Seconds, m_aChanges[1] / Seconds, m_aChanges[2] / Seconds, m_aChanges[3] / Seconds, m_aChanges[4] / Seconds,
		HoldMedian, HoldMedian * TickMs, ShortShare,
		m_InputEvents, m_InputEvents ? m_InputIntraSum / m_InputEvents : 0.0,
		(int)m_vShifts.size(), Percentile(m_vShifts, 0.5f), Percentile(m_vShifts, 0.9f), Percentile(m_vShifts, 1.0f),
		(int)m_vJumpShifts.size(), Percentile(m_vJumpShifts, 0.5f), Percentile(m_vJumpShifts, 0.9f), Percentile(m_vJumpShifts, 1.0f),
		Percentile(m_vJumpScreenSteps, 0.5f), Percentile(m_vJumpScreenSteps, 1.0f),
		aErrAvg[2],
		CorrectionsPerSec, CorrectionAvg, m_CorrectionMax,
		m_Frames ? m_LeadSum / m_Frames : 0.0, m_Frames ? m_SmoothSum / m_Frames : 0.0,
		(int)std::round(RecommendedOffset * 100.0), RecommendedSmoothing, RecommendAdaptive ? 1 : 0,
		m_Marks);

	if(m_OverlapStart >= 0.0)
		m_OverlapTotal += Now() - m_OverlapStart;

	std::string Details = "\nkeys in detail\n";
	std::vector<std::string> vProblems;
	for(int Key = 0; Key < NUM_KEYS; Key++)
	{
		int Presses = 0, Taps = 0, Lost = 0;
		std::vector<float> vSendDelay, vHold, vAim;
		for(const SPress &Press : m_vPresses)
		{
			if(Press.m_Key != Key)
				continue;
			Presses++;
			if(Press.m_Up >= 0.0)
			{
				vHold.push_back((float)(Press.m_Up - Press.m_Down));
				if(Press.m_Up - Press.m_Down < TickMs)
					Taps++;
			}
			if(Press.m_Lost)
				Lost++;
			if(Press.m_Sent >= 0.0)
				vSendDelay.push_back((float)(Press.m_Sent - Press.m_Down));
			if(Press.m_AimError >= 0.0f)
				vAim.push_back(Press.m_AimError);
		}
		if(Presses == 0)
			continue;
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "  %-5s presses %d (%.2f/s), hold median %.0f ms, shorter than a tick: %d, lost: %d\n"
					       "        press to send: median %.1f ms, worst 10%% %.1f ms, max %.1f ms\n"
					       "        position jump on press: median %.2f, worst 10%% %.2f, max %.2f\n",
			gs_apKeyNames[Key], Presses, Presses / Seconds, Percentile(vHold, 0.5f), Taps, Lost,
			Percentile(vSendDelay, 0.5f), Percentile(vSendDelay, 0.9f), Percentile(vSendDelay, 1.0f),
			Percentile(m_avKeyShifts[Key], 0.5f), Percentile(m_avKeyShifts[Key], 0.9f), Percentile(m_avKeyShifts[Key], 1.0f));
		Details += aBuf;
		if(!vAim.empty())
		{
			str_format(aBuf, sizeof(aBuf), "        aim drift between click and send: median %.2f deg, worst 10%% %.2f deg, max %.2f deg\n", Percentile(vAim, 0.5f), Percentile(vAim, 0.9f), Percentile(vAim, 1.0f));
			Details += aBuf;
			if(Percentile(vAim, 0.9f) > 2.0f)
			{
				str_format(aBuf, sizeof(aBuf), "%s aim drifts up to %.1f deg between click and send, enable cl_sub_tick_aiming", gs_apKeyNames[Key], Percentile(vAim, 0.9f));
				vProblems.push_back(aBuf);
			}
		}
		if(Lost > 0)
		{
			str_format(aBuf, sizeof(aBuf), "%d %s presses never reached the server, enable sc_input_debounce", Lost, gs_apKeyNames[Key]);
			vProblems.push_back(aBuf);
		}
		if(Percentile(vSendDelay, 1.0f) > TickMs * 1.6)
		{
			str_format(aBuf, sizeof(aBuf), "some %s presses waited %.0f ms before being sent (more than a tick)", gs_apKeyNames[Key], Percentile(vSendDelay, 1.0f));
			vProblems.push_back(aBuf);
		}
	}
	{
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "  A+D held together: %d times, %.0f ms in total, direction reversals: %d\n", m_Overlaps, m_OverlapTotal, m_Reversals);
		Details += aBuf;
		if(m_Overlaps > 0 && m_Reversals > 0 && m_Overlaps * 3 > m_Reversals && !g_Config.m_ScSnapTap)
		{
			str_format(aBuf, sizeof(aBuf), "A and D overlap in %d of %d reversals and cancel each other out, enable Snap Tap", m_Overlaps, m_Reversals);
			vProblems.push_back(aBuf);
		}
		str_format(aBuf, sizeof(aBuf), "\nserver vs prediction (%d ticks checked)\n  position off by more than %.0f units: %d (max %.2f)\n  hook grab state different: %d, hooked player different: %d\n",
			m_ServerChecks, BIG_ERROR, m_ServerPosMismatches, m_ServerPosMax, m_ServerHookMismatches, m_ServerHookedMismatches);
		Details += aBuf;
		if(m_ServerHookMismatches > 0)
		{
			str_format(aBuf, sizeof(aBuf), "the server disagreed with the predicted hook %d times, check the events file for server_mismatch", m_ServerHookMismatches);
			vProblems.push_back(aBuf);
		}
		if(m_ServerChecks > 0 && m_ServerPosMismatches * 100 > m_ServerChecks)
		{
			str_format(aBuf, sizeof(aBuf), "%.1f%% of ticks were mispredicted by the server, the prediction margin may be too low", 100.0 * m_ServerPosMismatches / m_ServerChecks);
			vProblems.push_back(aBuf);
		}
	}
	Details += "\nproblems found\n";
	if(vProblems.empty())
		Details += "  none\n";
	for(const std::string &Problem : vProblems)
		Details += "  - " + Problem + "\n";

	{
		int TotalLost = 0;
		std::vector<float> vAllAim;
		for(const SPress &Press : m_vPresses)
		{
			if(Press.m_Lost)
				TotalLost++;
			if(Press.m_AimError >= 0.0f)
				vAllAim.push_back(Press.m_AimError);
		}
		const float MovingSeconds = m_MovingTicks / (float)std::max(1, Client()->GameTickSpeed());
		double Budget = 3.0;
		const double ReversalsPerSec = m_Reversals / Seconds;
		if(ReversalsPerSec > 0.15)
			Budget -= std::min(0.6, (ReversalsPerSec - 0.15) * 1.5);
		if(ShortShare > 10.0)
			Budget -= std::min(0.5, (ShortShare - 10.0) * 0.03);
		if(PredJitter > 3.0)
			Budget -= std::min(0.8, (PredJitter - 3.0) * 0.1);
		if(CorrectionsPerSec > 0.2)
			Budget -= std::min(0.6, CorrectionsPerSec * 0.3);
		Budget = std::max(1.5, Budget);

		double Offset = 1.0;
		if(ShiftWorst > 0.0 && CurrentOffset > 0.0)
		{
			Offset = 0.8;
			for(double Candidate = 1.5; Candidate >= 0.8; Candidate -= 0.05)
			{
				if(ShiftWorst * (0.5 + Candidate) / (0.5 + CurrentOffset) <= Budget)
				{
					Offset = Candidate;
					break;
				}
			}
		}
		Offset = std::clamp(std::round(Offset * 20.0) / 20.0, 0.8, 1.5);

		m_Result = SResult();
		m_Result.m_Valid = true;
		m_Result.m_EnoughData = MovingSeconds >= CALIBRATION_SECONDS * 0.75f && !m_vShifts.empty();
		m_Result.m_Amount = (int)std::round(Offset * 100.0);
		m_Result.m_Smoothing = CorrectionsPerSec < 0.2 ? 0 : (CorrectionAvg > 6.0 ? 30 : 15);
		m_Result.m_Adaptive = (PredJitter > 8.0 && CorrectionsPerSec > 0.2) || CorrectionsPerSec > 1.0;
		m_Result.m_JumpEase = Percentile(m_vJumpShifts, 0.5f) > 6.0f || m_vJumpShifts.empty();
		m_Result.m_SubTickAiming = true;
		m_Result.m_Debounce = true;
		m_Result.m_SnapTap = m_Overlaps > 0 && m_Reversals > 0 && m_Overlaps * 3 > m_Reversals;
		m_Result.m_MovingSeconds = MovingSeconds;
		m_Result.m_PingMs = (float)PredAvg;
		m_Result.m_JitterMs = (float)PredJitter;
		m_Result.m_ShiftWorst = (float)ShiftWorst;
		m_Result.m_JumpScreenMax = Percentile(m_vJumpScreenSteps, 1.0f);
		m_Result.m_CorrectionsPerSec = (float)CorrectionsPerSec;
		m_Result.m_AimDrift = Percentile(vAllAim, 0.9f);
		m_Result.m_Lost = TotalLost;
		m_Result.m_Overlaps = m_Overlaps;
		m_Result.m_ServerMismatches = m_ServerPosMismatches + m_ServerHookMismatches;
	}

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s_summary.txt", m_aBaseName);
	if(IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE))
	{
		io_write(File, aSummary, str_length(aSummary));
		io_write(File, Details.c_str(), Details.size());
		io_close(File);
	}

	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "input_log", aSummary);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "input_log", Details.c_str());
	char aFullPath[IO_MAX_PATH_LENGTH];
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, aPath, aFullPath, sizeof(aFullPath));
	if(m_Calibrating)
	{
		RestoreCalibrationConfig();
		m_Calibrating = false;
		char aMsg[256];
		if(m_Result.m_EnoughData)
			str_format(aMsg, sizeof(aMsg), "Your best 667 input: offset %.2f, smoothing %d ms, adaptive %s. Open Settings / Cecen Client / Gameplay to apply it.", m_Result.m_Amount / 100.0f, m_Result.m_Smoothing, m_Result.m_Adaptive ? "on" : "off");
		else
			str_format(aMsg, sizeof(aMsg), "Calibration stopped after %.0f s of movement, the result is only a rough guess. Run it again for a better one.", m_Result.m_MovingSeconds);
		Message(aMsg);
	}
	else if(!m_Quiet)
	{
		char aMsg[IO_MAX_PATH_LENGTH + 128];
		str_format(aMsg, sizeof(aMsg), "Input log saved. Suggested: offset %.2f, smoothing %d ms. Summary: %s", RecommendedOffset, RecommendedSmoothing, aFullPath);
		Message(aMsg);
	}
}

float CFastInputLog::CalibrationProgress() const
{
	if(!m_Calibrating)
		return 0.0f;
	const int Target = CALIBRATION_SECONDS * std::max(1, Client()->GameTickSpeed());
	return std::clamp(m_MovingTicks / (float)Target, 0.0f, 1.0f);
}

void CFastInputLog::StartCalibration()
{
	if(m_Calibrating)
		return;
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		Message("Join a server first, the calibration needs live gameplay.");
		return;
	}
	if(m_Active)
		Stop();

	m_Saved.m_Inputs = g_Config.m_ScInputs;
	m_Saved.m_Amount = g_Config.m_ScSixInputAmount;
	m_Saved.m_Smoothing = g_Config.m_ScSixInputSmoothing;
	m_Saved.m_Adaptive = g_Config.m_ScSixInputAdaptive;
	m_Saved.m_SubTick = g_Config.m_ScSixInputSubTick;
	m_Saved.m_JumpEase = g_Config.m_ScSixInputJumpEase;

	g_Config.m_ScInputs = SC_INPUTS_SIX;
	g_Config.m_ScSixInputAmount = 150;
	g_Config.m_ScSixInputSmoothing = 0;
	g_Config.m_ScSixInputAdaptive = 0;
	g_Config.m_ScSixInputSubTick = 0;
	g_Config.m_ScSixInputJumpEase = 1;

	m_Result.m_Valid = false;
	m_Quiet = true;
	Start();
	m_Quiet = false;
	if(!m_Active)
	{
		RestoreCalibrationConfig();
		return;
	}
	m_Calibrating = true;
	char aMsg[256];
	str_format(aMsg, sizeof(aMsg), "Finding your best input: play normally for %d minutes of movement. Run sc_input_calibrate again to cancel.", CALIBRATION_SECONDS / 60);
	Message(aMsg);
}

void CFastInputLog::CancelCalibration()
{
	if(!m_Calibrating)
		return;
	RestoreCalibrationConfig();
	m_Calibrating = false;
	m_Quiet = true;
	Stop();
	m_Quiet = false;
	m_Result.m_Valid = false;
	Message("Calibration cancelled, your previous input settings are back.");
}

void CFastInputLog::RestoreCalibrationConfig()
{
	g_Config.m_ScInputs = m_Saved.m_Inputs;
	g_Config.m_ScSixInputAmount = m_Saved.m_Amount;
	g_Config.m_ScSixInputSmoothing = m_Saved.m_Smoothing;
	g_Config.m_ScSixInputAdaptive = m_Saved.m_Adaptive;
	g_Config.m_ScSixInputSubTick = m_Saved.m_SubTick;
	g_Config.m_ScSixInputJumpEase = m_Saved.m_JumpEase;
}

void CFastInputLog::ApplyResult()
{
	if(!m_Result.m_Valid)
		return;
	g_Config.m_ScInputs = SC_INPUTS_SIX;
	g_Config.m_ScSixInputAmount = m_Result.m_Amount;
	g_Config.m_ScSixInputSmoothing = m_Result.m_Smoothing;
	g_Config.m_ScSixInputAdaptive = m_Result.m_Adaptive;
	g_Config.m_ScSixInputJumpEase = m_Result.m_JumpEase;
	g_Config.m_ScSixInputSubTick = 0;
	g_Config.m_ClSubTickAiming = 1;
	g_Config.m_ScInputDebounce = 1;
	if(m_Result.m_SnapTap)
		g_Config.m_ScSnapTap = 1;
	GameClient()->m_SixInput.Reset();
	m_Result.m_Valid = false;
	Message("Your best 667 input settings are applied.");
}

void CFastInputLog::OnRender()
{
	if(!m_Calibrating)
		return;
	if(CalibrationProgress() >= 1.0f)
	{
		Stop();
		return;
	}
	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(Width, Height);

	CUIRect Box = {Width / 2.0f - 70.0f, 34.0f, 140.0f, 22.0f};
	Box.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.45f), IGraphics::CORNER_ALL, 5.0f);
	CUIRect Label, Bar;
	Box.Margin(4.0f, &Box);
	Box.HSplitTop(9.0f, &Label, &Bar);
	Bar.HSplitTop(2.0f, nullptr, &Bar);
	const float Progress = CalibrationProgress();
	const int SecondsLeft = (int)std::ceil((1.0f - Progress) * CALIBRATION_SECONDS);
	char aText[96];
	str_format(aText, sizeof(aText), "Finding your best input · %d:%02d left", SecondsLeft / 60, SecondsLeft % 60);
	Ui()->DoLabel(&Label, aText, 6.5f, TEXTALIGN_MC);
	Bar.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.15f), IGraphics::CORNER_ALL, 1.5f);
	Bar.w *= Progress;
	if(Bar.w > 0.5f)
		Bar.Draw(ColorRGBA(0.45f, 0.8f, 1.0f, 0.9f), IGraphics::CORNER_ALL, 1.5f);
}
