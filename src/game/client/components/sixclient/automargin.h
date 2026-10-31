#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_AUTOMARGIN_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_AUTOMARGIN_H

#include <game/client/component.h>

#include <cstdint>

class CBcAutoMargin : public CComponent
{
public:
	enum
	{
		WINDOW_SIZE = 150,
	};

private:
	int m_aWindow[WINDOW_SIZE] = {};
	int m_WindowCount = 0;
	int m_WindowPos = 0;
	int64_t m_ReadCursor = -1;
	float m_CheckTimer = 0.0f;
	float m_HoldTimer = 0.0f;
	bool m_WasEnabled = false;
	int m_SavedMargin = -1;
	int m_Jitter = -1;
	float m_LatePercent = 0.0f;

	void ResetState();
	void ReadSamples();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnUpdate() override;

	bool HasData() const { return m_Jitter >= 0; }
	int Jitter() const { return m_Jitter; }
	float LatePercent() const { return m_LatePercent; }
};

#endif
