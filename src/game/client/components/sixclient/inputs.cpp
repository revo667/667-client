#include "inputs.h"

#include <base/math.h>
#include <engine/shared/config.h>

#include <algorithm>
#include <cmath>

namespace
{
	float FastModeOffsetTicks()
	{
		if(g_Config.m_ScInputs != SC_INPUTS_FAST || g_Config.m_TcFastInputAmount <= 0)
			return 0.0f;
		return g_Config.m_TcFastInputAmount / 20.0f;
	}

	float BestModeOffsetTicks()
	{
		if(g_Config.m_ScInputs != SC_INPUTS_BEST || g_Config.m_ScBestInputAmount <= 0)
			return 0.0f;

		float Offset = g_Config.m_ScBestInputAmount / 100.0f;

		if(g_Config.m_ScBestInputSmoothing > 0)
			Offset *= 1.0f - (g_Config.m_ScBestInputSmoothing / 200.0f);

		if(g_Config.m_ScBestInputLatencyComp > 0)
			Offset *= 1.0f + (g_Config.m_ScBestInputLatencyComp / 100.0f);

		return Offset;
	}

	float SaikoModeOffsetTicks()
	{
		if(g_Config.m_ScInputs != SC_INPUTS_SAIKO || g_Config.m_ScSaikoInputAmount <= 0)
			return 0.0f;
		return g_Config.m_ScSaikoInputAmount / 100.0f;
	}

	float DeltaModeOffsetTicks()
	{
		if(g_Config.m_ScInputs != SC_INPUTS_DELTA || g_Config.m_ScDeltaInputAmount <= 0)
			return 0.0f;
		return g_Config.m_ScDeltaInputAmount / 100.0f;
	}

	float FModeOffsetTicks()
	{
		if(g_Config.m_ScInputs != SC_INPUTS_F || g_Config.m_ScFInputAmount <= 0)
			return 0.0f;
		return g_Config.m_ScFInputAmount / 1000.0f;
	}
} // namespace

float ScInputs::EffectiveOffsetTicks()
{
	switch(g_Config.m_ScInputs)
	{
	case SC_INPUTS_FAST: return FastModeOffsetTicks();
	case SC_INPUTS_BEST: return BestModeOffsetTicks();
	case SC_INPUTS_SAIKO: return SaikoModeOffsetTicks();
	case SC_INPUTS_DELTA: return DeltaModeOffsetTicks();
	case SC_INPUTS_F: return FModeOffsetTicks();
	default: return 0.0f;
	}
}

int ScInputs::PredictionTicks(float OffsetTicks)
{
	if(OffsetTicks <= 0.0f)
		return 0;
	return (int)std::ceil(OffsetTicks);
}

int ScInputs::PredictionTicksOthers(float OffsetTicks)
{
	return PredictionTicks(OffsetTicks);
}

void ScInputs::ApplyOffset(float OffsetTicks, int &Tick, float &Intra)
{
	if(OffsetTicks <= 0.0f)
		return;

	const int WholeTicks = (int)OffsetTicks;
	const float OffsetIntra = OffsetTicks - WholeTicks;

	const float CombinedIntra = Intra + OffsetIntra;
	const int CarryOverTicks = (int)CombinedIntra;

	Tick += WholeTicks + CarryOverTicks;
	Intra = CombinedIntra - CarryOverTicks;
}

float ScInputs::BestInterpolationAmount(float Fraction, float DeltaLength, bool Enable)
{
	if(!Enable)
		return Fraction;

	const float T = std::clamp(Fraction, 0.0f, 1.0f);
	const float T2 = T * T;
	const float CubicT = 3.0f * T2 - 2.0f * T2 * T;
	switch(std::clamp(g_Config.m_ScBestInputInterpolation, 1, 3))
	{
	case 2:
		return CubicT;
	case 3:
		return mix(T, CubicT, std::clamp(DeltaLength / 1000.0f, 0.0f, 1.0f));
	default:
		return T;
	}
}

vec2 ScInputs::BestInterpolate(vec2 PrevPos, vec2 CurPos, float Fraction, bool Enable)
{
	return mix(PrevPos, CurPos, BestInterpolationAmount(Fraction, length(CurPos - PrevPos), Enable));
}

float ScInputs::BestInterpolate(float PrevPos, float CurPos, float Fraction, bool Enable)
{
	return mix(PrevPos, CurPos, BestInterpolationAmount(Fraction, absolute(CurPos - PrevPos), Enable));
}

bool ScInputs::FastOthers()
{
	return g_Config.m_ScInputs == SC_INPUTS_FAST && g_Config.m_TcFastInputOthers != 0;
}

bool ScInputs::BestOthers()
{
	return g_Config.m_ScInputs == SC_INPUTS_BEST && g_Config.m_ScBestInputOthers != 0;
}

bool ScInputs::SaikoOthers()
{
	return g_Config.m_ScInputs == SC_INPUTS_SAIKO && g_Config.m_ScSaikoInputOthers != 0;
}

bool ScInputs::DeltaOthers()
{
	return g_Config.m_ScInputs == SC_INPUTS_DELTA && g_Config.m_ScDeltaInputOthers != 0;
}

bool ScInputs::FOthers()
{
	return g_Config.m_ScInputs == SC_INPUTS_F && g_Config.m_ScFInputOthers != 0;
}

bool ScInputs::CloudOthers()
{
	return g_Config.m_ScInputs == SC_INPUTS_CLOUD && g_Config.m_ScCloudInputOthers != 0;
}

bool ScInputs::AnyOthers()
{
	return FastOthers() || BestOthers() || SaikoOthers() || DeltaOthers() || FOthers() || CloudOthers();
}

bool ScInputs::ImmediateOthers()
{
	return BestOthers() || SaikoOthers() || DeltaOthers();
}
