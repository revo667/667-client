/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-2f347c7d */

#include "six_input.h"

#include <base/math.h>
#include <base/time.h>

#include <engine/shared/config.h>

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float TELEPORT_DISTANCE = 160.0f;
	constexpr float ERROR_TOLERANCE = 1.0f;
	constexpr float ERROR_RANGE = 24.0f;
	constexpr float MIN_SCALE = 0.7f;
	constexpr float MAX_OFFSET = 64.0f;
}

void CSixInput::Reset()
{
	std::fill(std::begin(m_aGuessTick), std::end(m_aGuessTick), -1);
	m_LastEvaluatedTick = -1;
	m_ErrorAvg = 0.0f;
	m_Scale = 1.0f;
	m_Offset = vec2(0.0f, 0.0f);
	m_InputOffset = vec2(0.0f, 0.0f);
	m_LastSmoothTime = 0;
}

bool CSixInput::Active() const
{
	return g_Config.m_ScInputs == SC_INPUTS_SIX && g_Config.m_ScSixInputAmount > 0;
}

float CSixInput::OffsetTicks() const
{
	if(!Active())
		return 0.0f;
	const float Max = g_Config.m_ScSixInputAmount / 100.0f;
	return g_Config.m_ScSixInputAdaptive ? Max * m_Scale : Max;
}

void CSixInput::OnPredicted(const vec2 *pPredPos, const int *pPredTick, int RegularTick, int FinalTick)
{
	if(RegularTick > m_LastEvaluatedTick && RegularTick > 0)
	{
		const int Slot = RegularTick % HISTORY;
		if(m_aGuessTick[Slot] == RegularTick && pPredTick[Slot] == RegularTick)
		{
			const float Error = distance(m_aGuessPos[Slot], pPredPos[Slot]);
			if(Error < TELEPORT_DISTANCE)
			{
				m_ErrorAvg = m_ErrorAvg * 0.92f + Error * 0.08f;
				const float Target = std::clamp(1.0f - (m_ErrorAvg - ERROR_TOLERANCE) / ERROR_RANGE, MIN_SCALE, 1.0f);
				m_Scale += (Target - m_Scale) * 0.1f;
			}
		}
		m_LastEvaluatedTick = RegularTick;
	}

	for(int Tick = RegularTick + 1; Tick <= FinalTick; Tick++)
	{
		const int Slot = Tick % HISTORY;
		if(pPredTick[Slot] != Tick)
			continue;
		m_aGuessPos[Slot] = pPredPos[Slot];
		m_aGuessTick[Slot] = Tick;
	}
}

void CSixInput::OnCorrection(vec2 Before, vec2 After, bool InputChanged, bool JumpPressed, float IntraTick, float OffsetTicks)
{
	const vec2 Delta = Before - After;
	const float Length = length(Delta);
	if(Length >= TELEPORT_DISTANCE)
	{
		m_Offset = vec2(0.0f, 0.0f);
		m_InputOffset = vec2(0.0f, 0.0f);
		return;
	}
	if(InputChanged)
	{
		float Share = 0.0f;
		if(JumpPressed && g_Config.m_ScSixInputJumpEase)
			Share = 1.0f;
		else if(g_Config.m_ScSixInputSubTick && IntraTick + OffsetTicks > 0.0f)
			Share = std::clamp(IntraTick / (IntraTick + OffsetTicks), 0.0f, 1.0f);
		if(Share <= 0.0f)
			return;
		m_InputOffset += Delta * Share;
		const float InputLength = length(m_InputOffset);
		if(InputLength > MAX_OFFSET)
			m_InputOffset *= MAX_OFFSET / InputLength;
		return;
	}
	if(g_Config.m_ScSixInputSmoothing <= 0)
		return;
	m_Offset += Delta;
	const float OffsetLength = length(m_Offset);
	if(OffsetLength > MAX_OFFSET)
		m_Offset *= MAX_OFFSET / OffsetLength;
}

vec2 CSixInput::Smooth(vec2 Pos, int64_t Now, int TickSpeed)
{
	const float Dt = m_LastSmoothTime ? std::clamp((Now - m_LastSmoothTime) / (float)time_freq(), 0.0f, 0.1f) : 0.0f;
	m_LastSmoothTime = Now;

	if(g_Config.m_ScSixInputSmoothing <= 0)
		m_Offset = vec2(0.0f, 0.0f);
	else
		m_Offset *= std::exp(-Dt / (g_Config.m_ScSixInputSmoothing / 1000.0f));
	if(length(m_Offset) < 0.05f)
		m_Offset = vec2(0.0f, 0.0f);

	const float InputLength = length(m_InputOffset);
	const float CatchUp = InputLength * Dt * std::max(TickSpeed, 1) * 3.0f + Dt * 40.0f;
	m_InputOffset = InputLength <= CatchUp ? vec2(0.0f, 0.0f) : m_InputOffset * ((InputLength - CatchUp) / InputLength);

	return Pos + m_Offset + m_InputOffset;
}
