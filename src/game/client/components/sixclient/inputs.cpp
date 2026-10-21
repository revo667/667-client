#include "inputs.h"

#include <base/math.h>
#include <engine/shared/config.h>

#include <algorithm>
#include <cmath>

namespace
{
	float gs_SixOffsetTicks = 0.0f;

	float FastModeOffsetTicks()
	{
		if(g_Config.m_ScInputs != SC_INPUTS_FAST || g_Config.m_TcFastInputAmount <= 0)
			return 0.0f;
		return g_Config.m_TcFastInputAmount / 20.0f;
	}

	float SaikoModeOffsetTicks()
	{
		if(g_Config.m_ScInputs != SC_INPUTS_SAIKO || g_Config.m_ScSaikoInputAmount <= 0)
			return 0.0f;
		return g_Config.m_ScSaikoInputAmount / 100.0f;
	}

	float MeowModeOffsetTicks()
	{
		if(g_Config.m_ScInputs != SC_INPUTS_MEOW || g_Config.m_ScMeowInputAmount <= 0)
			return 0.0f;
		return g_Config.m_ScMeowInputAmount / 1000.0f;
	}
} // namespace

float ScInputs::EffectiveOffsetTicks()
{
	switch(g_Config.m_ScInputs)
	{
	case SC_INPUTS_FAST: return FastModeOffsetTicks();
	case SC_INPUTS_SAIKO: return SaikoModeOffsetTicks();
	case SC_INPUTS_MEOW: return MeowModeOffsetTicks();
	case SC_INPUTS_SIX: return gs_SixOffsetTicks;
	default: return 0.0f;
	}
}

void ScInputs::SetSixOffsetTicks(float OffsetTicks)
{
	gs_SixOffsetTicks = OffsetTicks;
}

int ScInputs::PredictionTicks(float OffsetTicks)
{
	if(OffsetTicks <= 0.0f)
		return 0;
	return (int)std::ceil(OffsetTicks);
}

int ScInputs::PredictionTicksOthers(float OffsetTicks)
{
	// meow input only gives other tees one extra tick
	if(g_Config.m_ScInputs == SC_INPUTS_MEOW)
		return OffsetTicks > 0.0f ? 1 : 0;
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

bool ScInputs::FastOthers()
{
	return g_Config.m_ScInputs == SC_INPUTS_FAST && g_Config.m_TcFastInputOthers != 0;
}

bool ScInputs::SaikoOthers()
{
	return g_Config.m_ScInputs == SC_INPUTS_SAIKO && g_Config.m_ScSaikoInputOthers != 0;
}

bool ScInputs::CloudOthers()
{
	return g_Config.m_ScInputs == SC_INPUTS_CLOUD && g_Config.m_ScCloudInputOthers != 0;
}

bool ScInputs::SixOthers()
{
	return g_Config.m_ScInputs == SC_INPUTS_SIX && g_Config.m_ScSixInputOthers != 0;
}

bool ScInputs::MeowOthers()
{
	return g_Config.m_ScInputs == SC_INPUTS_MEOW && g_Config.m_ScMeowInputOthers != 0;
}

bool ScInputs::AnyOthers()
{
	return FastOthers() || SaikoOthers() || CloudOthers() || SixOthers() || MeowOthers();
}

bool ScInputs::ImmediateOthers()
{
	return SaikoOthers();
}
