#include "automargin.h"

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr float CHECK_INTERVAL_SECONDS = 0.5f;
constexpr float HOLD_AFTER_RISE_SECONDS = 4.0f;
constexpr int MIN_SAMPLES = 25;
constexpr float PERCENTILE = 0.97f;
constexpr int SAFETY_MS = 2;
constexpr int STEP_UP_MS = 10;
constexpr int STEP_DOWN_MS = 1;
}

void CBcAutoMargin::ResetState()
{
	m_WindowCount = 0;
	m_WindowPos = 0;
	m_ReadCursor = -1;
	m_CheckTimer = 0.0f;
	m_HoldTimer = 0.0f;
	m_Jitter = -1;
	m_LatePercent = 0.0f;
}

void CBcAutoMargin::OnReset()
{
	ResetState();
}

void CBcAutoMargin::OnStateChange(int NewState, int OldState)
{
	if(NewState != OldState)
		ResetState();
}

void CBcAutoMargin::ReadSamples()
{
	const int64_t Count = Client()->InputTimingCount();
	if(m_ReadCursor < 0 || m_ReadCursor > Count)
		m_ReadCursor = Count;
	m_ReadCursor = std::max(m_ReadCursor, Count - (int64_t)IClient::INPUT_TIMING_HISTORY);
	for(; m_ReadCursor < Count; m_ReadCursor++)
	{
		m_aWindow[m_WindowPos] = Client()->InputTimingLateness(m_ReadCursor);
		m_WindowPos = (m_WindowPos + 1) % WINDOW_SIZE;
		m_WindowCount = std::min(m_WindowCount + 1, (int)WINDOW_SIZE);
	}
}

void CBcAutoMargin::OnUpdate()
{
	if(!g_Config.m_ScAutoMargin)
	{
		if(m_WasEnabled)
		{
			if(m_SavedMargin >= 0)
				g_Config.m_ClPredictionMargin = m_SavedMargin;
			m_WasEnabled = false;
			m_SavedMargin = -1;
		}
		ResetState();
		return;
	}

	if(!m_WasEnabled)
	{
		m_SavedMargin = g_Config.m_ClPredictionMargin;
		m_WasEnabled = true;
	}

	if(Client()->State() != IClient::STATE_ONLINE || !Client()->CustomPredictionMarginSupported())
	{
		ResetState();
		return;
	}

	ReadSamples();

	const float FrameTime = Client()->RenderFrameTime();
	m_HoldTimer = std::max(0.0f, m_HoldTimer - FrameTime);
	m_CheckTimer -= FrameTime;
	if(m_CheckTimer > 0.0f)
		return;
	m_CheckTimer = CHECK_INTERVAL_SECONDS;

	if(m_WindowCount < MIN_SAMPLES)
		return;

	int aSorted[WINDOW_SIZE];
	std::copy(m_aWindow, m_aWindow + m_WindowCount, aSorted);
	const int Nth = std::min(m_WindowCount - 1, (int)std::ceil(m_WindowCount * PERCENTILE) - 1);
	std::nth_element(aSorted, aSorted + Nth, aSorted + m_WindowCount);
	m_Jitter = std::max(0, aSorted[Nth]);

	const int Margin = g_Config.m_ClPredictionMargin;
	int Late = 0;
	for(int i = 0; i < m_WindowCount; i++)
		if(m_aWindow[i] > Margin)
			Late++;
	m_LatePercent = Late * 100.0f / m_WindowCount;

	const int MinMargin = std::min(g_Config.m_ScAutoMarginMin, g_Config.m_ScAutoMarginMax);
	const int MaxMargin = std::max(g_Config.m_ScAutoMarginMin, g_Config.m_ScAutoMarginMax);
	const int Target = std::clamp(m_Jitter + SAFETY_MS, MinMargin, MaxMargin);

	int NewMargin = std::clamp(Margin, MinMargin, MaxMargin);
	if(Target > NewMargin)
	{
		NewMargin += std::min(Target - NewMargin, STEP_UP_MS);
		m_HoldTimer = HOLD_AFTER_RISE_SECONDS;
	}
	else if(Target < NewMargin && m_HoldTimer <= 0.0f)
	{
		NewMargin -= std::min(NewMargin - Target, STEP_DOWN_MS);
	}

	if(NewMargin != Margin)
		g_Config.m_ClPredictionMargin = NewMargin;
}
