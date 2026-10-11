/* Copyright © 2026 BestProject Team */
#include "source.h"

namespace SixClientVisualizer
{

namespace
{
class CPassiveVisualizerSource final : public IVisualizerSource
{
public:
	void SetPlaybackHint(const SVisualizerPlaybackHint &Hint) override
	{
		(void)Hint;
	}

	void SetConfig(const SVisualizerConfig &Config) override
	{
		(void)Config;
	}

	bool PollFrame(SVisualizerFrame &OutFrame) override
	{
		OutFrame = SVisualizerFrame();
		OutFrame.m_IsPassiveFallback = true;
		OutFrame.m_BackendStatus = EVisualizerBackendStatus::FALLBACK;
		return false;
	}
};
} // namespace

std::unique_ptr<IVisualizerSource> CreatePassiveVisualizerSource()
{
	return std::make_unique<CPassiveVisualizerSource>();
}

} // namespace 667 ClientVisualizer
