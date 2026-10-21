#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_INPUTS_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_INPUTS_H

#include <base/vmath.h>

namespace ScInputs
{
	float EffectiveOffsetTicks();
	void SetSixOffsetTicks(float OffsetTicks);

	int PredictionTicks(float OffsetTicks);
	int PredictionTicksOthers(float OffsetTicks);

	void ApplyOffset(float OffsetTicks, int &Tick, float &Intra);

	bool FastOthers();
	bool SaikoOthers();
	bool CloudOthers();
	bool SixOthers();
	bool MeowOthers();
	bool AnyOthers();
	bool ImmediateOthers();
} // namespace ScInputs

#endif
