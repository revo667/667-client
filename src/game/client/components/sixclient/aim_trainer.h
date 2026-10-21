/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-cbaff306 */

#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_AIM_TRAINER_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_AIM_TRAINER_H

#include <base/color.h>
#include <base/vmath.h>

#include <game/client/component.h>

#include <cstdint>
#include <random>
#include <vector>

class CAimTrainer : public CComponent
{
public:
	enum EMode
	{
		MODE_FLICK = 0,
		MODE_TIMED,
		MODE_PSA,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnShutdown() override;
	void OnRender() override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	bool OnInput(const IInput::CEvent &Event) override;

	void Activate(int Mode = -1);
	void Deactivate();
	bool IsActive() const { return m_Active; }

private:
	enum EState
	{
		STATE_READY = 0,
		STATE_PLAYING,
		STATE_RESULTS,
		STATE_PSA_RESULT,
	};

	struct STarget
	{
		vec2 m_Pos;
		int64_t m_SpawnTime;
	};

	struct SEffect
	{
		bool m_Hit;
		vec2 m_Pos;
		int64_t m_Time;
		char m_aText[16];
	};

	struct STrialResult
	{
		int m_Hits = 0;
		int m_Clicks = 0;
		float m_Seconds = 0.0f;
		float m_AvgReactionMs = 0.0f;
		float Accuracy() const { return m_Clicks > 0 ? (float)m_Hits / m_Clicks : 0.0f; }
		float Score() const { return m_Seconds > 0.0f ? m_Hits / m_Seconds * Accuracy() * Accuracy() : 0.0f; }
	};

	bool m_Active = false;
	int m_State = STATE_READY;
	int64_t m_ReadyTime = 0;
	int m_Mode = MODE_FLICK;
	vec2 m_Cursor = vec2(0.0f, 0.0f);
	vec2 m_World = vec2(1.0f, 1.0f);
	int64_t m_ClickTime = 0;
	std::mt19937 m_Random;

	std::vector<STarget> m_vTargets;
	std::vector<SEffect> m_vEffects;
	int m_TargetsLeft = 0;
	int64_t m_TrialStart = 0;
	float m_ReactionSum = 0.0f;
	STrialResult m_Current;
	STrialResult m_LastResult;
	bool m_NewBest = false;

	int m_PsaRound = 0;
	bool m_PsaTestingHigh = false;
	float m_PsaLow = 0.0f;
	float m_PsaHigh = 0.0f;
	STrialResult m_PsaLowResult;
	float m_PsaRecommended = 0.0f;
	bool m_PsaSensSaved = false;
	bool m_PsaDyncam = false;
	int m_PsaOriginalSens = 0;
	float m_PsaLastScoreA = -1.0f;
	float m_PsaLastScoreB = -1.0f;

	int *SensConfig() const;
	void ApplyPsaTestSens();
	void RestorePsaSens();
	void RenderPsaPanel();
	float BaseSensitivity() const;
	float ActiveSensitivity() const;
	float TargetRadius() const { return 32.0f; }
	void UpdateWorldSize();
	vec2 RandomTargetPos() const;
	void StartTrial();
	void FinishTrial();
	void HandleClick();
	bool Button(float CenterX, float Y, float Width, const char *pText, bool Clicked, bool Primary = false);

	void DrawRing(vec2 Center, float Inner, float Outer, ColorRGBA Color, int Segments = 48);
	void DrawDisc(vec2 Center, float Radius, ColorRGBA Color, int Segments = 48);
	void DrawTextCentered(float X, float Y, float Size, const char *pText, ColorRGBA Color);
	void RenderBackground();
	void RenderTargets();
	void RenderEffects();
	void RenderCrosshair();
	void RenderHud();
	void EnterReady();
	void RenderReady();
	void RenderResults(bool Clicked);
	void RenderPsaResult(bool Clicked);

	bool m_PendingClick = false;
};

#endif
