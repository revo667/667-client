/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-5ce5677c */

#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_FAST_INPUT_LOG_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_FAST_INPUT_LOG_H

#include <base/types.h>
#include <base/vmath.h>

#include <engine/console.h>

#include <generated/protocol.h>

#include <game/client/component.h>
#include <game/gamecore.h>

#include <cstdint>
#include <vector>

class CFastInputLog : public CComponent
{
public:
	static constexpr int HISTORY = 200;

	enum
	{
		KEY_LEFT = 0,
		KEY_RIGHT,
		KEY_JUMP,
		KEY_HOOK,
		KEY_FIRE,
		NUM_KEYS,
	};
	static constexpr int MAX_HORIZON = 3;

	struct SResult
	{
		bool m_Valid = false;
		bool m_EnoughData = false;
		int m_Amount = 140;
		int m_Smoothing = 0;
		int m_Adaptive = 0;
		int m_JumpEase = 1;
		bool m_SubTickAiming = true;
		bool m_Debounce = true;
		bool m_SnapTap = false;
		float m_MovingSeconds = 0.0f;
		float m_PingMs = 0.0f;
		float m_JitterMs = 0.0f;
		float m_ShiftWorst = 0.0f;
		float m_JumpScreenMax = 0.0f;
		float m_CorrectionsPerSec = 0.0f;
		float m_AimDrift = 0.0f;
		int m_Lost = 0;
		int m_Overlaps = 0;
		int m_ServerMismatches = 0;
	};

	static constexpr int CALIBRATION_SECONDS = 120;

	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnRender() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnShutdown() override;

	bool Active() const { return m_Active; }
	bool Calibrating() const { return m_Calibrating; }
	float CalibrationProgress() const;
	void StartCalibration();
	void CancelCalibration();
	const SResult &Result() const { return m_Result; }
	void ApplyResult();
	void ClearResult() { m_Result.m_Valid = false; }
	void Start();
	void Stop();
	void Mark();

	void OnPredicted(int RegularTick, int FinalTick, const vec2 *pPredPos, const int *pPredTick, const CCharacterCore &RegularCore, const CNetObj_PlayerInput *pInput, float OffsetTicks, int PredictionMs);
	void OnKey(int Key, bool Pressed, vec2 MousePos, float IntraTick);
	void OnSend(const CNetObj_PlayerInput &Input);
	void OnServerState(int Tick, const CNetObj_Character &Char);
	void OnInputEvent(float IntraTick);
	void OnCorrection(vec2 Before, vec2 After, bool InputChanged, bool JumpPressed);
	void OnFrame(vec2 RawPos, vec2 FinalPos, vec2 RegularPos, bool HasRegular);

private:
	struct SPress
	{
		int m_Key = 0;
		double m_Down = 0.0;
		double m_Up = -1.0;
		double m_Sent = -1.0;
		bool m_Lost = false;
		float m_Intra = 0.0f;
		vec2 m_Mouse = vec2(0.0f, 0.0f);
		float m_AimError = -1.0f;
	};

	struct SPredState
	{
		int m_Tick = -1;
		vec2 m_Pos = vec2(0.0f, 0.0f);
		int m_HookState = 0;
		int m_HookedPlayer = -1;
	};

	struct SSavedConfig
	{
		int m_Inputs = 0;
		int m_Amount = 0;
		int m_Smoothing = 0;
		int m_Adaptive = 0;
		int m_SubTick = 0;
		int m_JumpEase = 0;
	};

	bool m_Calibrating = false;
	bool m_Quiet = false;
	SSavedConfig m_Saved;
	SResult m_Result;
	void RestoreCalibrationConfig();

	std::vector<SPress> m_vPresses;
	int m_aOpenPress[NUM_KEYS] = {};
	bool m_aKeyDown[NUM_KEYS] = {};
	double m_aLastUp[NUM_KEYS] = {};
	int m_PendingKeyMask = 0;
	int m_LastSentFire = 0;
	bool m_HasLastSentFire = false;
	double m_OverlapStart = -1.0;
	double m_OverlapTotal = 0.0;
	int m_Overlaps = 0;
	int m_Reversals = 0;
	std::vector<float> m_avKeyShifts[NUM_KEYS];
	SPredState m_aPredStates[HISTORY];
	int m_ServerChecks = 0;
	int m_ServerPosMismatches = 0;
	float m_ServerPosMax = 0.0f;
	int m_ServerHookMismatches = 0;
	int m_ServerHookedMismatches = 0;

	static void ConToggle(IConsole::IResult *pResult, void *pUserData);
	static void ConMark(IConsole::IResult *pResult, void *pUserData);
	static void ConCalibrate(IConsole::IResult *pResult, void *pUserData);
	static void ConchainInputs(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	void WriteLine(IOHANDLE File, const char *pLine);
	void Message(const char *pText);
	double Now() const;

	bool m_Active = false;
	IOHANDLE m_TicksFile = nullptr;
	IOHANDLE m_EventsFile = nullptr;
	IOHANDLE m_FramesFile = nullptr;
	char m_aBaseName[128] = "";
	int64_t m_StartTime = 0;
	int64_t m_LastFrameTime = 0;

	vec2 m_aaGuessPos[MAX_HORIZON + 1][HISTORY] = {};
	int m_aaGuessTick[MAX_HORIZON + 1][HISTORY] = {};
	int m_LastTick = -1;
	CNetObj_PlayerInput m_LastInput = {};
	bool m_HasLastInput = false;
	int m_DirectionRun = 0;
	int m_LastDirection = 0;

	int m_Ticks = 0;
	int m_MovingTicks = 0;
	int m_Frames = 0;
	int m_Marks = 0;
	int m_aChanges[5] = {};
	std::vector<int> m_vDirectionHolds;
	double m_PredMsSum = 0.0;
	double m_PredMsSqSum = 0.0;
	int m_PredMsMin = 0;
	int m_PredMsMax = 0;
	double m_aErrorSum[MAX_HORIZON + 1] = {};
	int m_aErrorCount[MAX_HORIZON + 1] = {};
	int m_aErrorBig[MAX_HORIZON + 1] = {};
	int m_InputEvents = 0;
	double m_InputIntraSum = 0.0;
	int m_Corrections = 0;
	double m_CorrectionSum = 0.0;
	double m_CorrectionMax = 0.0;
	std::vector<float> m_vShifts;
	std::vector<float> m_vJumpShifts;
	std::vector<float> m_vJumpScreenSteps;
	double m_JumpWatchUntil = -1.0;
	float m_JumpWatchMax = 0.0f;
	vec2 m_LastFinalPos = vec2(0.0f, 0.0f);
	bool m_HasLastFinalPos = false;
	double m_LeadSum = 0.0;
	double m_SmoothSum = 0.0;
	double m_FrameDtSum = 0.0;
	double m_FrameDtMax = 0.0;
	float m_OffsetSum = 0.0f;
};

#endif
