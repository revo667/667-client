/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-0bb00e2c */

#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_SIX_INPUT_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_SIX_INPUT_H

#include <base/vmath.h>

#include <cstdint>

class CSixInput
{
public:
	static constexpr int HISTORY = 200;

	void Reset();
	bool Active() const;
	float OffsetTicks() const;
	float Scale() const { return m_Scale; }
	float AverageError() const { return m_ErrorAvg; }

	void OnPredicted(const vec2 *pPredPos, const int *pPredTick, int RegularTick, int FinalTick);
	void OnCorrection(vec2 Before, vec2 After, bool InputChanged, bool JumpPressed, float IntraTick, float OffsetTicks);
	vec2 Smooth(vec2 Pos, int64_t Now, int TickSpeed);

private:
	vec2 m_aGuessPos[HISTORY] = {};
	int m_aGuessTick[HISTORY] = {};
	int m_LastEvaluatedTick = -1;
	float m_ErrorAvg = 0.0f;
	float m_Scale = 1.0f;

	vec2 m_Offset = vec2(0.0f, 0.0f);
	vec2 m_InputOffset = vec2(0.0f, 0.0f);
	int64_t m_LastSmoothTime = 0;
};

#endif
